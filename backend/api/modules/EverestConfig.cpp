// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "EverestConfig.hpp"

#include "BackendConfig.hpp"
#include "EverestServiceControl.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"
#include "SystemdService.hpp"

#include <QEventLoop>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>

#include <stdexcept>

namespace EverestConfig {
namespace {
RpcApiClient *g_rpcApiClient = nullptr;
constexpr char kConfigYamlKey[] = "config_yaml";
constexpr char kConfigFileName[] = "config.yaml";
constexpr char kErrorConfigReadFailed[] = "everest_config_read_failed";
constexpr char kErrorConfigWriteFailed[] = "everest_config_upload_write_failed";
constexpr char kErrorEverestConfigValidationFailed[] = "everest_config_validation_failed";
constexpr char kConfEverestBaseConfPath[] = "everest_base_config_path";
constexpr char kConfEverestOverlayConfPath[] = "everest_config_overlay_path";
constexpr char kErrorEverestBaseConfigCopyFailed[] = "everest_base_config_copy_failed";
constexpr char kErrorEverestBaseConfigVerificationFailed[] = "everest_base_config_verification_failed";
constexpr char kErrorEverestConfOverlayWriteFailed[] = "everest_config_overlay_write_failed";
constexpr char kErrorEverestConfSymlinkCreateFailed[] = "everest_config_symlink_create_failed";
constexpr char kErrorEverestConfSymlinkVerificationFailed[] = "everest_config_symlink_verification_failed";
constexpr char kAvailableModules[] = "_available_modules";
constexpr char kErrorEverestRequiredModuleMissing[] = "everest_required_module_missing";
constexpr char kModuleEvseManager[] = "EvseManager";
constexpr char kUploadedConfigFilePrefix[] = "everest-ui-uploaded-";

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
    const QJsonObject activeModules = yamlRoot.value(QLatin1String(kEverestConfActiveModules)).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        if (activeModule.value(QLatin1String(kEverestConfModule)).toString() != moduleName) {
            continue;
        }

        return activeModule.value(QLatin1String(kEverestConfConfigModule)).toObject();
    }

    return QJsonObject{};
}

QStringList findAvailableModules(const QJsonObject &yamlRoot) {
    QStringList availableModules;
    const QJsonObject activeModules = yamlRoot.value(QLatin1String(kEverestConfActiveModules)).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        const QString moduleName = activeModule.value(QLatin1String(kEverestConfModule)).toString();
        if (moduleName.isEmpty() || availableModules.contains(moduleName)) {
            continue;
        }

        availableModules.append(moduleName);
    }

    return availableModules;
}

QJsonArray availableModulesToJsonArray(const QStringList &availableModules) {
    QJsonArray modules;
    for (const QString &moduleName : availableModules) {
        modules.append(moduleName);
    }

    return modules;
}
} // namespace

void setRpcApiClient(RpcApiClient *rpcApiClient) {
    g_rpcApiClient = rpcApiClient;
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

bool readTextFile(const QString &path, QString &content) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    content = QString::fromUtf8(file.readAll());
    return true;
}

bool writeTextFile(const QString &path, const QString &content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    const QByteArray data = content.toUtf8();
    return file.write(data) == data.size();
}

QString sanitizeUploadedConfigFileName(const QString &rawFileName) {
    const QString fileName = QFileInfo(rawFileName.trimmed()).fileName().trimmed();
    if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")) {
        return QString();
    }

    return fileName;
}

QString validateYamlText(const QString &content) {
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        return QLatin1String(kErrorEverestConfigValidationFailed);
    }

    const QByteArray data = content.toUtf8();
    if (tempFile.write(data) != data.size() || !tempFile.flush()) {
        return QLatin1String(kErrorEverestConfigValidationFailed);
    }

    const YamlLoadResult yamlLoadResult = loadYamlFile(tempFile.fileName());
    return yamlLoadResult.success ? QString() : yamlLoadResult.error;
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
        .error = configKey + QLatin1String(kErrorMissing),
    };
}

