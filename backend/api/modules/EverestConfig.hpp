// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef EVEREST_CONFIG_HPP
#define EVEREST_CONFIG_HPP

#include "EverestServiceControl.hpp"
#include "RequestResponseTypes.hpp"
#include "YamlUtils.hpp"

#include <QJsonObject>
#include <QString>

enum class EverestAction {
    ReadConfigParameters,
    WriteConfigParameters,
    DownloadConfig,
    UploadConfig,
    Unknown
};

struct ConfigPathResult {
    bool success = false;
    QString path;
    QString error;
};

class RpcApiClient;

namespace EverestConfig {
    void setRpcApiClient(RpcApiClient *rpcApiClient);
    ModuleResponse handleRequest(const ModuleRequest &request);
}

#endif // EVEREST_CONFIG_HPP
