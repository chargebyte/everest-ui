// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "EverestServiceControl.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"
#include "SystemdService.hpp"

#include <QEventLoop>
#include <QElapsedTimer>
#include <QTimer>
#include <QtGlobal>

#include <limits>

namespace {
constexpr int kEverestRestartWaitTimeoutMs = 10000;
constexpr int kEverestRestartPollIntervalMs = 200;
constexpr int kDefaultRpcApiReadyTimeoutSeconds = 15;
constexpr int kMaxRpcApiReadyTimeoutSeconds = std::numeric_limits<int>::max() / 1000;
constexpr int kEverestErrorPresentMonitorTimeoutMs = 10000;
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

EverestErrorPresentResult monitorEverestErrorPresent(RpcApiClient* rpcApiClient, const QList<int>& evseIndices) {
    if (!rpcApiClient) {
        return EverestErrorPresentResult{
            .success = false,
            .errorPresent = false,
            .error = QStringLiteral("rpc_api_client_unavailable"),
        };
    }

    if (evseIndices.isEmpty()) {
        return EverestErrorPresentResult{
            .success = false,
            .errorPresent = false,
            .error = QStringLiteral("safety_controller_evse_mapping_unavailable"),
        };
    }

    bool errorPresentDetected = false;
    bool waitTimedOut = false;
    QString rpcError;
    QString transientRpcError;
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;

    pollTimer.setInterval(kEverestErrorPresentMonitorPollIntervalMs);
    pollTimer.setSingleShot(false);
    timeoutTimer.setInterval(kEverestErrorPresentMonitorTimeoutMs);
    timeoutTimer.setSingleShot(true);

    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        for (const int evseIndex : evseIndices) {
            const RpcApiEvseErrorPresentResult errorPresentResult = rpcApiClient->getEvseErrorPresent(evseIndex);
            if (!errorPresentResult.success) {
                if (errorPresentResult.error == QLatin1String("rpc_api_not_connected") ||
                    errorPresentResult.error == QLatin1String("rpc_api_request_pending")) {
                    transientRpcError = errorPresentResult.error;
                    return;
                }
                rpcError = errorPresentResult.error;
                waitLoop.quit();
                return;
            }
            transientRpcError.clear();
            if (errorPresentResult.errorPresent) {
                errorPresentDetected = true;
                waitLoop.quit();
                return;
            }
        }
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

    if (!rpcError.isEmpty()) {
        return EverestErrorPresentResult{
            .success = false,
            .errorPresent = false,
            .error = rpcError,
        };
    }

    if (waitTimedOut && !transientRpcError.isEmpty()) {
        return EverestErrorPresentResult{
            .success = false,
            .errorPresent = false,
            .error = transientRpcError,
        };
    }

    if (errorPresentDetected) {
        return EverestErrorPresentResult{
            .success = true,
            .errorPresent = true,
            .error = QString(),
        };
    }

    Q_UNUSED(waitTimedOut);

    return EverestErrorPresentResult{
        .success = false,
        .errorPresent = false,
        .error = QLatin1String(kInfoEverestErrorPresentNotDetected),
    };
}

EverestServiceControlResult executeEverestRestart(RpcApiClient* rpcApiClient) {
    const quint64 previousHandshakeGeneration = rpcApiClient ? rpcApiClient->handshakeGeneration() : 0;
    const quint64 previousDisconnectionGeneration = rpcApiClient ? rpcApiClient->disconnectionGeneration() : 0;
    const bool requirePostRestartDisconnect = rpcApiClient && rpcApiClient->isReady();
    const RpcApiReadyTimeout timeout = rpcApiReadyTimeout();
    SystemdService systemdService;
    if (!systemdService.restartUnit(QStringLiteral("everest.service"))) {
        return EverestServiceControlResult{
            .success = false,
            .error = QStringLiteral("everest_restart_failed"),
        };
    }

    QElapsedTimer restartElapsedTimer;
    restartElapsedTimer.start();
    const EverestServiceControlResult rpcReadyResult =
        waitForRpcApiReady(rpcApiClient, previousHandshakeGeneration, previousDisconnectionGeneration,
                           requirePostRestartDisconnect, timeout.milliseconds);
    if (!rpcReadyResult.success) {
        qWarning().noquote() << QStringLiteral("EVerest RPC readiness failed after restart: phase=hello_handshake; "
                                               "timeout_ms=%1; timeout_source=%2; generation_before=%3; "
                                               "generation_after=%4")
                                    .arg(timeout.milliseconds)
                                    .arg(timeout.source)
                                    .arg(previousHandshakeGeneration)
                                    .arg(rpcApiClient ? rpcApiClient->handshakeGeneration() : 0);
        return rpcReadyResult;
    }
    qInfo().noquote() << QStringLiteral("EVerest RPC ready after restart: phase=hello_handshake; elapsed_ms=%1; "
                                         "timeout_ms=%2; timeout_source=%3")
                              .arg(restartElapsedTimer.elapsed())
                              .arg(timeout.milliseconds)
                              .arg(timeout.source);

    return EverestServiceControlResult{
        .success = true,
        .error = QString(),
    };
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
                                               bool requirePostRestartDisconnect, int timeoutMs) {
    if (!rpcApiClient) {
        return EverestServiceControlResult{
            .success = false,
            .error = QStringLiteral("rpc_api_not_configured"),
        };
    }

    bool rpcApiReady = false;
    bool waitTimedOut = false;
    QElapsedTimer elapsedTimer;
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;

    pollTimer.setInterval(kEverestRestartPollIntervalMs);
    pollTimer.setSingleShot(false);
    timeoutTimer.setInterval(timeoutMs);
    timeoutTimer.setSingleShot(true);

    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        if (!rpcApiClient->isReady() || rpcApiClient->handshakeGeneration() <= previousHandshakeGeneration) {
            return;
        }
        if (requirePostRestartDisconnect &&
            rpcApiClient->disconnectionGeneration() <= previousDisconnectionGeneration) {
            return;
        }

        rpcApiReady = true;
        waitLoop.quit();
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &waitLoop, [&]() {
        waitTimedOut = true;
        waitLoop.quit();
    });

    pollTimer.start();
    elapsedTimer.start();
    timeoutTimer.start();
    waitLoop.exec();

    pollTimer.stop();
    timeoutTimer.stop();

    if (waitTimedOut || !rpcApiReady) {
        qWarning().noquote() << QStringLiteral("Timed out waiting for a fresh EVerest RPC handshake: "
                                               "elapsed_ms=%1; timeout_ms=%2; generation_before=%3; "
                                               "generation_after=%4; disconnects_before=%5; disconnects_after=%6")
                                    .arg(elapsedTimer.elapsed())
                                    .arg(timeoutMs)
                                    .arg(previousHandshakeGeneration)
                                    .arg(rpcApiClient->handshakeGeneration())
                                    .arg(previousDisconnectionGeneration)
                                    .arg(rpcApiClient->disconnectionGeneration());
        return EverestServiceControlResult{
            .success = false,
            .error = QStringLiteral("rpc_api_not_connected"),
        };
    }

    return EverestServiceControlResult{
        .success = true,
        .error = QString(),
    };
}
} // namespace EverestServiceControl
