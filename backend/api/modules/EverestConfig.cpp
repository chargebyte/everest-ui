// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "EverestConfig.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"
#include "SystemdService.hpp"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <stdexcept>

namespace EverestConfig {
namespace {
RpcApiClient *g_rpcApiClient = nullptr;
constexpr int kEverestRestartWaitTimeoutMs = 10000;
constexpr int kEverestRestartPollIntervalMs = 200;
constexpr int kEverestSilFixedWaitMs = 5000;

EverestAction toEverestAction(const QString &action) {
    if (action == QLatin1String(kActionReadConfigParameters)) {
        return EverestAction::ReadConfigParameters;
    }
    if (action == QLatin1String(kActionWriteConfigParameters)) {
        return EverestAction::WriteConfigParameters;
    }
    if (action == QLatin1String(kActionDownloadConfig)) {
        return EverestAction::DownloadConfig;
    }
    if (action == QLatin1String(kActionUploadConfig)) {
        return EverestAction::UploadConfig;
    }

    return EverestAction::Unknown;
}

QJsonObject findActiveModuleConfig(const QJsonObject &yamlRoot, const QString &moduleName) {
    const QJsonObject activeModules = yamlRoot.value(QStringLiteral("active_modules")).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        if (activeModule.value(QStringLiteral("module")).toString() != moduleName) {
            continue;
        }

        return activeModule.value(QStringLiteral("config_module")).toObject();
    }

    return QJsonObject{};
}
} // namespace

void setRpcApiClient(RpcApiClient *rpcApiClient) {
    g_rpcApiClient = rpcApiClient;
}

ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toEverestAction(request.action)) {
    case EverestAction::ReadConfigParameters:
        return handleReadRequest(request);
    case EverestAction::WriteConfigParameters:
        return handleWriteRequest(request);
    case EverestAction::DownloadConfig:
        return handleDownloadRequest(request);
    case EverestAction::UploadConfig:
        return handleUploadRequest(request);
    case EverestAction::Unknown:
        throw std::runtime_error("EverestConfig::handleRequest got unsupported action");
    }

    throw std::runtime_error("EverestConfig::handleRequest reached unreachable code");
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("everest"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QStringLiteral("everest_config_path"));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), configPathResult.error},
        };
        return response;
    }

    const YamlLoadResult yamlLoadResult = loadYamlFile(configPathResult.path);
    if (!yamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), yamlLoadResult.error},
        };
        return response;
    }

    response.parameters =
        fillRequestedReadParameters(request.parameters, yamlLoadResult.yamlRoot);
    response.success = true;
    return response;
}

