// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "OCPPConfig.hpp"

#include "BackendConfig.hpp"
#include "EverestServiceControl.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"

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
RpcApiClient *g_rpcApiClient = nullptr;
constexpr char kOcppBaseConfigPathKey[] = "ocpp_base_config_path";
constexpr char kOcppCustomConfigPathKey[] = "ocpp_custom_config_path";
constexpr char kNetworkConnectionProfiles[] = "NetworkConnectionProfiles";
constexpr char kPropertiesKey[] = "properties";
constexpr char kAttributesKey[] = "attributes";
constexpr char kValueKey[] = "value";
constexpr char kConfigurationSlotKey[] = "configurationSlot";
constexpr char kConnectionDataKey[] = "connectionData";
constexpr char kOcppCsmsUrlKey[] = "ocppCsmsUrl";
constexpr char kOcppCsmsUrlUserIdKey[] = "userId";
constexpr char kOcppCsmsUrlCsmsServerAddressKey[] = "csmsServerAddress";
constexpr char kOcppCsmsUrlEnergyConsumptionPointKey[] = "energyConsumptionPoint";

constexpr char kOcppBaseConfigMissing[] = "ocpp_base_config_path_missing";
constexpr char kOcppBaseConfigNotFound[] = "ocpp_base_config_path_not_found";
constexpr char kOcppBaseConfigNotDirectory[] = "ocpp_base_config_path_not_a_directory";
constexpr char kOcppCustomConfigMissing[] = "ocpp_custom_config_path_missing";
constexpr char kOcppCustomConfigCreateFailed[] = "ocpp_custom_config_path_create_failed";
constexpr char kOcppCustomFileCopyFailed[] = "ocpp_custom_config_file_copy_failed";
constexpr char kOcppReadParseFailed[] = "ocpp_read_parse_failed";
constexpr char kOcppValueNotFound[] = "ocpp_value_not_found";
constexpr char kOcppFileNotFound[] = "ocpp_file_not_found";
constexpr char kOcppWriteNotImplemented[] = "ocpp_write_not_implemented";

struct OcppConfigDirResult {
    bool success = false;
    bool exists = false;
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

struct OcppWriteResult {
    bool success = false;
    QString error;
};

struct OcppBackendPathResult {
    bool success = false;
    QString controllerName;
    QStringList valuePathSegments;
    QString error;
};

struct OcppCsmsUrlParts {
    bool success = false;
    QString csmsServerAddress;
    QString energyConsumptionPoint;
    QString userId;
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

OcppConfigDirResult validateConfigPath(const QString &path,
                                       const QString &missingError,
                                       const QString &notFoundError,
                                       const QString &notDirectoryError,
                                       bool allowMissingPath) {
    const QString cleanedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);

    if (path.isEmpty()) {
        return OcppConfigDirResult{
            .success = allowMissingPath,
            .exists = false,
            .path = cleanedPath,
            .error = allowMissingPath ? QString() : missingError,
        };
    }

    const QFileInfo pathInfo(path);
    if (!pathInfo.exists()) {
        return OcppConfigDirResult{
            .success = allowMissingPath,
            .exists = false,
            .path = cleanedPath,
            .error = allowMissingPath ? QString() : notFoundError,
        };
    }

    if (!pathInfo.isDir()) {
        return OcppConfigDirResult{
            .success = false,
            .exists = false,
            .path = QString(),
            .error = notDirectoryError,
        };
    }

