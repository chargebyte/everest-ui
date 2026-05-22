// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "OCPPConfig.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <stdexcept>

namespace OCPPConfig {
namespace {
constexpr char kOcppBaseConfigPathKey[] = "ocpp_base_config_path";
constexpr char kOcppCustomConfigPathKey[] = "ocpp_custom_config_path";
constexpr char kNetworkConnectionProfiles[] = "NetworkConnectionProfiles";
constexpr char kPropertiesKey[] = "properties";
constexpr char kAttributesKey[] = "attributes";
constexpr char kValueKey[] = "value";
constexpr char kConfigurationSlotKey[] = "configurationSlot";

constexpr char kOcppBaseConfigMissing[] = "ocpp_base_config_path_missing";
constexpr char kOcppBaseConfigNotFound[] = "ocpp_base_config_path_not_found";
constexpr char kOcppBaseConfigNotDirectory[] = "ocpp_base_config_path_not_a_directory";
constexpr char kOcppReadParseFailed[] = "ocpp_read_parse_failed";
constexpr char kOcppValueNotFound[] = "ocpp_value_not_found";
constexpr char kOcppFileNotFound[] = "ocpp_file_not_found";
constexpr char kOcppWriteNotImplemented[] = "ocpp_write_not_implemented";

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

OCPPAction toOcppAction(const QString &action) {
    if (action == QLatin1String(kActionReadSettings)) {
        return OCPPAction::ReadSettings;
    }
    if (action == QLatin1String(kActionWriteSettings)) {
        return OCPPAction::WriteSettings;
    }

    return OCPPAction::Unknown;
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey).trimmed();
}

OcppBaseDirResult loadOcppBaseConfigPath() {
    const QString path = loadBackendConfigValue(QString::fromLatin1(kOcppBaseConfigPathKey));
    if (path.isEmpty()) {
        return OcppBaseDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kOcppBaseConfigMissing),
        };
    }

    const QFileInfo pathInfo(path);
    if (!pathInfo.exists()) {
        return OcppBaseDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kOcppBaseConfigNotFound),
        };
    }

    if (!pathInfo.isDir()) {
        return OcppBaseDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kOcppBaseConfigNotDirectory),
        };
    }

    return OcppBaseDirResult{
        .success = true,
        .path = QDir::cleanPath(path),
        .error = QString(),
    };
}

QString loadOcppCustomConfigPath() {
    return QDir::cleanPath(loadBackendConfigValue(QString::fromLatin1(kOcppCustomConfigPathKey)));
}

QString buildControllerFilePath(const QString &directoryPath, const QString &controllerName) {
    if (directoryPath.isEmpty() || controllerName.isEmpty()) {
        return QString();
    }

    return QDir(directoryPath).filePath(controllerName + QStringLiteral(".json"));
}

OcppJsonLoadResult loadJsonObjectFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return OcppJsonLoadResult{
            .success = false,
            .rootObject = QJsonObject{},
            .error = QString::fromLatin1(kOcppReadParseFailed),
        };
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return OcppJsonLoadResult{
            .success = false,
            .rootObject = QJsonObject{},
            .error = QString::fromLatin1(kOcppReadParseFailed),
        };
    }

    return OcppJsonLoadResult{
        .success = true,
        .rootObject = document.object(),
        .error = QString(),
    };
}

OcppReadValueResult getAttributeZeroValue(const QJsonObject &propertyObject) {
    const QJsonArray attributesArray =
        propertyObject.value(QString::fromLatin1(kAttributesKey)).toArray();
    if (attributesArray.isEmpty() || !attributesArray.first().isObject()) {
        return OcppReadValueResult{
            .success = true,
            .found = false,
            .value = QJsonValue(),
            .error = QString(),
        };
    }

    const QJsonObject attributeZero = attributesArray.first().toObject();
    if (!attributeZero.contains(QString::fromLatin1(kValueKey))) {
        return OcppReadValueResult{
            .success = true,
            .found = false,
            .value = QJsonValue(),
            .error = QString(),
        };
    }

    return OcppReadValueResult{
        .success = true,
        .found = true,
        .value = attributeZero.value(QString::fromLatin1(kValueKey)),
        .error = QString(),
    };
}