QString buildUploadedConfigTargetPath(const QString &baseConfigPath,
                                      const QString &configPath,
                                      const QString &uploadedFileName) {
    if (baseConfigPath.trimmed().isEmpty() ||
        configPath.trimmed().isEmpty() ||
        uploadedFileName.trimmed().isEmpty()) {
        return QString();
    }

    const QFileInfo baseConfigInfo(baseConfigPath);
    const QDir baseConfigDirectory = baseConfigInfo.dir();
    const QString safeFileName =
        QString::fromLatin1(kUploadedConfigFilePrefix) + uploadedFileName.trimmed();
    const QString targetPath = baseConfigDirectory.filePath(safeFileName);
    const QString cleanedTargetPath = QDir::cleanPath(targetPath);
    const QString cleanedConfigPath = QDir::cleanPath(configPath);

    if (cleanedTargetPath == cleanedConfigPath) {
        return QString();
    }

    return cleanedTargetPath;
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

ModuleResponse ensureEverestBaseConfig(ModuleResponse response) {
    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestBaseConfPath));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), baseConfigPathResult.error},
        };
        return response;
    }

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestConfPath));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), configPathResult.error},
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
            {QLatin1String(kError), QLatin1String(kErrorEverestBaseConfigCopyFailed)},
        };
        return response;
    }

    if (!contentIdentical(configPathResult.path, baseConfigPathResult.path)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorEverestBaseConfigVerificationFailed)},
        };
        return response;
    }

    return response;
}

QString resolveActiveModuleKey(const QString &moduleName, const QJsonObject &baseYamlRoot) {
    const QJsonObject activeModules = baseYamlRoot.value(QLatin1String(kEverestConfActiveModules)).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        if (activeModule.value(QLatin1String(kEverestConfModule)).toString() == moduleName) {
            return activeModuleKey;
        }
    }

    return QString();
}

QJsonObject buildEverestConfigOverlayObject(const QJsonObject &requestParameters,
                                            const QJsonObject &baseYamlRoot) {
    QJsonObject overlayActiveModules;
    const auto requestedModuleNames = requestParameters.keys();
    for (const QString &requestedModuleName : requestedModuleNames) {
        const QString activeModuleKey =
            resolveActiveModuleKey(requestedModuleName, baseYamlRoot);
        if (activeModuleKey.isEmpty()) {
            continue;
        }

        QJsonObject configModule;
        const QJsonObject requestedConfigModule =
            requestParameters.value(requestedModuleName).toObject();
        const auto configKeys = requestedConfigModule.keys();
        for (const QString &configKey : configKeys) {
            const QJsonValue value = requestedConfigModule.value(configKey);
            if (value.isString() && value.toString().trimmed().isEmpty()) {
                continue;
            }
            configModule.insert(configKey, value);
        }
        if (configModule.isEmpty()) {
            continue;
        }

        overlayActiveModules.insert(activeModuleKey, QJsonObject{
                                                       {QLatin1String(kEverestConfModule), requestedModuleName},
                                                       {QLatin1String(kEverestConfConfigModule),
                                                        configModule},
                                                   });
    }

    return QJsonObject{
        {QLatin1String(kEverestConfActiveModules), overlayActiveModules},
    };
}