ModuleResponse handleWriteRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("everest"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    response = ensureEverestBaseConfig(response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    response = ensureEverestConfigOverlay(request, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    response = ensureEverestConfigSymlink(response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    return restartEverestStack(response);
}

ModuleResponse handleDownloadRequest(const ModuleRequest &request) {
    Q_UNUSED(request);
    throw std::runtime_error("EverestConfig::handleDownloadRequest is not implemented yet");
}

ModuleResponse handleUploadRequest(const ModuleRequest &request) {
    Q_UNUSED(request);
    throw std::runtime_error("EverestConfig::handleUploadRequest is not implemented yet");
}

ConfigPathResult loadEverestConfigPath(const QString &configKey) {
    const QString value = loadBackendConfigValue(configKey);
    if (!value.isEmpty()) {
        return ConfigPathResult{
            .success = true,
            .path = value,
            .error = QString(),
        };
    }

    return ConfigPathResult{
        .success = false,
        .path = QString(),
        .error = configKey + QStringLiteral("_missing"),
    };
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

bool copyContent(const QString &sourcePath, const QString &targetPath) {
    if (QFile::exists(targetPath) && !QFile::remove(targetPath)) {
        return false;
    }

    return QFile::copy(sourcePath, targetPath);
}

bool contentIdentical(const QString &firstPath, const QString &secondPath) {
    QFile firstFile(firstPath);
    QFile secondFile(secondPath);
    if (!firstFile.open(QIODevice::ReadOnly) || !secondFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    if (firstFile.size() != secondFile.size()) {
        return false;
    }

    return firstFile.readAll() == secondFile.readAll();
}

QJsonObject buildEverestConfigOverlayObject(const QJsonObject &requestParameters,
                                            const QJsonObject &baseYamlRoot) {
    QJsonObject overlayActiveModules;
    const auto requestedModuleNames = requestParameters.keys();
    for (const QString &requestedModuleName : requestedModuleNames) {
        const QString activeModuleKey =
            resolveActiveModuleKey(requestedModuleName, baseYamlRoot);

        overlayActiveModules.insert(activeModuleKey, QJsonObject{
                                                       {QStringLiteral("module"), requestedModuleName},
                                                       {QStringLiteral("config_module"),
                                                        requestParameters.value(requestedModuleName).toObject()},
                                                   });
    }

    return QJsonObject{
        {QStringLiteral("active_modules"), overlayActiveModules},
    };
}

QString resolveActiveModuleKey(const QString &moduleName, const QJsonObject &baseYamlRoot) {
    const QJsonObject activeModules = baseYamlRoot.value(QStringLiteral("active_modules")).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        if (activeModule.value(QStringLiteral("module")).toString() == moduleName) {
            return activeModuleKey;
        }
    }

    return generateActiveModuleKey(moduleName, activeModules);
}

QString generateActiveModuleKey(const QString &moduleName,
                                const QJsonObject &activeModulesObject) {
    QString sanitizedModuleName = moduleName.toLower();
    sanitizedModuleName.replace(QLatin1Char(' '), QLatin1Char('_'));

    QString baseKey = QStringLiteral("module_");
    for (const QChar character : sanitizedModuleName) {
        if (character.isLetterOrNumber() || character == QLatin1Char('_')) {
            baseKey.append(character);
        }
    }

    if (baseKey == QStringLiteral("module_")) {
        baseKey = QStringLiteral("module_generated");
    }

    QString candidateKey = baseKey;
    int suffix = 2;
    while (activeModulesObject.contains(candidateKey)) {
        candidateKey = baseKey + QLatin1Char('_') + QString::number(suffix);
        suffix += 1;
    }

    return candidateKey;
}

bool writeEverestConfigOverlay(const QString &overlayPath, const QJsonObject &overlayObject) {
    QFile overlayFile(overlayPath);
    if (!overlayFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&overlayFile);
    stream << "active_modules:\n";

    const QJsonObject activeModules = overlayObject.value(QStringLiteral("active_modules")).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        stream << "  " << activeModuleKey << ":\n";
        stream << "    module: " << formatYamlScalar(activeModule.value(QStringLiteral("module"))) << "\n";
        stream << "    config_module:\n";

        const QJsonObject configModule = activeModule.value(QStringLiteral("config_module")).toObject();
        const auto configKeys = configModule.keys();
        for (const QString &configKey : configKeys) {
            stream << "      " << configKey << ": "
                   << formatYamlScalar(configModule.value(configKey)) << "\n";
        }
    }

    return stream.status() == QTextStream::Ok;
}

QString formatYamlScalar(const QJsonValue &value) {
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }

    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("null");
    }

    const QString stringValue = value.toString();
    QString escapedValue = stringValue;
    escapedValue.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escapedValue.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escapedValue.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return QLatin1Char('"') + escapedValue + QLatin1Char('"');
}

ModuleResponse ensureEverestBaseConfig(ModuleResponse response) {
    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QStringLiteral("everest_base_config_path"));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseConfigPathResult.error},
        };
        return response;
    }

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QStringLiteral("everest_config_path"));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), configPathResult.error},
        };
        return response;
    }

    const QFileInfo sourceFileInfo(configPathResult.path);
    const QFileInfo targetFileInfo(baseConfigPathResult.path);
    const QString sourceCanonicalPath = sourceFileInfo.canonicalFilePath();
    const QString targetCanonicalPath = targetFileInfo.canonicalFilePath();
    if (!sourceCanonicalPath.isEmpty() &&
        !targetCanonicalPath.isEmpty() &&
        sourceCanonicalPath == targetCanonicalPath) {
        return response;
    }

    if (!copyContent(configPathResult.path, baseConfigPathResult.path)) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("everest_base_config_copy_failed")},
        };
        return response;
    }

    if (!contentIdentical(configPathResult.path, baseConfigPathResult.path)) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("everest_base_config_verification_failed")},
        };
        return response;
    }

    return response;
}

ModuleResponse ensureEverestConfigOverlay(const ModuleRequest &request, ModuleResponse response) {
    const ConfigPathResult overlayConfigPathResult =
        loadEverestConfigPath(QStringLiteral("everest_config_overlay_path"));
    if (!overlayConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), overlayConfigPathResult.error},
        };
        return response;
    }

    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QStringLiteral("everest_base_config_path"));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseConfigPathResult.error},
        };
        return response;
    }

    const YamlLoadResult baseYamlLoadResult = loadYamlFile(baseConfigPathResult.path);
    if (!baseYamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseYamlLoadResult.error},
        };
        return response;
    }

    const QJsonObject overlayObject =
        buildEverestConfigOverlayObject(request.parameters, baseYamlLoadResult.yamlRoot);
    if (!writeEverestConfigOverlay(overlayConfigPathResult.path, overlayObject)) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("everest_config_overlay_write_failed")},
        };
        return response;
    }

    return response;
}