    return OcppConfigDirResult{
        .success = true,
        .exists = true,
        .path = cleanedPath,
        .error = QString(),
    };
}

OcppConfigDirResult loadOcppBaseConfigPath() {
    return validateConfigPath(
        loadBackendConfigValue(QString::fromLatin1(kOcppBaseConfigPathKey)),
        QString::fromLatin1(kOcppBaseConfigMissing),
        QString::fromLatin1(kOcppBaseConfigNotFound),
        QString::fromLatin1(kOcppBaseConfigNotDirectory),
        false);
}

OcppConfigDirResult loadOcppCustomConfigPath() {
    return validateConfigPath(
        loadBackendConfigValue(QString::fromLatin1(kOcppCustomConfigPathKey)),
        QString::fromLatin1(kOcppCustomConfigMissing),
        QString(),
        QString::fromLatin1(kOcppBaseConfigNotDirectory),
        true);
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

OcppWriteResult saveJsonObjectFile(const QString &filePath, const QJsonObject &rootObject) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppWriteNotImplemented),
        };
    }

    const QJsonDocument document(rootObject);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppWriteNotImplemented),
        };
    }

    return OcppWriteResult{
        .success = true,
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

bool isOcppCsmsUrlPartPath(const QStringList &pathSegments) {
    if (pathSegments.size() != 3) {
        return false;
    }

    if (pathSegments.at(0) != QLatin1String(kConnectionDataKey) ||
        pathSegments.at(1) != QLatin1String(kOcppCsmsUrlKey)) {
        return false;
    }

    const QString partName = pathSegments.at(2);
    return partName == QLatin1String(kOcppCsmsUrlUserIdKey) ||
           partName == QLatin1String(kOcppCsmsUrlCsmsServerAddressKey) ||
           partName == QLatin1String(kOcppCsmsUrlEnergyConsumptionPointKey);
}

OcppCsmsUrlParts splitOcppCsmsUrl(const QString &ocppCsmsUrl) {
    const QString normalizedUrl = ocppCsmsUrl.trimmed();
    if (normalizedUrl.isEmpty()) {
        return OcppCsmsUrlParts{
            .success = true,
            .csmsServerAddress = QString(),
            .energyConsumptionPoint = QString(),
            .userId = QString(),
        };
    }

    const int schemeSeparatorIndex = normalizedUrl.indexOf(QStringLiteral("://"));
    const int firstSplitCandidateIndex =
        schemeSeparatorIndex >= 0 ? schemeSeparatorIndex + 3 : 0;

    const int userIdSeparatorIndex = normalizedUrl.lastIndexOf(QLatin1Char('/'));
    if (userIdSeparatorIndex < firstSplitCandidateIndex ||
        userIdSeparatorIndex >= normalizedUrl.size() - 1) {
        return OcppCsmsUrlParts{
            .success = true,
            .csmsServerAddress = normalizedUrl,
            .energyConsumptionPoint = QString(),
            .userId = QString(),
        };
    }

    const int energyPointSeparatorIndex =
        normalizedUrl.lastIndexOf(QLatin1Char('/'), userIdSeparatorIndex - 1);
    if (energyPointSeparatorIndex < firstSplitCandidateIndex ||
        energyPointSeparatorIndex >= userIdSeparatorIndex - 1) {
        return OcppCsmsUrlParts{
            .success = true,
            .csmsServerAddress = normalizedUrl,
            .energyConsumptionPoint = QString(),
            .userId = QString(),
        };
    }

    return OcppCsmsUrlParts{
        .success = true,
        .csmsServerAddress = normalizedUrl.left(energyPointSeparatorIndex),
        .energyConsumptionPoint = normalizedUrl.mid(
            energyPointSeparatorIndex + 1,
            userIdSeparatorIndex - energyPointSeparatorIndex - 1),
        .userId = normalizedUrl.mid(userIdSeparatorIndex + 1),
    };
}

OcppReadValueResult extractOcppCsmsUrlPart(const QJsonObject &profileObject,
                                           const QStringList &pathSegments) {
    const OcppReadValueResult ocppCsmsUrlValue =
        navigateJsonObject(profileObject, {
                                             QString::fromLatin1(kConnectionDataKey),
                                             QString::fromLatin1(kOcppCsmsUrlKey),
                                         });
    if (!ocppCsmsUrlValue.success || !ocppCsmsUrlValue.found ||
        !ocppCsmsUrlValue.value.isString()) {
        return ocppCsmsUrlValue;
    }

    const OcppCsmsUrlParts urlParts = splitOcppCsmsUrl(ocppCsmsUrlValue.value.toString());
    if (!urlParts.success) {
        return OcppReadValueResult{
            .success = true,
            .found = false,
            .value = QJsonValue(),
            .error = QString(),
        };
    }

    const QString partName = pathSegments.at(2);
    if (partName == QLatin1String(kOcppCsmsUrlUserIdKey)) {
        return OcppReadValueResult{
            .success = true,
            .found = true,
            .value = urlParts.userId,
            .error = QString(),
        };
    }

    if (partName == QLatin1String(kOcppCsmsUrlCsmsServerAddressKey)) {
        return OcppReadValueResult{
            .success = true,
            .found = true,
            .value = urlParts.csmsServerAddress,
            .error = QString(),
        };
    }

    if (partName == QLatin1String(kOcppCsmsUrlEnergyConsumptionPointKey)) {
        return OcppReadValueResult{
            .success = true,
            .found = true,
            .value = urlParts.energyConsumptionPoint,
            .error = QString(),
        };
    }

    return OcppReadValueResult{
        .success = true,
        .found = false,
        .value = QJsonValue(),
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

int findConfigurationSlotOneProfileIndex(const QJsonArray &profilesArray) {
    for (qsizetype i = 0; i < profilesArray.size(); ++i) {
        const QJsonValue &profileValue = profilesArray.at(i);
        if (!profileValue.isObject()) {
            continue;
        }

        const QJsonObject profileObject = profileValue.toObject();
        if (profileObject.value(QString::fromLatin1(kConfigurationSlotKey)).toInt(-1) == 1) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool writeNestedJsonObjectValue(QJsonObject &jsonObject,
                                const QStringList &pathSegments,
                                const QJsonValue &value) {
    if (pathSegments.isEmpty()) {
        return false;
    }

    if (pathSegments.size() == 1) {
        jsonObject.insert(pathSegments.first(), value);
        return true;
    }

    const QString currentKey = pathSegments.first();
    if (!jsonObject.contains(currentKey) || !jsonObject.value(currentKey).isObject()) {
        return false;
    }

    QJsonObject childObject = jsonObject.value(currentKey).toObject();
    if (!writeNestedJsonObjectValue(childObject, pathSegments.mid(1), value)) {
        return false;
    }

    jsonObject.insert(currentKey, childObject);
    return true;
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

    const QJsonObject profileObject = slotOneProfile.value.toObject();
    if (isOcppCsmsUrlPartPath(pathSegments)) {
        return extractOcppCsmsUrlPart(profileObject, pathSegments);
    }

    return navigateJsonObject(profileObject, pathSegments);
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

OcppWriteResult writeStandardPropertyValue(QJsonObject &controllerRoot,
                                           const QString &propertyName,
                                           const QJsonValue &value) {
    const QString propertiesKey = QString::fromLatin1(kPropertiesKey);
    const QString attributesKey = QString::fromLatin1(kAttributesKey);
    const QString valueKey = QString::fromLatin1(kValueKey);

    const QJsonObject propertiesObject = controllerRoot.value(propertiesKey).toObject();
    if (!propertiesObject.contains(propertyName) || !propertiesObject.value(propertyName).isObject()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    QJsonObject mutablePropertiesObject = propertiesObject;
    QJsonObject propertyObject = mutablePropertiesObject.value(propertyName).toObject();
    QJsonArray attributesArray = propertyObject.value(attributesKey).toArray();
    if (attributesArray.isEmpty() || !attributesArray.first().isObject()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    QJsonObject attributeZero = attributesArray.first().toObject();
    attributeZero.insert(valueKey, value);
    attributesArray[0] = attributeZero;
    propertyObject.insert(attributesKey, attributesArray);
    mutablePropertiesObject.insert(propertyName, propertyObject);
    controllerRoot.insert(propertiesKey, mutablePropertiesObject);

    return OcppWriteResult{
        .success = true,
        .error = QString(),
    };
}

OcppWriteResult writeNetworkConnectionProfilesValue(QJsonObject &controllerRoot,
                                                    const QStringList &pathSegments,
                                                    const QJsonValue &value) {
    if (pathSegments.isEmpty()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    const OcppReadValueResult rawProfilesValue =
        extractStandardPropertyValue(controllerRoot, QString::fromLatin1(kNetworkConnectionProfiles));
    if (!rawProfilesValue.success) {
        return OcppWriteResult{
            .success = false,
            .error = rawProfilesValue.error,
        };
    }
    if (!rawProfilesValue.found || !rawProfilesValue.value.isString()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    QJsonParseError parseError;
    const QJsonDocument profilesDocument =
        QJsonDocument::fromJson(rawProfilesValue.value.toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !profilesDocument.isArray()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppReadParseFailed),
        };
    }

    QJsonArray profilesArray = profilesDocument.array();
    const int slotOneProfileIndex = findConfigurationSlotOneProfileIndex(profilesArray);
    if (slotOneProfileIndex < 0 || !profilesArray.at(slotOneProfileIndex).isObject()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    QJsonObject slotOneProfileObject = profilesArray.at(slotOneProfileIndex).toObject();
    if (!writeNestedJsonObjectValue(slotOneProfileObject, pathSegments, value)) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    profilesArray[slotOneProfileIndex] = slotOneProfileObject;
    const QString updatedProfilesJson =
        QString::fromUtf8(QJsonDocument(profilesArray).toJson(QJsonDocument::Compact));

    return writeStandardPropertyValue(
        controllerRoot,
        QString::fromLatin1(kNetworkConnectionProfiles),
        QJsonValue(updatedProfilesJson));
}

OcppBackendPathResult splitBackendPathSegments(const QStringList &pathSegments) {
    if (pathSegments.size() < 2) {
        return OcppBackendPathResult{
            .success = false,
            .controllerName = QString(),
            .valuePathSegments = QStringList{},
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    return OcppBackendPathResult{
        .success = true,
        .controllerName = pathSegments.first(),
        .valuePathSegments = pathSegments.mid(1),
        .error = QString(),
    };
}

OcppReadValueResult readConfigValue(const QStringList &backendPathSegments,
                                    const QString &baseDirectoryPath,
                                    const QString &customDirectoryPath,
                                    bool customPathExists) {
    const OcppBackendPathResult backendPathResult = splitBackendPathSegments(backendPathSegments);
    if (!backendPathResult.success) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = backendPathResult.error,
        };
    }

    const QString controllerName = backendPathResult.controllerName;
    const QStringList valuePathSegments = backendPathResult.valuePathSegments;
    const QString customFilePath =
        buildControllerFilePath(customDirectoryPath, controllerName);
    const QString baseFilePath =
        buildControllerFilePath(baseDirectoryPath, controllerName);

    const auto readValueFromFilePath = [&valuePathSegments](const QString &filePath) {
        const OcppJsonLoadResult loadResult = loadJsonObjectFile(filePath);
        if (!loadResult.success) {
            return OcppReadValueResult{
                .success = false,
                .found = false,
                .value = QJsonValue(),
                .error = loadResult.error,
            };
        }

        return extractValueFromDocument(loadResult.rootObject, valuePathSegments);
    };

    const QString preferredFilePath =
        customPathExists && !customFilePath.isEmpty() ? customFilePath : baseFilePath;

    if (!QFileInfo::exists(preferredFilePath)) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kOcppFileNotFound),
        };
    }

    const OcppReadValueResult preferredValueResult = readValueFromFilePath(preferredFilePath);
    if (!preferredValueResult.success) {
        return preferredValueResult;
    }
    if (preferredValueResult.found || preferredFilePath == baseFilePath) {
        if (preferredValueResult.found) {
            return preferredValueResult;
        }

        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kOcppValueNotFound),
        };
    }

    if (!QFileInfo::exists(baseFilePath)) {
        return OcppReadValueResult{
            .success = false,
            .found = false,
            .value = QJsonValue(),
            .error = QString::fromLatin1(kOcppFileNotFound),
        };
    }

    const OcppReadValueResult baseValueResult = readValueFromFilePath(baseFilePath);
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

QStringList buildBackendPathSegments(const QStringList &pathPrefix, const QString &parameterKey) {
    return pathPrefix + QStringList{parameterKey};
}

OcppFillResult fillRequestedReadParameters(const QJsonObject &requestedParameters,
                                           const QStringList &pathPrefix,
                                           const QString &baseDirectoryPath,
                                           const QString &customDirectoryPath,
                                           bool customPathExists) {
    QJsonObject filledParameters;

    const auto parameterKeys = requestedParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonValue parameterValue = requestedParameters.value(parameterKey);
        const QStringList currentPath = buildBackendPathSegments(pathPrefix, parameterKey);

        if (parameterValue.isObject()) {
            const OcppFillResult nestedFillResult =
                fillRequestedReadParameters(parameterValue.toObject(),
                                           currentPath,
                                           baseDirectoryPath,
                                           customDirectoryPath,
                                           customPathExists);
            if (!nestedFillResult.success) {
                return nestedFillResult;
            }

            filledParameters.insert(parameterKey, nestedFillResult.parameters);
            continue;
        }

        const OcppReadValueResult readValueResult =
            readConfigValue(currentPath, baseDirectoryPath, customDirectoryPath, customPathExists);
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

OcppWriteResult writeValueIntoDocument(const QStringList &valuePathSegments,
                                       const QJsonValue &value,
                                       const QString &customFilePath) {
    const OcppJsonLoadResult loadResult = loadJsonObjectFile(customFilePath);
    if (!loadResult.success) {
        return OcppWriteResult{
            .success = false,
            .error = loadResult.error,
        };
    }

    QJsonObject controllerRoot = loadResult.rootObject;
    OcppWriteResult writeResult;

    if (valuePathSegments.isEmpty()) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    if (valuePathSegments.first() == QLatin1String(kNetworkConnectionProfiles)) {
        writeResult = writeNetworkConnectionProfilesValue(
            controllerRoot,
            valuePathSegments.mid(1),
            value);
    } else if (valuePathSegments.size() == 1) {
        writeResult = writeStandardPropertyValue(
            controllerRoot,
            valuePathSegments.first(),
            value);
    } else {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    if (!writeResult.success) {
        return writeResult;
    }

    return saveJsonObjectFile(customFilePath, controllerRoot);
}

OcppWriteResult writeConfigValue(const QStringList &backendPathSegments,
                                 const QJsonValue &value,
                                 const QString &customDirectoryPath) {
    const OcppBackendPathResult backendPathResult = splitBackendPathSegments(backendPathSegments);
    if (!backendPathResult.success) {
        return OcppWriteResult{
            .success = false,
            .error = backendPathResult.error,
        };
    }

    const QString customFilePath =
        buildControllerFilePath(customDirectoryPath, backendPathResult.controllerName);
    if (!QFileInfo::exists(customFilePath)) {
        return OcppWriteResult{
            .success = false,
            .error = QString::fromLatin1(kOcppFileNotFound),
        };
    }

    return writeValueIntoDocument(backendPathResult.valuePathSegments, value, customFilePath);
}

OcppWriteResult writeRequestedWriteParameters(const QJsonObject &requestedParameters,
                                              const QStringList &pathPrefix,
                                              const QString &customDirectoryPath) {
    const auto parameterKeys = requestedParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonValue parameterValue = requestedParameters.value(parameterKey);
        const QStringList currentPath = buildBackendPathSegments(pathPrefix, parameterKey);

        if (parameterValue.isObject()) {
            const OcppWriteResult nestedWriteResult =
                writeRequestedWriteParameters(parameterValue.toObject(),
                                             currentPath,
                                             customDirectoryPath);
            if (!nestedWriteResult.success) {
                return nestedWriteResult;
            }

            continue;
        }

        const OcppWriteResult writeResult =
            writeConfigValue(currentPath, parameterValue, customDirectoryPath);
        if (!writeResult.success) {
            return writeResult;
        }
    }

    return OcppWriteResult{
        .success = true,
        .error = QString(),
    };
}

QStringList collectRequestedControllerNames(const QJsonObject &parameters) {
    QStringList controllerNames;
    const auto parameterKeys = parameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        if (!parameters.value(parameterKey).isObject()) {
            continue;
        }

        if (!controllerNames.contains(parameterKey)) {
            controllerNames.append(parameterKey);
        }
    }

    return controllerNames;
}

QString ensureCustomConfigDirectoryExists(const QString &customDirectoryPath) {
    if (customDirectoryPath.trimmed().isEmpty()) {
        return QString::fromLatin1(kOcppCustomConfigMissing);
    }

    QDir directory;
    if (directory.mkpath(customDirectoryPath)) {
        return QString();
    }

    return QString::fromLatin1(kOcppCustomConfigCreateFailed);
}

QString ensureCustomControllerFileExists(const QString &controllerName,
                                         const QString &baseDirectoryPath,
                                         const QString &customDirectoryPath) {
    const QString customFilePath = buildControllerFilePath(customDirectoryPath, controllerName);
    if (QFileInfo::exists(customFilePath)) {
        return QString();
    }

    const QString baseFilePath = buildControllerFilePath(baseDirectoryPath, controllerName);
    if (!QFileInfo::exists(baseFilePath)) {
        return QString::fromLatin1(kOcppFileNotFound);
    }

    if (QFile::copy(baseFilePath, customFilePath)) {
        return QString();
    }

    return QString::fromLatin1(kOcppCustomFileCopyFailed);
}

ModuleResponse restartEverestStack(ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QStringLiteral("everest_state_not_allowed")) {
            error =
                QStringLiteral("config could not be applied because EVerest-stack is in state \"%1\" and cannot be restarted, please unplug the EV and try again")
                    .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QStringLiteral("error"), error},
        };
        return response;
    }

    const EverestServiceControlResult restartResult =
        EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), restartResult.error},
        };
        return response;
    }

    response.parameters = QJsonObject{};
    response.success = true;
    return response;
}
} // namespace

