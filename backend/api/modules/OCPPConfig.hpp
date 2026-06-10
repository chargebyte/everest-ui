// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef OCPP_CONFIG_HPP
#define OCPP_CONFIG_HPP

#include "RequestResponseTypes.hpp"

class RpcApiClient;

enum class OCPPAction {
    ReadSettings,
    WriteSettings,
    Unknown
};

struct OcppBaseDirResult {
    bool success = false;
    QString path;
    QString error;
};

struct OcppJsonLoadResult {
    bool success = false;
    QJsonObject rootObject;
    QString error;
};

struct OcppReadValueResult {
    bool success = false;
    bool found = false;
    QJsonValue value;
    QString error;
};

struct OcppFillResult {
    bool success = false;
    QJsonObject parameters;
    QString error;
};

namespace OCPPConfig {
void setRpcApiClient(RpcApiClient *rpcApiClient);
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleWriteRequest(const ModuleRequest &request);
}

#endif // OCPP_CONFIG_HPP
