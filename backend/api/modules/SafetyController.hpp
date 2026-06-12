// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SAFETY_CONTROLLER_HPP
#define SAFETY_CONTROLLER_HPP

#include "RequestResponseTypes.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

enum class SafetyControllerAction {
    ReadSettings,
    WriteSettings,
    Unknown
};

struct SafetyControllerConfigPathResult {
    bool success = false;
    QString path;
    QString error;
};

class RpcApiClient;

namespace SafetyController {
    void setRpcApiClient(RpcApiClient *rpcApiClient);
    ModuleResponse handleRequest(const ModuleRequest &request);
}

#endif // SAFETY_CONTROLLER_HPP