bool writeEverestConfigOverlay(const QString &overlayPath, const QJsonObject &overlayObject) {
    QFile overlayFile(overlayPath);
    if (!overlayFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    // This is implemented in plain C++/Qt for the time being.
    // For more complex configurations, a proper YAML emitter is required.
    QTextStream stream(&overlayFile);
    const QJsonObject activeModules = overlayObject.value(QLatin1String(kEverestConfActiveModules)).toObject();
    if (activeModules.isEmpty()) {
        stream << QLatin1String(kEverestConfActiveModules) << ": {}\n";
        return stream.status() == QTextStream::Ok;
    }

    stream << QLatin1String(kEverestConfActiveModules) << ":\n";

    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        stream << "  " << activeModuleKey << ":\n";
        stream << "    " << QLatin1String(kEverestConfModule) << ": "
               << formatYamlScalar(activeModule.value(QLatin1String(kEverestConfModule))) << "\n";
        stream << "    " << QLatin1String(kEverestConfConfigModule) << ":\n";

        const QJsonObject configModule = activeModule.value(QLatin1String(kEverestConfConfigModule)).toObject();
        const auto configKeys = configModule.keys();
        for (const QString &configKey : configKeys) {
            stream << "      " << configKey << ": "
                   << formatYamlScalar(configModule.value(configKey)) << "\n";
        }
    }

    return stream.status() == QTextStream::Ok;
}

ModuleResponse ensureEverestConfigOverlay(const ModuleRequest &request, ModuleResponse response) {
    const ConfigPathResult overlayConfigPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestOverlayConfPath));
    if (!overlayConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), overlayConfigPathResult.error},
        };
        return response;
    }

    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestBaseConfPath));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), baseConfigPathResult.error},
        };
        return response;
    }

    const YamlLoadResult baseYamlLoadResult = loadYamlFile(baseConfigPathResult.path);
    if (!baseYamlLoadResult.success) {
        qWarning() << "Unable to load EVerest base configuration"
                   << baseConfigPathResult.path << ":" << baseYamlLoadResult.error;
        response.parameters = QJsonObject{
            {QLatin1String(kError), baseYamlLoadResult.error},
        };
        return response;
    }

    const QJsonObject overlayObject =
        buildEverestConfigOverlayObject(request.parameters, baseYamlLoadResult.yamlRoot);
    if (!writeEverestConfigOverlay(overlayConfigPathResult.path, overlayObject)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorEverestConfOverlayWriteFailed)},
        };
        return response;
    }

    const YamlLoadResult overlayYamlLoadResult = loadYamlFile(overlayConfigPathResult.path);
    if (!overlayYamlLoadResult.success) {
        qWarning() << "Unable to validate EVerest configuration overlay"
                   << overlayConfigPathResult.path << ":" << overlayYamlLoadResult.error;
        response.parameters = QJsonObject{
            {QLatin1String(kError), overlayYamlLoadResult.error},
        };
        return response;
    }

    return response;
}

ModuleResponse ensureEverestConfigSymlinkToTarget(const QString &targetPath, ModuleResponse response) {
    if (targetPath.trimmed().isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorEverestConfSymlinkCreateFailed)},
        };
        return response;
    }

    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestConfPath));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), baseConfigPathResult.error},
        };
        return response;
    }

    QFileInfo configFileInfo(baseConfigPathResult.path);
    if (configFileInfo.exists() || configFileInfo.isSymLink()) {
        if (!QFile::remove(baseConfigPathResult.path)) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorEverestConfSymlinkCreateFailed)},
            };
            return response;
        }
    }

    if (!QFile::link(targetPath, baseConfigPathResult.path)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorEverestConfSymlinkCreateFailed)},
        };
        return response;
    }

    const QFileInfo symlinkInfo(baseConfigPathResult.path);
    if (!symlinkInfo.isSymLink() || symlinkInfo.symLinkTarget() != targetPath) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorEverestConfSymlinkVerificationFailed)},
        };
        return response;
    }

    return response;
}

ModuleResponse ensureEverestConfigSymlink(ModuleResponse response) {
    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestBaseConfPath));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), baseConfigPathResult.error},
        };
        return response;
    }

    return ensureEverestConfigSymlinkToTarget(baseConfigPathResult.path, response);
}

