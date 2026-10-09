// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "EverestServiceControl.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"
#include "SystemdService.hpp"

#include <QDeadlineTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QtGlobal>

#include <limits>

namespace {
constexpr int kEverestRestartWaitTimeoutMs = 10000;
constexpr int kEverestRestartPollIntervalMs = 200;
constexpr int kDefaultRpcApiReadyTimeoutSeconds = 20;
constexpr int kMaxRpcApiReadyTimeoutSeconds = std::numeric_limits<int>::max() / 1000;
constexpr int kEverestErrorPresentMonitorPollIntervalMs = 200;
constexpr char kRpcApiReadyTimeoutConfigKey[] = "everest_rpc_api_ready_timeout_seconds";

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif

struct RpcApiReadyTimeout {
    int milliseconds = 0;
    QString source;
};

bool isTransientRpcError(const QString& error) {
    return error == QLatin1String("rpc_api_not_connected") || error == QLatin1String("rpc_api_request_pending") ||
           error == QLatin1String("rpc_api_no_response");
}

void waitForNextPoll(const QDeadlineTimer& deadline, int intervalMs) {
    if (deadline.hasExpired()) {
        return;
    }
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(static_cast<int>(qMin<qint64>(intervalMs, deadline.remainingTime())));
    loop.exec();
}

RpcApiReadyTimeout rpcApiReadyTimeout() {
    const QString configKey = QLatin1String(kRpcApiReadyTimeoutConfigKey);
    const QString baseValue = readBackendConfigValue(configKey);
    const QMap<QString, QString> platformValues = readBackendConfigValues(configKey + QLatin1Char('.'));
    const QByteArray compatibleData = readDeviceTreeCompatibleData();
    QList<QPair<QString, QString>> candidates;
    for (const QByteArray& entry : compatibleData.split('\0')) {
        if (entry.isEmpty()) {
            continue;
        }
        const QString compatible = QString::fromUtf8(entry);
        const auto value = platformValues.constFind(compatible);
        if (value != platformValues.constEnd()) {
            candidates.append({value.value(), configKey + QLatin1Char('.') + compatible});
        }
    }
    if (!baseValue.isEmpty()) {
        candidates.append({baseValue, configKey});
    }
    for (const auto& candidate : candidates) {
        bool valid = false;
        const int timeoutSeconds = candidate.first.toInt(&valid);
        if (valid && timeoutSeconds > 0 && timeoutSeconds <= kMaxRpcApiReadyTimeoutSeconds) {
            return {timeoutSeconds * 1000, candidate.second};
        }
        qWarning().noquote() << QStringLiteral("Ignoring invalid value '%1' for backend config key '%2'; "
                               "expected a positive integer no greater than %3")
                                    .arg(candidate.first, candidate.second)
                                    .arg(kMaxRpcApiReadyTimeoutSeconds);
    }
    return {kDefaultRpcApiReadyTimeoutSeconds * 1000, QStringLiteral("built-in default")};
}
} // namespace

namespace EverestServiceControl {
EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient* rpcApiClient, int evseIndex) {
    return checkEverestStateAllowed(rpcApiClient, QList<int>{evseIndex});
}

EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient* rpcApiClient, const QList<int>& evseIndices) {
    if (!rpcApiClient) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("rpc_api_client_unavailable"),
        };
    }

    if (evseIndices.isEmpty()) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("safety_controller_evse_mapping_unavailable"),
        };
    }

    const QString whitelistValue = readBackendConfigValue(QStringLiteral("everest_restart_allowed_states"));
    if (whitelistValue.isEmpty()) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("everest_restart_allowed_states_missing"),
        };
    }

    QStringList allowedStates = whitelistValue.split(QLatin1Char(','), kSkipEmptyParts);
    for (QString& allowedState : allowedStates) {
        allowedState = allowedState.trimmed();
    }

    return checkEverestStateAllowed(rpcApiClient, evseIndices, allowedStates);
}

EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient* rpcApiClient, const QList<int>& evseIndices,
                                                   const QStringList& allowedStates) {
    if (!rpcApiClient) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("rpc_api_client_unavailable"),
        };
    }

    if (evseIndices.isEmpty()) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("safety_controller_evse_mapping_unavailable"),
        };
    }

    for (const int evseIndex : evseIndices) {
        const RpcApiEvseStateResult evseStateResult = rpcApiClient->getEvseState(evseIndex);
        if (!evseStateResult.success) {
            return EverestStateAllowedResult{
                .success = false,
                .state = QString(),
                .error = evseStateResult.error,
            };
        }
        if (!allowedStates.contains(evseStateResult.state)) {
            return EverestStateAllowedResult{
                .success = false,
                .state = evseStateResult.state,
                .error = QLatin1String(kErrorEverestStateNotAllowed),
            };
        }
    }

    return EverestStateAllowedResult{
        .success = true,
        .state = QString(),
        .error = QString(),
    };
}

EverestErrorPresentResult monitorEverestErrorPresent(RpcApiClient* rpcApiClient, int evseIndex) {
    return monitorEverestErrorPresent(rpcApiClient, QList<int>{evseIndex});
}

EverestErrorPresentResult monitorEverestErrorPresent(RpcApiClient* rpcApiClient, const QList<int>& evseIndices,
                                                     int timeoutMs, bool initialErrorPresent) {
    if (!rpcApiClient) {
        return {.success = false, .errorPresent = false, .error = QStringLiteral("rpc_api_client_unavailable")};
    }
    if (evseIndices.isEmpty()) {
        return {.success = false,
                .errorPresent = false,
                .error = QStringLiteral("safety_controller_evse_mapping_unavailable")};
    }
    if (initialErrorPresent) {
        return {.success = true, .errorPresent = true, .error = {}};
    }

    const QDeadlineTimer deadline(timeoutMs, Qt::PreciseTimer);
    QMap<int, QString> unresolvedErrors;
    for (const int index : evseIndices) {
        unresolvedErrors.insert(index, QStringLiteral("rpc_api_no_response"));
    }
#ifdef EVEREST_UI_RPC_DEBUG
    qInfo() << "EVerest RPC monitor started: phase=error_present; timeout_ms=" << timeoutMs << "evses=" << evseIndices;
#endif
    while (!deadline.hasExpired()) {
        for (const int index : evseIndices) {
            if (deadline.hasExpired()) {
                break;
            }
            const auto result = rpcApiClient->getEvseErrorPresent(index, static_cast<int>(deadline.remainingTime()));
            if (deadline.hasExpired()) {
                unresolvedErrors.insert(index, result.success ? QStringLiteral("rpc_api_no_response") : result.error);
                break;
            }
            if (!result.success) {
                unresolvedErrors.insert(index, result.error);
                qWarning().noquote() << QStringLiteral("EVerest RPC check failed: phase=error_present; evse=%1; "
                                                       "error=%2; remaining_ms=%3")
                                            .arg(index)
                                            .arg(result.error)
                                            .arg(deadline.remainingTime());
                if (!isTransientRpcError(result.error)) {
                    return {.success = false, .errorPresent = false, .error = result.error};
                }
                continue;
            }
            unresolvedErrors.remove(index);
            if (result.errorPresent) {
                qWarning() << "EVerest EVSE error observed: phase=error_present; evse=" << index;
                return {.success = true, .errorPresent = true, .error = {}};
            }
        }
        waitForNextPoll(deadline, kEverestErrorPresentMonitorPollIntervalMs);
    }
    if (!unresolvedErrors.isEmpty()) {
        qWarning() << "EVerest RPC monitor incomplete: phase=error_present; evse_errors=" << unresolvedErrors;
        return {.success = false, .errorPresent = false, .error = unresolvedErrors.constBegin().value()};
    }
    return {.success = false, .errorPresent = false, .error = QLatin1String(kInfoEverestErrorPresentNotDetected)};
}