OcppReadValueResult navigateJsonObject(const QJsonObject &jsonObject,
                                       const QStringList &pathSegments) {
    QJsonValue currentValue(jsonObject);
    for (const QString &pathSegment : pathSegments) {
        if (!currentValue.isObject()) {
            return OcppReadValueResult{
                .success = true,
                .found = false,
                .value = QJsonValue(),
                .error = QString(),
            };
        }

        const QJsonObject currentObject = currentValue.toObject();
        if (!currentObject.contains(pathSegment)) {
            return OcppReadValueResult{
                .success = true,
                .found = false,
                .value = QJsonValue(),
                .error = QString(),
            };
        }

        currentValue = currentObject.value(pathSegment);
    }

    return OcppReadValueResult{
        .success = true,
        .found = true,
        .value = currentValue,
        .error = QString(),
    };
}

OcppReadValueResult findConfigurationSlotOneProfile(const QJsonArray &profilesArray) {
    for (const QJsonValue &profileValue : profilesArray) {
        if (!profileValue.isObject()) {
            continue;
        }

        const QJsonObject profileObject = profileValue.toObject();
        if (profileObject.value(QString::fromLatin1(kConfigurationSlotKey)).toInt(-1) == 1) {
            return OcppReadValueResult{
                .success = true,
                .found = true,
                .value = profileObject,
                .error = QString(),
            };
        }
    }

    return OcppReadValueResult{
        .success = true,
        .found = false,
        .value = QJsonValue(),
        .error = QString(),
    };
}

OcppReadValueResult extractStandardPropertyValue(const QJsonObject &controllerRoot,
                                                 const QString &propertyName) {
    const QJsonObject propertiesObject =
        controllerRoot.value(QString::fromLatin1(kPropertiesKey)).toObject();
    if (!propertiesObject.contains(propertyName) || !propertiesObject.value(propertyName).isObject()) {
        return OcppReadValueResult{
            .success = true,
            .found = false,
            .value = QJsonValue(),
            .error = QString(),
        };
    }

    return getAttributeZeroValue(propertiesObject.value(propertyName).toObject());
}

OcppReadValueResult extractNetworkConnectionProfilesValue(const QJsonObject &controllerRoot,
                                                          const QStringList &pathSegments) {
    const OcppReadValueResult rawProfilesValue =
        extractStandardPropertyValue(controllerRoot, QString::fromLatin1(kNetworkConnectionProfiles));
    if (!rawProfilesValue.success || !rawProfilesValue.found || !rawProfilesValue.value.isString()) {
        return rawProfilesValue;
    }

    QJsonParseError parseError;
    const QJsonDocument profilesDocument =
        QJsonDocument::fromJson(rawProfilesValue.value.toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !profilesDocument.isArray()) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kOcppReadParseFailed),
        };
    }

    const OcppReadValueResult slotOneProfile =
        findConfigurationSlotOneProfile(profilesDocument.array());
    if (!slotOneProfile.success || !slotOneProfile.found || !slotOneProfile.value.isObject()) {
        return slotOneProfile;
    }

    return navigateJsonObject(slotOneProfile.value.toObject(), pathSegments);
}

OcppReadValueResult extractValueFromDocument(const QJsonObject &controllerRoot,
                                             const QStringList &pathSegmentsWithoutController) {
    if (pathSegmentsWithoutController.isEmpty()) {
        return OcppReadValueResult{
            .success = true,
            .found = false,
            .value = QJsonValue(),
            .error = QString(),
        };
    }

    if (pathSegmentsWithoutController.first() == QLatin1String(kNetworkConnectionProfiles)) {
        return extractNetworkConnectionProfilesValue(
            controllerRoot,
            pathSegmentsWithoutController.mid(1));
    }

    if (pathSegmentsWithoutController.size() == 1) {
        return extractStandardPropertyValue(controllerRoot, pathSegmentsWithoutController.first());
    }

    return OcppReadValueResult{
        .success = true,
        .found = false,
        .value = QJsonValue(),
        .error = QString(),
    };
}

