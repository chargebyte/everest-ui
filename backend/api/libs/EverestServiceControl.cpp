// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "EverestServiceControl.hpp"

#include "BackendConfig.hpp"
#include "RpcApiClient.hpp"
#include "SystemdService.hpp"

#include <QEventLoop>
#include <QTimer>

namespace {
constexpr int kEverestRestartWaitTimeoutMs = 10000;
constexpr int kEverestRestartPollIntervalMs = 200;
}

namespace EverestServiceControl {
EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient *rpcApiClient, int evseIndex) {
    if (!rpcApiClient) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("rpc_api_client_unavailable"),
        };
    }

    const QString whitelistValue =
        readBackendConfigValue(QStringLiteral("everest_restart_allowed_states"));
    if (whitelistValue.isEmpty()) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("everest_restart_allowed_states_missing"),
        };
    }

    const RpcApiEvseStateResult evseStateResult = rpcApiClient->getEvseState(evseIndex);
    if (!evseStateResult.success) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = evseStateResult.error,
        };
    }

    QStringList allowedStates = whitelistValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &allowedState : allowedStates) {
        allowedState = allowedState.trimmed();
    }

    if (!allowedStates.contains(evseStateResult.state)) {
        return EverestStateAllowedResult{
            .success = false,
            .state = evseStateResult.state,
            .error = QStringLiteral("everest_state_not_allowed"),
        };
    }

    return EverestStateAllowedResult{
        .success = true,
        .state = evseStateResult.state,
        .error = QString(),
    };
}

EverestServiceControlResult executeEverestRestart(RpcApiClient *rpcApiClient) {
    SystemdService systemdService;
    if (!systemdService.restartUnit(QStringLiteral("everest.service"))) {
        return EverestServiceControlResult{
            .success = false,
            .error = QStringLiteral("everest_restart_failed"),
        };
    }

    const EverestServiceControlResult serviceWaitResult = waitForEverestServiceState(true);
    if (!serviceWaitResult.success) {
        return serviceWaitResult;
    }

    const EverestServiceControlResult rpcReadyResult = waitForRpcApiReady(rpcApiClient);
    if (!rpcReadyResult.success) {
        return rpcReadyResult;
    }

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
            .error = shouldBeActive
                         ? QStringLiteral("everest_restart_timeout")
                         : QStringLiteral("everest_stop_timeout"),
        };
    }

    return EverestServiceControlResult{
        .success = true,
        .error = QString(),
    };
}

EverestServiceControlResult waitForRpcApiReady(RpcApiClient *rpcApiClient) {
    if (!rpcApiClient) {
        return EverestServiceControlResult{
            .success = false,
            .error = QStringLiteral("rpc_api_not_configured"),
        };
    }

    bool rpcApiReady = false;
    bool waitTimedOut = false;
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;

    pollTimer.setInterval(kEverestRestartPollIntervalMs);
    pollTimer.setSingleShot(false);
    timeoutTimer.setInterval(kEverestRestartWaitTimeoutMs);
    timeoutTimer.setSingleShot(true);

    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        if (!rpcApiClient->isReady()) {
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
    timeoutTimer.start();
    waitLoop.exec();

    pollTimer.stop();
    timeoutTimer.stop();

    if (waitTimedOut || !rpcApiReady) {
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
