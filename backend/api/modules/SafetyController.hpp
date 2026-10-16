// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SAFETY_CONTROLLER_HPP
#define SAFETY_CONTROLLER_HPP

#include "RequestResponseTypes.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

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
class QObject;

namespace SafetyController {
    void setRpcApiClient(RpcApiClient *rpcApiClient);
    ModuleResponse handleRequest(const ModuleRequest &request);
    ModuleResponse startWriteRequest(const ModuleRequest &request, QObject *owner,
                                     std::function<void(const ModuleResponse &)> completed);
}

#endif // SAFETY_CONTROLLER_HPP
