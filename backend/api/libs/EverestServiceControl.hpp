// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef EVEREST_SERVICE_CONTROL_HPP
#define EVEREST_SERVICE_CONTROL_HPP

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class RpcApiClient;

struct EverestServiceControlResult {
    bool success = false;
    QString error;
};

struct EverestStateAllowedResult {
    bool success = false;
    QString state;
    QString error;
};

struct EverestErrorPresentResult {
    bool success = false;
    bool errorPresent = false;
    QString error;
};

namespace EverestServiceControl {
EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient *rpcApiClient, int evseIndex);
EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient *rpcApiClient, const QList<int> &evseIndices);
EverestStateAllowedResult checkEverestStateAllowed(RpcApiClient *rpcApiClient, const QList<int> &evseIndices,
                                                   const QStringList &allowedStates);
EverestErrorPresentResult monitorEverestErrorPresent(RpcApiClient *rpcApiClient, int evseIndex);
EverestErrorPresentResult monitorEverestErrorPresent(RpcApiClient *rpcApiClient, const QList<int> &evseIndices);
EverestServiceControlResult executeEverestRestart(RpcApiClient *rpcApiClient);
EverestServiceControlResult executeEverestStop();
EverestServiceControlResult waitForEverestServiceState(bool shouldBeActive);
EverestServiceControlResult waitForRpcApiReady(RpcApiClient *rpcApiClient, quint64 previousHandshakeGeneration,
                                               quint64 previousDisconnectionGeneration,
                                               bool requirePostRestartDisconnect, int timeoutMs);
}

#endif // EVEREST_SERVICE_CONTROL_HPP