ModuleResponse restartEverestStack(ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        qWarning() << "Unable to check whether EVerest may be restarted"
                   << "state=" << stateAllowedResult.state
                   << "error=" << stateAllowedResult.error;
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QLatin1String(kErrorEverestStateNotAllowed)) {
            error =
                QStringLiteral("config could not be applied because EVerest-stack is in state \"%1\" and cannot be restarted, please unplug the EV and try again")
                    .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QLatin1String(kError), error},
        };
        return response;
    }

    const EverestServiceControlResult restartResult =
        EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), restartResult.error},
        };
        return response;
    }

    response.parameters = QJsonObject{};
    response.success = true;
    return response;
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupEverest),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestConfPath));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), configPathResult.error},
        };
        return response;
    }

    const YamlLoadResult yamlLoadResult = loadYamlFile(configPathResult.path);
    if (!yamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), yamlLoadResult.error},
        };
        return response;
    }

    const QStringList availableModules = findAvailableModules(yamlLoadResult.yamlRoot);
    if (!availableModules.contains(QString::fromLatin1(kModuleEvseManager))) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorEverestRequiredModuleMissing)},
        };
        return response;
    }

    response.parameters =
        fillRequestedReadParameters(request.parameters, yamlLoadResult.yamlRoot);
    response.parameters.insert(
        QLatin1String(kAvailableModules), availableModulesToJsonArray(availableModules));
    response.success = true;
    return response;
}

ModuleResponse handleWriteRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupEverest),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
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
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupEverest),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestConfPath));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), configPathResult.error},
        };
        return response;
    }

    QString configYaml;
    if (!readTextFile(configPathResult.path, configYaml)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QString::fromLatin1(kErrorConfigReadFailed)},
        };
        return response;
    }

    QString downloadFileName = QString::fromLatin1(kConfigFileName);
    const QFileInfo configFileInfo(configPathResult.path);
    if (configFileInfo.isSymLink()) {
        const QFileInfo targetInfo(configFileInfo.symLinkTarget());
        if (!targetInfo.fileName().trimmed().isEmpty()) {
            downloadFileName = targetInfo.fileName();
        }
    } else if (!configFileInfo.fileName().trimmed().isEmpty()) {
        downloadFileName = configFileInfo.fileName();
    }

    response.parameters = QJsonObject{
        {QString::fromLatin1(kKeyFile), downloadFileName},
        {QString::fromLatin1(kConfigYamlKey), configYaml},
    };
    response.success = true;
    return response;
}

ModuleResponse handleUploadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupEverest),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const QString configYaml =
        request.parameters.value(QString::fromLatin1(kConfigYamlKey)).toString();
    if (configYaml.trimmed().isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QString::fromLatin1(kErrorInvalidParams)},
        };
        return response;
    }

    const QString uploadedFileName = sanitizeUploadedConfigFileName(
        request.parameters.value(QString::fromLatin1(kParametersFileName)).toString());
    if (uploadedFileName.isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QString::fromLatin1(kErrorInvalidParams)},
        };
        return response;
    }

    const QString yamlValidationError = validateYamlText(configYaml);
    if (!yamlValidationError.isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), yamlValidationError},
        };
        return response;
    }

    const ConfigPathResult baseConfigPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestBaseConfPath));
    if (!baseConfigPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), baseConfigPathResult.error},
        };
        return response;
    }

    const ConfigPathResult configPathResult =
        loadEverestConfigPath(QLatin1String(kConfEverestConfPath));
    if (!configPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), configPathResult.error},
        };
        return response;
    }

    const QString uploadedConfigPath =
        buildUploadedConfigTargetPath(baseConfigPathResult.path,
                                      configPathResult.path,
                                      uploadedFileName);
    if (uploadedConfigPath.isEmpty() || !writeTextFile(uploadedConfigPath, configYaml)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QString::fromLatin1(kErrorConfigWriteFailed)},
        };
        return response;
    }

    const YamlLoadResult writtenYamlLoadResult = loadYamlFile(uploadedConfigPath);
    if (!writtenYamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), writtenYamlLoadResult.error},
        };
        return response;
    }

    response = ensureEverestConfigSymlinkToTarget(uploadedConfigPath, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    return restartEverestStack(response);
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
} // namespace EverestConfig