OcppReadValueResult readConfigValue(const QStringList &backendPathSegments,
                                    const QString &baseDirectoryPath,
                                    const QString &customDirectoryPath) {
    if (backendPathSegments.size() < 2) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    const QString controllerName = backendPathSegments.first();
    const QStringList valuePathSegments = backendPathSegments.mid(1);
    const QString customFilePath =
        buildControllerFilePath(customDirectoryPath, controllerName);
    const QString baseFilePath =
        buildControllerFilePath(baseDirectoryPath, controllerName);

    if (!customFilePath.isEmpty() && QFileInfo::exists(customFilePath)) {
        const OcppJsonLoadResult customLoadResult = loadJsonObjectFile(customFilePath);
        if (!customLoadResult.success) {
            return OcppReadValueResult{
                .success = false,
                .found = false,
                .value = QJsonValue(),
                .error = customLoadResult.error,
            };
        }

        const OcppReadValueResult customValueResult =
            extractValueFromDocument(customLoadResult.rootObject, valuePathSegments);
        if (!customValueResult.success) {
            return customValueResult;
        }
        if (customValueResult.found) {
            return customValueResult;
        }
    }

    if (!QFileInfo::exists(baseFilePath)) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kOcppFileNotFound),
        };
    }

    const OcppJsonLoadResult baseLoadResult = loadJsonObjectFile(baseFilePath);
    if (!baseLoadResult.success) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = baseLoadResult.error,
        };
    }

    const OcppReadValueResult baseValueResult =
        extractValueFromDocument(baseLoadResult.rootObject, valuePathSegments);
    if (!baseValueResult.success) {
        return baseValueResult;
    }
    if (!baseValueResult.found) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    return baseValueResult;
}

OcppFillResult fillRequestedReadParameters(const QJsonObject &requestedParameters,
                                           const QStringList &pathPrefix,
                                           const QString &baseDirectoryPath,
                                           const QString &customDirectoryPath) {
    QJsonObject filledParameters;

    const auto parameterKeys = requestedParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonValue parameterValue = requestedParameters.value(parameterKey);
        const QStringList currentPath = pathPrefix + QStringList{parameterKey};

        if (parameterValue.isObject()) {
            const OcppFillResult nestedFillResult =
                fillRequestedReadParameters(parameterValue.toObject(),
                                           currentPath,
                                           baseDirectoryPath,
                                           customDirectoryPath);
            if (!nestedFillResult.success) {
                return nestedFillResult;
            }

            filledParameters.insert(parameterKey, nestedFillResult.parameters);
            continue;
        }

        const OcppReadValueResult readValueResult =
            readConfigValue(currentPath, baseDirectoryPath, customDirectoryPath);
        if (!readValueResult.success) {
            return OcppFillResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = readValueResult.error,
            };
        }

        filledParameters.insert(parameterKey, readValueResult.value);
    }

    return OcppFillResult{
        .success = true,
        .parameters = filledParameters,
        .error = QString(),
    };
}
} // namespace

ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toOcppAction(request.action)) {
    case OCPPAction::ReadSettings:
        return handleReadRequest(request);
    case OCPPAction::WriteSettings:
        return handleWriteRequest(request);
    case OCPPAction::Unknown:
        throw std::runtime_error("OCPPConfig::handleRequest got unsupported action");
    }

    throw std::runtime_error("OCPPConfig::handleRequest reached unreachable code");
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("ocpp"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const OcppBaseDirResult baseDirectoryResult = loadOcppBaseConfigPath();
    if (!baseDirectoryResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseDirectoryResult.error},
        };
        return response;
    }

    const QString customDirectoryPath = loadOcppCustomConfigPath();
    const OcppFillResult fillResult =
        fillRequestedReadParameters(request.parameters,
                                    {},
                                    baseDirectoryResult.path,
                                    customDirectoryPath);
    if (!fillResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), fillResult.error},
        };
        return response;
    }

    response.parameters = fillResult.parameters;
    response.success = true;
    return response;
}

ModuleResponse handleWriteRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("ocpp"),
        .action = request.action,
        .parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kOcppWriteNotImplemented)},
        },
        .success = false,
        .final = true,
    };
}
} // namespace OCPPConfig