ModuleResponse ensureEverestConfigSymlink(ModuleResponse response) {
    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QStringLiteral("everest_base_config_path"));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseConfigPathResult.error},
        };
        return response;
    }

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QStringLiteral("everest_config_path"));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), configPathResult.error},
        };
        return response;
    }

    QFileInfo configFileInfo(configPathResult.path);
    if (configFileInfo.exists() || configFileInfo.isSymLink()) {
        if (!QFile::remove(configPathResult.path)) {
            response.parameters = QJsonObject{
                {QStringLiteral("error"), QStringLiteral("everest_config_symlink_create_failed")},
            };
            return response;
        }
    }

    if (!QFile::link(baseConfigPathResult.path, configPathResult.path)) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("everest_config_symlink_create_failed")},
        };
        return response;
    }

    const QFileInfo symlinkInfo(configPathResult.path);
    if (!symlinkInfo.isSymLink() || symlinkInfo.symLinkTarget() != baseConfigPathResult.path) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("everest_config_symlink_verification_failed")},
        };
        return response;
    }

    return response;
}

EverestStateAllowedResult checkEverestStateAllowed(int evseIndex) {
    if (!g_rpcApiClient) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("rpc_api_client_unavailable"),
        };
    }

    const QString whitelistValue =
        loadBackendConfigValue(QStringLiteral("everest_restart_allowed_states"));
    if (whitelistValue.isEmpty()) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("everest_restart_allowed_states_missing"),
        };
    }

    const RpcApiEvseStateResult evseStateResult = g_rpcApiClient->getEvseState(evseIndex);
    if (!evseStateResult.success) {
        return EverestStateAllowedResult{
            .success = false,
            .state = QString(),
            .error = evseStateResult.error,
        };
    }

    QStringList allowedStates =
        whitelistValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &allowedState : allowedStates) {
        allowedState = allowedState.trimmed();
    }

    if (!allowedStates.contains(evseStateResult.state)) {
        return EverestStateAllowedResult{
            .success = false,
            .state = evseStateResult.state,
            .error = QStringLiteral("config could not be applied because EVerest-stack is in state \"%1\" and cannot be restarted, please unplug the EV and try again")
                         .arg(evseStateResult.state),
        };
    }

    return EverestStateAllowedResult{
        .success = true,
        .state = evseStateResult.state,
        .error = QString(),
    };
}

ModuleResponse restartEverestStack(ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult = checkEverestStateAllowed(1);
    if (!stateAllowedResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), stateAllowedResult.error},
        };
        return response;
    }

    return executeEverestRestart(response);
}

ModuleResponse executeEverestRestart(ModuleResponse response) {
    SystemdService systemdService;
    if (!systemdService.restartUnit(QStringLiteral("everest.service"))) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("everest_restart_failed")},
        };
        return response;
    }
    
    bool isUnitActive = false;
    bool waitTimedOut = false;
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;
    
    pollTimer.setInterval(kEverestRestartPollIntervalMs);
    pollTimer.setSingleShot(false);
    timeoutTimer.setInterval(kEverestRestartWaitTimeoutMs);
    timeoutTimer.setSingleShot(true);
    
    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        if (!systemdService.isUnitActive(QStringLiteral("everest.service"))) {
            return;
        }
    
        isUnitActive = true;
        waitLoop.quit();
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &waitLoop, [&]() {
        waitTimedOut = true;
        waitLoop.quit();
    });
    
    pollTimer.start();
    timeoutTimer.start();

    const EverestStateAllowedResult stateAllowedResult = checkEverestStateAllowed(1);
    if (!stateAllowedResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), stateAllowedResult.error},
        };
        return response;
    }

    response.parameters = QJsonObject{};
    response.success = true;
    return response;
}

QJsonObject fillRequestedReadParameters(const QJsonObject &requestParameters,
                                        const QJsonObject &yamlRoot) {
    QJsonObject responseParameters = requestParameters;
    const auto requestedModuleNames = responseParameters.keys();

    for (const QString &requestedModuleName : requestedModuleNames) {
        const QJsonObject activeModuleConfig =
            findActiveModuleConfig(yamlRoot, requestedModuleName);
        
        QJsonObject requestedModuleParameters =
            responseParameters.value(requestedModuleName).toObject();
        const auto requestedParameterNames = requestedModuleParameters.keys();
        
        if (activeModuleConfig.isEmpty()) {
            for (const QString &requestedParameterName : requestedParameterNames) {
                requestedModuleParameters.insert(
                    requestedParameterName, QJsonValue());
            }
            responseParameters.insert(requestedModuleName, requestedModuleParameters);
            continue;
        }

        for (const QString &requestedParameterName : requestedParameterNames) {
            if (!activeModuleConfig.contains(requestedParameterName)) {
                requestedModuleParameters.insert(
                    requestedParameterName, QJsonValue());
                continue;
            }

            requestedModuleParameters.insert(
                requestedParameterName, activeModuleConfig.value(requestedParameterName));
        }

        responseParameters.insert(requestedModuleName, requestedModuleParameters);
    }

    return responseParameters;
}
} // namespace EverestConfig
