// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef EVEREST_CONFIG_HPP
#define EVEREST_CONFIG_HPP

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

struct EverestStateAllowedResult {
    bool success = false;
    QString state;
    QString error;
};

class RpcApiClient;

namespace EverestConfig {
void setRpcApiClient(RpcApiClient *rpcApiClient);
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleWriteRequest(const ModuleRequest &request);
ModuleResponse handleDownloadRequest(const ModuleRequest &request);
ModuleResponse handleUploadRequest(const ModuleRequest &request);
ModuleResponse ensureEverestBaseConfig(ModuleResponse response);
ModuleResponse ensureEverestConfigOverlay(const ModuleRequest &request, ModuleResponse response);
ModuleResponse ensureEverestConfigSymlink(ModuleResponse response);
ModuleResponse restartEverestStack(ModuleResponse response);
ModuleResponse executeEverestRestart(ModuleResponse response);
ModuleResponse waitForEverestServiceActive(ModuleResponse response);
ModuleResponse waitForRpcApiReady(ModuleResponse response);
EverestStateAllowedResult checkEverestStateAllowed(int evseIndex);

ConfigPathResult loadEverestConfigPath(const QString &configKey);
QString loadBackendConfigValue(const QString &configKey);
bool copyContent(const QString &sourcePath, const QString &targetPath);
bool contentIdentical(const QString &firstPath, const QString &secondPath);
QJsonObject buildEverestConfigOverlayObject(const QJsonObject &requestParameters,
                                            const QJsonObject &baseYamlRoot);
QString resolveActiveModuleKey(const QString &moduleName, const QJsonObject &baseYamlRoot);
QString generateActiveModuleKey(const QString &moduleName,
                                const QJsonObject &activeModulesObject);
bool writeEverestConfigOverlay(const QString &overlayPath, const QJsonObject &overlayObject);
QJsonObject fillRequestedReadParameters(const QJsonObject &requestParameters,
                                        const QJsonObject &yamlRoot);
}

#endif // EVEREST_CONFIG_HPP