void setRpcApiClient(RpcApiClient *rpcApiClient) {
    g_rpcApiClient = rpcApiClient;
}

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

    const OcppConfigDirResult baseDirectoryResult = loadOcppBaseConfigPath();
    if (!baseDirectoryResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseDirectoryResult.error},
        };
        return response;
    }

    const OcppConfigDirResult customDirectoryResult = loadOcppCustomConfigPath();
    if (!customDirectoryResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), customDirectoryResult.error},
        };
        return response;
    }

    const OcppFillResult fillResult =
        fillRequestedReadParameters(request.parameters,
                                    {},
                                    baseDirectoryResult.path,
                                    customDirectoryResult.exists ? customDirectoryResult.path
                                                                 : QString(),
                                    customDirectoryResult.exists);
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
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("ocpp"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const OcppConfigDirResult baseDirectoryResult = loadOcppBaseConfigPath();
    if (!baseDirectoryResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), baseDirectoryResult.error},
        };
        return response;
    }

    const OcppConfigDirResult customDirectoryResult = loadOcppCustomConfigPath();
    if (!customDirectoryResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), customDirectoryResult.error},
        };
        return response;
    }

    const QString customDirectoryPath = customDirectoryResult.path;
    if (!customDirectoryResult.exists) {
        const QString ensureDirectoryError = ensureCustomConfigDirectoryExists(customDirectoryPath);
        if (!ensureDirectoryError.isEmpty()) {
            response.parameters = QJsonObject{
                {QStringLiteral("error"), ensureDirectoryError},
            };
            return response;
        }
    }

    const QStringList controllerNames = collectRequestedControllerNames(request.parameters);
    for (const QString &controllerName : controllerNames) {
        const QString ensureFileError =
            ensureCustomControllerFileExists(
                controllerName,
                baseDirectoryResult.path,
                customDirectoryPath);
        if (!ensureFileError.isEmpty()) {
            response.parameters = QJsonObject{
                {QStringLiteral("error"), ensureFileError},
            };
            return response;
        }
    }

    const OcppWriteResult writeResult =
        writeRequestedWriteParameters(request.parameters, {}, customDirectoryPath);
    if (!writeResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), writeResult.error},
        };
        return response;
    }

    return restartEverestStack(response);
}
} // namespace OCPPConfig