EverestServiceControlResult executeEverestRestart(RpcApiClient* rpcApiClient, const QList<int>& evseIndices) {
    const quint64 previousHandshakeGeneration = rpcApiClient ? rpcApiClient->handshakeGeneration() : 0;
    const quint64 previousDisconnectionGeneration = rpcApiClient ? rpcApiClient->disconnectionGeneration() : 0;
    const bool requirePostRestartDisconnect = rpcApiClient && rpcApiClient->isReady();
    const RpcApiReadyTimeout timeout = rpcApiReadyTimeout();
    SystemdService systemdService;
    if (!systemdService.restartUnit(QStringLiteral("everest.service"))) {
        return {.success = false, .error = QStringLiteral("everest_restart_failed")};
    }

    QElapsedTimer elapsed;
    elapsed.start();
#ifdef EVEREST_UI_RPC_DEBUG
    qInfo().noquote() << QStringLiteral(
                             "EVerest restart accepted: phase=rpc_readiness; timeout_ms=%1; timeout_source=%2; "
                             "generation_before=%3; disconnects_before=%4")
                             .arg(timeout.milliseconds)
                             .arg(timeout.source)
                             .arg(previousHandshakeGeneration)
                             .arg(previousDisconnectionGeneration);
#endif
    const auto result = waitForRpcApiReady(
        rpcApiClient, previousHandshakeGeneration, previousDisconnectionGeneration, requirePostRestartDisconnect,
        qMax(0, timeout.milliseconds - static_cast<int>(elapsed.elapsed())), evseIndices);
    if (!result.success) {
        qWarning().noquote() << QStringLiteral("EVerest restart readiness failed: phase=rpc_readiness; error=%1; "
                                               "elapsed_ms=%2; timeout_ms=%3; timeout_source=%4; generation_after=%5")
                                    .arg(result.error)
                                    .arg(elapsed.elapsed())
                                    .arg(timeout.milliseconds)
                                    .arg(timeout.source)
                                    .arg(rpcApiClient ? rpcApiClient->handshakeGeneration() : 0);
    }
#ifdef EVEREST_UI_RPC_DEBUG
    else {
        qInfo().noquote() << QStringLiteral(
                                 "EVerest restart readiness completed: phase=rpc_readiness; success=%1; "
                                 "error=%2; elapsed_ms=%3; timeout_ms=%4; timeout_source=%5; generation_after=%6")
                                 .arg(result.success)
                                 .arg(result.error)
                                 .arg(elapsed.elapsed())
                                 .arg(timeout.milliseconds)
                                 .arg(timeout.source)
                                 .arg(rpcApiClient ? rpcApiClient->handshakeGeneration() : 0);
    }
#endif
    return result;
}

EverestServiceControlResult executeEverestStop() {
    SystemdService systemdService;
    if (!systemdService.stopUnit(QStringLiteral("everest.service"))) {
        return EverestServiceControlResult{
            .success = false,
            .error = QStringLiteral("everest_stop_failed"),
        };
    }

    return waitForEverestServiceState(false);
}

EverestServiceControlResult waitForEverestServiceState(bool shouldBeActive) {
    SystemdService systemdService;
    bool reachedRequestedState = false;
    bool waitTimedOut = false;
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;

    pollTimer.setInterval(kEverestRestartPollIntervalMs);
    pollTimer.setSingleShot(false);
    timeoutTimer.setInterval(kEverestRestartWaitTimeoutMs);
    timeoutTimer.setSingleShot(true);

    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        if (systemdService.isUnitActive(QStringLiteral("everest.service")) != shouldBeActive) {
            return;
        }

        reachedRequestedState = true;
        waitLoop.quit();
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &waitLoop, [&]() {
        waitTimedOut = true;
        waitLoop.quit();
    });

    pollTimer.start();
    timeoutTimer.start();
    waitLoop.exec();

    pollTimer.stop();
    timeoutTimer.stop();

    if (waitTimedOut || !reachedRequestedState) {
        return EverestServiceControlResult{
            .success = false,
            .error =
                shouldBeActive ? QStringLiteral("everest_restart_timeout") : QStringLiteral("everest_stop_timeout"),
        };
    }

    return EverestServiceControlResult{
        .success = true,
        .error = QString(),
    };
}

EverestServiceControlResult waitForRpcApiReady(RpcApiClient* rpcApiClient, quint64 previousHandshakeGeneration,
                                               quint64 previousDisconnectionGeneration,
                                               bool requirePostRestartDisconnect, int timeoutMs,
                                               const QList<int>& evseIndices) {
    if (!rpcApiClient) {
        return {.success = false, .error = QStringLiteral("rpc_api_not_configured")};
    }
    const QDeadlineTimer deadline(timeoutMs, Qt::PreciseTimer);
    QString lastError = QStringLiteral("rpc_api_not_connected");
    QString phase = QStringLiteral("hello_handshake");
    bool errorPresent = false;
    while (!deadline.hasExpired()) {
        const bool fresh = rpcApiClient->isReady() &&
                           rpcApiClient->handshakeGeneration() > previousHandshakeGeneration &&
                           (!requirePostRestartDisconnect ||
                            rpcApiClient->disconnectionGeneration() > previousDisconnectionGeneration);
        if (fresh) {
            phase = QStringLiteral("status_readiness");
            const quint64 generation = rpcApiClient->handshakeGeneration();
            bool allResponsive = true;
            for (const int index : evseIndices) {
                if (deadline.hasExpired()) {
                    allResponsive = false;
                    break;
                }
                const auto result =
                    rpcApiClient->getEvseErrorPresent(index, static_cast<int>(deadline.remainingTime()));
                if (deadline.hasExpired() || !rpcApiClient->isReady() ||
                    rpcApiClient->handshakeGeneration() != generation) {
                    lastError = QStringLiteral("rpc_api_no_response");
                    allResponsive = false;
                    break;
                }
                if (!result.success) {
                    lastError = result.error;
                    allResponsive = false;
                    qWarning().noquote() << QStringLiteral("EVerest RPC check failed: phase=status_readiness; evse=%1; "
                                                           "error=%2; remaining_ms=%3; generation=%4")
                                                .arg(index)
                                                .arg(result.error)
                                                .arg(deadline.remainingTime())
                                                .arg(generation);
                    if (!isTransientRpcError(result.error)) {
                        return {.success = false, .error = result.error};
                    }
                } else {
                    if (result.errorPresent) {
                        qWarning() << "EVerest EVSE error observed: phase=status_readiness; evse=" << index;
                    }
                    errorPresent = errorPresent || result.errorPresent;
                }
            }
            if (allResponsive && !deadline.hasExpired()) {
#ifdef EVEREST_UI_RPC_DEBUG
                qInfo().noquote() << QStringLiteral(
                                         "EVerest RPC ready: phase=%1; elapsed_ms=%2; remaining_ms=%3; generation=%4")
                                         .arg(evseIndices.isEmpty() ? QStringLiteral("hello_handshake") : phase)
                                         .arg(timeoutMs - deadline.remainingTime())
                                         .arg(deadline.remainingTime())
                                         .arg(generation);
#endif
                return {.success = true, .error = {}, .errorPresent = errorPresent};
            }
        }
        waitForNextPoll(deadline, kEverestRestartPollIntervalMs);
    }
    qWarning().noquote() << QStringLiteral("EVerest RPC readiness timed out: phase=%1; error=%2; elapsed_ms=%3; "
                                           "timeout_ms=%4; generation_before=%5; generation_after=%6; "
                                           "disconnects_before=%7; disconnects_after=%8")
                                .arg(phase, lastError)
                                .arg(timeoutMs - deadline.remainingTime())
                                .arg(timeoutMs)
                                .arg(previousHandshakeGeneration)
                                .arg(rpcApiClient->handshakeGeneration())
                                .arg(previousDisconnectionGeneration)
                                .arg(rpcApiClient->disconnectionGeneration());
    return {.success = false, .error = lastError};
}
} // namespace EverestServiceControl
