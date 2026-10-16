// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SafetyController.hpp"

#include "BackendConfig.hpp"
#include "ConsoleConnector.hpp"
#include "EverestServiceControl.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"
#include "YamlUtils.hpp"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace SafetyController {
QJsonObject readRequestedParametersFromYaml(const QJsonObject& requestParameters, const QJsonObject& yamlRoot);
namespace {
RpcApiClient* g_rpcApiClient = nullptr;
constexpr char kModuleEvseManager[] = "EvseManager";
constexpr char kErrorSafetyControllerYamlWriteFailed[] = "safety_controller_yaml_write_failed";
constexpr char kErrorSafetyControllerPbCreateFailed[] = "safety_controller_pb_create_failed";
constexpr char kErrorSafetyControllerFlashFailed[] = "safety_controller_flash_failed";
constexpr char kParametersPt1000[] = "pt1000_";
constexpr char kParametersContactors[] = "contactors_";
constexpr char kParametersEstops[] = "estops_";
constexpr char kCmdFlagI[] = "-i";
constexpr char kCmdFlagO[] = "-o";
constexpr char kSftyCtrlrParamDisabled[] = "disabled";
constexpr char kSftyCtrlrParamAbortTemp[] = "abort-temperature";
constexpr char kSftyCtrlrParamResistanceOffset[] = "resistance-offset";
constexpr char kSftyCtrlrParamOvertempProtection[] = "overtemperature-protection";
constexpr char kSftyCtrlrParamCloseTime[] = "close-time";
constexpr char kSftyCtrlrParamOpenTime[] = "open-time";
constexpr char kSftyCtrlrParamEnabled[] = "enabled";
constexpr char kSftyCtrlrParamPt1000S[] = "pt1000s";
constexpr char kSftyCtrlrParamContactors[] = "contactors";
constexpr char kSftyCtrlrParamEstops[] = "estops";
constexpr char kSafetyControllerSettingAll[] = "all";
constexpr char kSafetyControllerSettingNone[] = "none";
constexpr char kSafetyControllerSettingPt1000[] = "pt1000";
constexpr char kSafetyControllerSettingContactors[] = "contactors";
constexpr char kSafetyControllerSettingEstops[] = "estops";
constexpr char kUnitMs[] = " ms";
constexpr char kConfSafetyControllerSettingsBin[] = "safety_controller_settings_bin";
constexpr char kConfSafetyControllerSettingsYaml[] = "safety_controller_settings_yaml";
constexpr char kConfSafetyControllerAvailableSettings[] = "safety_controller_available_settings";
constexpr char kCacheDirectory[] = "/run/ra-utils";
constexpr char kKeyActiveModules[] = "active_modules";
constexpr char kKeyConnections[] = "connections";
constexpr char kKeyMapping[] = "mapping";
constexpr char kKeyModule[] = "module";
constexpr char kKeyEvse[] = "evse";
constexpr char kKeyBsp[] = "bsp";
constexpr char kKeyModuleId[] = "module_id";
constexpr char kKeySerialPort[] = "serial_port";
constexpr char kKeyResetGpioLineName[] = "reset_gpio_line_name";
constexpr char kKeyDefault[] = "default";
constexpr char kKeyConfig[] = "config";
constexpr char kKeyControllers[] = "controllers";
constexpr char kKeyDeviceName[] = "device_name";
constexpr char kKeyBspInstance[] = "bsp_instance";
constexpr char kKeyDriverModule[] = "driver_module";
constexpr char kKeySettings[] = "settings";
constexpr char kKeyAvailable[] = "available";
constexpr char kKeyWritable[] = "writable";
constexpr char kKeyMessage[] = "message";
constexpr char kKeyRpcAvailable[] = "rpc_available";
constexpr char kKeyResolutionError[] = "resolution_error";
constexpr char kErrorDeviceInvalid[] = "safety_controller_device_invalid";
constexpr char kErrorDeviceNotConfigured[] = "safety_controller_device_not_configured";
constexpr char kErrorEvseMappingUnavailable[] = "safety_controller_evse_mapping_unavailable";
constexpr char kErrorYamlPublishAfterFlash[] = "safety_controller_yaml_publish_failed_after_flash";
constexpr char kMessageEverestErrorAfterRestart[] =
    "Safety Controller settings were applied successfully. EVerest reported an error state after restarting. "
    "Check the EVerest error logs.";

struct SafetyControllerDevice {
    QString name;
    QString resetGpioLineName;
    QString bootModeGpioLineName;
    QString bspInstance;
    QString driverModule;
    QString yamlPath;
    QStringList evseManagerInstances;
    QList<int> evseIndices;
    bool evseMappingResolved = false;
};

struct SafetyControllerDeviceResolution {
    QList<SafetyControllerDevice> devices;
    QStringList errors;
};

SafetyControllerAction toSafetyControllerAction(const QString& action) {
    if (action == QLatin1String(kActionReadSettings)) {
        return SafetyControllerAction::ReadSettings;
    }
    if (action == QLatin1String(kActionWriteSettings)) {
        return SafetyControllerAction::WriteSettings;
    }

    return SafetyControllerAction::Unknown;
}

QString stripUnitSuffix(const QJsonValue& value) {
    QString text = value.toString().trimmed();
    text.replace(QStringLiteral("Â°C"), QStringLiteral("\u00b0C"));
    text.replace(QStringLiteral("Î©"), QStringLiteral("\u03a9"));
    return text.section(QLatin1Char(' '), 0, 0);
}

QString unitCelsius() {
    return QStringLiteral(" \u00b0C");
}

QString unitOhm() {
    return QStringLiteral(" \u03a9");
}

QString jsonValueToText(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString().trimmed();
    }

    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'f', -1);
    }

    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    return QString();
}

QStringList defaultAvailableSafetyControllerSettings() {
    return {
        QLatin1String(kSafetyControllerSettingPt1000),
        QLatin1String(kSafetyControllerSettingContactors),
        QLatin1String(kSafetyControllerSettingEstops),
    };
}

QStringList loadAvailableSafetyControllerSettings() {
    const QString configuredSettings =
        ::readBackendConfigValue(QLatin1String(kConfSafetyControllerAvailableSettings)).trimmed();
    if (configuredSettings.isEmpty()) {
        return defaultAvailableSafetyControllerSettings();
    }

    QStringList availableSettings;
    const QStringList configuredSettingList = configuredSettings.split(QLatin1Char(','));
    for (const QString& configuredSetting : configuredSettingList) {
        const QString setting = configuredSetting.trimmed();
        if (!setting.isEmpty()) {
            availableSettings.append(setting);
        }
    }

    if (availableSettings.isEmpty()) {
        return defaultAvailableSafetyControllerSettings();
    }

    return availableSettings;
}

bool isSafetyControllerSettingAvailable(const QStringList& availableSettings, const QString& setting) {
    if (availableSettings.contains(QLatin1String(kSafetyControllerSettingNone), Qt::CaseInsensitive)) {
        return false;
    }

    return availableSettings.contains(QLatin1String(kSafetyControllerSettingAll), Qt::CaseInsensitive) ||
           availableSettings.contains(setting, Qt::CaseInsensitive);
}

bool isSafeDeviceName(const QString& deviceName) {
    static const QRegularExpression deviceNamePattern(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    return deviceNamePattern.match(deviceName).hasMatch() && deviceName != QStringLiteral(".") &&
           deviceName != QStringLiteral("..");
}

bool resolveDeviceName(const QString& serialPort, QString& deviceName) {
    QString normalized = serialPort.trimmed();
    if (normalized.startsWith(QStringLiteral("/dev/"))) {
        normalized.remove(0, 5);
    }
    if (!isSafeDeviceName(normalized)) {
        return false;
    }
    deviceName = normalized;
    return true;
}

bool defaultModuleConfigFromManifest(const QString& moduleName, const QString& configKey, const QString& manifestRoot,
                                     QString& value, QString& error) {
    if (!isSafeDeviceName(moduleName)) {
        error = QStringLiteral("invalid driver module name: %1").arg(moduleName);
        return false;
    }
    const QString manifestPath = QDir(manifestRoot).filePath(moduleName + QStringLiteral("/manifest.yaml"));
    const YamlLoadResult manifestResult = loadYamlFile(manifestPath);
    if (!manifestResult.success) {
        error = QStringLiteral("unable to load module manifest %1").arg(manifestPath);
        return false;
    }

    const QJsonObject config = manifestResult.yamlRoot.value(QLatin1String(kKeyConfig)).toObject();
    const QJsonObject valueConfig = config.value(configKey).toObject();
    value = valueConfig.value(QLatin1String(kKeyDefault)).toString().trimmed();
    if (value.isEmpty()) {
        error = QStringLiteral("%1 has no default in %2").arg(configKey, manifestPath);
        return false;
    }
    return true;
}

// Platform-specific GPIO naming convention; remove when boot-mode lines become configurable.
bool deriveBootModeGpioLineName(const QString& resetGpioLineName, QString& bootModeGpioLineName) {
    static const QRegularExpression resetLinePattern(QStringLiteral("SAFETY(\\w*)_RESET_INT$"));
    const QRegularExpressionMatch match = resetLinePattern.match(resetGpioLineName);
    if (!match.hasMatch()) {
        return false;
    }

    bootModeGpioLineName = QStringLiteral("SAFETY%1_BOOTMODE_SET").arg(match.captured(1));
    return true;
}

SafetyControllerDeviceResolution resolveSafetyControllerDevices(const QJsonObject& effectiveConfig,
                                                                const QString& manifestRoot) {
    SafetyControllerDeviceResolution resolution;
    const QJsonObject activeModules = effectiveConfig.value(QLatin1String(kKeyActiveModules)).toObject();
    bool foundEvseManager = false;
    QStringList evseManagerInstances;

    for (const QString& instanceName : activeModules.keys()) {
        const QJsonObject module = activeModules.value(instanceName).toObject();
        if (module.value(QLatin1String(kEverestConfModule)).toString() == QLatin1String(kModuleEvseManager)) {
            evseManagerInstances.append(instanceName);
        }
    }

    const bool singleEvseManager = evseManagerInstances.size() == 1;

    for (const QString& instanceName : evseManagerInstances) {
        const QJsonObject evseModule = activeModules.value(instanceName).toObject();
        foundEvseManager = true;
        const QJsonValue mappingConfiguration = evseModule.value(QLatin1String(kKeyMapping));
        const QJsonValue configuredEvseIndex =
            mappingConfiguration.toObject().value(QLatin1String(kKeyModule)).toObject().value(QLatin1String(kKeyEvse));
        const bool validConfiguredIndex = configuredEvseIndex.isDouble() && configuredEvseIndex.toDouble() > 0 &&
                                          configuredEvseIndex.toDouble() <= std::numeric_limits<int>::max() &&
                                          std::floor(configuredEvseIndex.toDouble()) == configuredEvseIndex.toDouble();
        const bool useSingleEvseFallback = mappingConfiguration.isUndefined() && singleEvseManager;
        const bool evseMappingResolved = validConfiguredIndex || useSingleEvseFallback;
        const int evseIndex = validConfiguredIndex ? configuredEvseIndex.toInt() : (useSingleEvseFallback ? 1 : 0);
        if (!evseMappingResolved) {
            resolution.errors.append(
                QStringLiteral("EvseManager %1 has no usable mapping.module.evse index").arg(instanceName));
        }
        const QJsonArray bspConnections =
            evseModule.value(QLatin1String(kKeyConnections)).toObject().value(QLatin1String(kKeyBsp)).toArray();
        for (const QJsonValue& connectionValue : bspConnections) {
            const QString bspInstance = connectionValue.toObject().value(QLatin1String(kKeyModuleId)).toString();
            const QJsonObject bspModule = activeModules.value(bspInstance).toObject();
            const QString driverModule = bspModule.value(QLatin1String(kEverestConfModule)).toString();
            if (bspInstance.isEmpty() || driverModule.isEmpty()) {
                resolution.errors.append(
                    QStringLiteral("EvseManager %1 has an unresolved BSP module reference").arg(instanceName));
                continue;
            }

            QString serialPort = bspModule.value(QLatin1String(kEverestConfConfigModule))
                                     .toObject()
                                     .value(QLatin1String(kKeySerialPort))
                                     .toString()
                                     .trimmed();
            if (serialPort.isEmpty()) {
                QString manifestError;
                if (!defaultModuleConfigFromManifest(driverModule, QLatin1String(kKeySerialPort), manifestRoot,
                                                     serialPort, manifestError)) {
                    resolution.errors.append(
                        QStringLiteral("BSP %1 (%2): %3").arg(bspInstance, driverModule, manifestError));
                    continue;
                }
            }

            QString resetGpioLineName = bspModule.value(QLatin1String(kEverestConfConfigModule))
                                            .toObject()
                                            .value(QLatin1String(kKeyResetGpioLineName))
                                            .toString()
                                            .trimmed();
            if (resetGpioLineName.isEmpty()) {
                QString manifestError;
                if (!defaultModuleConfigFromManifest(driverModule, QLatin1String(kKeyResetGpioLineName), manifestRoot,
                                                     resetGpioLineName, manifestError)) {
                    resolution.errors.append(
                        QStringLiteral("BSP %1 (%2): %3").arg(bspInstance, driverModule, manifestError));
                    continue;
                }
            }

            QString bootModeGpioLineName;
            if (!deriveBootModeGpioLineName(resetGpioLineName, bootModeGpioLineName)) {
                resolution.errors.append(QStringLiteral("BSP %1 (%2) has an unsupported reset_gpio_line_name: %3")
                                             .arg(bspInstance, driverModule, resetGpioLineName));
                continue;
            }

            QString deviceName;
            if (!resolveDeviceName(serialPort, deviceName)) {
                resolution.errors.append(
                    QStringLiteral("BSP %1 (%2) has an invalid serial_port value").arg(bspInstance, driverModule));
                continue;
            }

            auto existingDevice =
                std::find_if(resolution.devices.begin(), resolution.devices.end(),
                             [&deviceName](const SafetyControllerDevice& device) { return device.name == deviceName; });
            if (existingDevice != resolution.devices.end()) {
                if (!existingDevice->evseManagerInstances.contains(instanceName)) {
                    existingDevice->evseManagerInstances.append(instanceName);
                }
                if (evseMappingResolved && !existingDevice->evseIndices.contains(evseIndex)) {
                    existingDevice->evseIndices.append(evseIndex);
                }
                existingDevice->evseMappingResolved = existingDevice->evseMappingResolved && evseMappingResolved;
                continue;
            }
            SafetyControllerDevice device{
                deviceName,
                resetGpioLineName,
                bootModeGpioLineName,
                bspInstance,
                driverModule,
                QDir(QLatin1String(kCacheDirectory)).filePath(deviceName + QStringLiteral(".yaml")),
                {instanceName},
                {},
                evseMappingResolved};
            if (evseMappingResolved) {
                device.evseIndices.append(evseIndex);
            }
            resolution.devices.append(device);
        }
    }

    if (!foundEvseManager) {
        resolution.errors.append(QStringLiteral("No active EvseManager module was found"));
    } else if (resolution.devices.isEmpty() && resolution.errors.isEmpty()) {
        resolution.errors.append(QStringLiteral("No BSP connection was found for active EvseManager modules"));
    }
    return resolution;
}

QJsonObject safetyControllerDeviceToJson(const SafetyControllerDevice& device, const QJsonObject& requestedParameters,
                                         bool rpcAvailable) {
    QJsonObject controller{
        {QLatin1String(kKeyDeviceName), device.name},
        {QLatin1String(kKeyBspInstance), device.bspInstance},
        {QLatin1String(kKeyDriverModule), device.driverModule},
        {QLatin1String(kKeyAvailable), false},
        {QLatin1String(kKeyWritable), false},
        {QLatin1String(kKeyMessage), QString()},
    };

    const YamlLoadResult yamlResult = loadYamlFile(device.yamlPath);
    if (!yamlResult.success) {
        controller.insert(
            QLatin1String(kKeyMessage),
            QFile::exists(device.yamlPath + QStringLiteral(".invalid"))
                ? QStringLiteral("Safety Controller settings are unavailable after an interrupted or "
                                 "unsuccessful write. A controller readback is required.")
                : QStringLiteral("Safety Controller settings are unavailable at %1").arg(device.yamlPath));
        return controller;
    }

    controller.insert(QLatin1String(kKeyAvailable), true);
    const bool writable = rpcAvailable && device.evseMappingResolved;
    controller.insert(QLatin1String(kKeyWritable), writable);
    controller.insert(QLatin1String(kKeySettings),
                      readRequestedParametersFromYaml(requestedParameters, yamlResult.yamlRoot));
    if (!device.evseMappingResolved) {
        controller.insert(QLatin1String(kKeyMessage),
                          QStringLiteral("Settings are read-only because the owning EVSE mapping is unavailable."));
    } else if (!rpcAvailable) {
        controller.insert(QLatin1String(kKeyMessage),
                          QStringLiteral("EVerest JSON-RPC is unavailable; settings are read-only."));
    }
    return controller;
}
} // namespace

void setRpcApiClient(RpcApiClient* rpcApiClient) {
    g_rpcApiClient = rpcApiClient;
}
QString loadBackendConfigValue(const QString& configKey) {
    return ::readBackendConfigValue(configKey);
}

SafetyControllerConfigPathResult loadSafetyControllerSettingsPath(const QString& configKey) {
    const QString value = loadBackendConfigValue(configKey);
    if (!value.isEmpty()) {
        return SafetyControllerConfigPathResult{
            .success = true,
            .path = value,
            .error = QString(),
        };
    }

    return SafetyControllerConfigPathResult{
        .success = false,
        .path = QString(),
        .error = configKey + QLatin1String(kErrorMissing),
    };
}

QJsonObject readPt1000ParametersFromYaml(const QJsonObject& requestBlock, const QJsonValue& yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString() && yamlEntry.toString() == QLatin1String(kSftyCtrlrParamDisabled)) {
        filledBlock.insert(QLatin1String(kSftyCtrlrParamAbortTemp), QString());
        filledBlock.insert(QLatin1String(kSftyCtrlrParamResistanceOffset), QString());
        filledBlock.insert(QLatin1String(kSftyCtrlrParamOvertempProtection), false);
        return filledBlock;
    }

    if (!yamlEntry.isObject()) {
        return filledBlock;
    }

    const QJsonObject yamlObject = yamlEntry.toObject();
    filledBlock.insert(QLatin1String(kSftyCtrlrParamAbortTemp),
                       stripUnitSuffix(yamlObject.value(QLatin1String(kSftyCtrlrParamAbortTemp))));
    filledBlock.insert(QLatin1String(kSftyCtrlrParamResistanceOffset),
                       stripUnitSuffix(yamlObject.value(QLatin1String(kSftyCtrlrParamResistanceOffset))));
    filledBlock.insert(QLatin1String(kSftyCtrlrParamOvertempProtection), true);
    return filledBlock;
}

QJsonObject readContactorParametersFromYaml(const QJsonObject& requestBlock, const QJsonValue& yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString() && yamlEntry.toString() == QLatin1String(kSftyCtrlrParamDisabled)) {
        filledBlock.insert(QLatin1String(kKeyType), QLatin1String(kSftyCtrlrParamDisabled));
        filledBlock.insert(QLatin1String(kSftyCtrlrParamCloseTime), QStringLiteral(""));
        filledBlock.insert(QLatin1String(kSftyCtrlrParamOpenTime), QStringLiteral(""));
        return filledBlock;
    }

    if (!yamlEntry.isObject()) {
        return filledBlock;
    }

    const QJsonObject yamlObject = yamlEntry.toObject();
    filledBlock.insert(QLatin1String(kKeyType), yamlObject.value(QLatin1String(kKeyType)));
    filledBlock.insert(QLatin1String(kSftyCtrlrParamCloseTime),
                       stripUnitSuffix(yamlObject.value(QLatin1String(kSftyCtrlrParamCloseTime))));
    filledBlock.insert(QLatin1String(kSftyCtrlrParamOpenTime),
                       stripUnitSuffix(yamlObject.value(QLatin1String(kSftyCtrlrParamOpenTime))));
    return filledBlock;
}

QJsonObject readEstopParametersFromYaml(const QJsonObject& requestBlock, const QJsonValue& yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString()) {
        filledBlock.insert(QLatin1String(kSftyCtrlrParamEnabled), yamlEntry.toString());
    }

    return filledBlock;
}

QJsonObject readRequestedParametersFromYaml(const QJsonObject& requestParameters, const QJsonObject& yamlRoot) {
    QJsonObject filledParameters = requestParameters;
    const QStringList availableSettings = loadAvailableSafetyControllerSettings();
    const QJsonArray pt1000Entries = yamlRoot.value(QLatin1String(kSftyCtrlrParamPt1000S)).toArray();
    const QJsonArray contactorEntries = yamlRoot.value(QLatin1String(kSftyCtrlrParamContactors)).toArray();
    const QJsonArray estopEntries = yamlRoot.value(QLatin1String(kSftyCtrlrParamEstops)).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString& parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QLatin1String(kParametersPt1000))) {
            if (!isSafetyControllerSettingAvailable(availableSettings, QLatin1String(kSafetyControllerSettingPt1000))) {
                filledParameters.remove(parameterKey);
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersPt1000).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
                filledParameters.insert(parameterKey,
                                        readPt1000ParametersFromYaml(requestBlock, pt1000Entries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersContactors))) {
            if (!isSafetyControllerSettingAvailable(availableSettings,
                                                    QLatin1String(kSafetyControllerSettingContactors))) {
                filledParameters.remove(parameterKey);
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersContactors).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                filledParameters.insert(parameterKey,
                                        readContactorParametersFromYaml(requestBlock, contactorEntries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersEstops))) {
            if (!isSafetyControllerSettingAvailable(availableSettings, QLatin1String(kSafetyControllerSettingEstops))) {
                filledParameters.remove(parameterKey);
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersEstops).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < estopEntries.size()) {
                filledParameters.insert(parameterKey,
                                        readEstopParametersFromYaml(requestBlock, estopEntries.at(index)));
            }
        }
    }

    return filledParameters;
}

bool validatePt1000Settings(const QJsonObject& parameters,
                            const QStringList& availableSettings = loadAvailableSafetyControllerSettings()) {
    if (!isSafetyControllerSettingAvailable(availableSettings, QLatin1String(kSafetyControllerSettingPt1000))) {
        return true;
    }
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        if (!it.key().startsWith(QLatin1String(kParametersPt1000))) {
            continue;
        }
        bool validIndex = false;
        const int index = it.key().mid(QLatin1String(kParametersPt1000).size()).toInt(&validIndex);
        if (!validIndex || index < 0) {
            continue;
        }
        const QJsonObject block = it.value().toObject();
        if (!block.value(QLatin1String(kSftyCtrlrParamOvertempProtection)).toBool()) {
            continue;
        }
        for (const char* field : {kSftyCtrlrParamAbortTemp, kSftyCtrlrParamResistanceOffset}) {
            const QJsonValue value = block.value(QLatin1String(field));
            bool validNumber = value.isDouble();
            const double number = validNumber ? value.toDouble() : value.toString().trimmed().toDouble(&validNumber);
            if (!validNumber || !std::isfinite(number)) {
                return false;
            }
        }
    }
    return true;
}

QJsonValue updatePt1000ParametersInYaml(const QJsonObject& requestBlock) {
    const bool overtemperatureProtection =
        requestBlock.value(QLatin1String(kSftyCtrlrParamOvertempProtection)).toBool();
    if (!overtemperatureProtection) {
        return QLatin1String(kSftyCtrlrParamDisabled);
    }

    return QJsonObject{
        {QLatin1String(kSftyCtrlrParamAbortTemp),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamAbortTemp))) + unitCelsius()},
        {QLatin1String(kSftyCtrlrParamResistanceOffset),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamResistanceOffset))) + unitOhm()},
    };
}

QJsonValue updateContactorParametersInYaml(const QJsonObject& requestBlock) {
    const QString type = requestBlock.value(QLatin1String(kKeyType)).toString();
    if (type == QLatin1String(kSftyCtrlrParamDisabled)) {
        return QLatin1String(kSftyCtrlrParamDisabled);
    }

    return QJsonObject{
        {QLatin1String(kKeyType), type},
        {QLatin1String(kSftyCtrlrParamCloseTime),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamCloseTime))) + QLatin1String(kUnitMs)},
        {QLatin1String(kSftyCtrlrParamOpenTime),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamOpenTime))) + QLatin1String(kUnitMs)},
    };
}

QJsonValue updateEstopParametersInYaml(const QJsonObject& requestBlock) {
    return requestBlock.value(QLatin1String(kSftyCtrlrParamEnabled));
}

QJsonObject
updateRequestParametersInYaml(const QJsonObject& requestParameters, const QJsonObject& yamlRoot,
                              const QStringList& availableSettings = loadAvailableSafetyControllerSettings()) {
    QJsonObject updatedYamlRoot = yamlRoot;
    QJsonArray pt1000Entries = updatedYamlRoot.value(QLatin1String(kSftyCtrlrParamPt1000S)).toArray();
    QJsonArray contactorEntries = updatedYamlRoot.value(QLatin1String(kSftyCtrlrParamContactors)).toArray();
    QJsonArray estopEntries = updatedYamlRoot.value(QLatin1String(kSftyCtrlrParamEstops)).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString& parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QLatin1String(kParametersPt1000))) {
            if (!isSafetyControllerSettingAvailable(availableSettings, QLatin1String(kSafetyControllerSettingPt1000))) {
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersPt1000).size());
            bool validIndex = false;
            const int index = indexString.toInt(&validIndex);
            if (validIndex && index >= 0 && index < pt1000Entries.size()) {
                pt1000Entries.replace(index, updatePt1000ParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersContactors))) {
            if (!isSafetyControllerSettingAvailable(availableSettings,
                                                    QLatin1String(kSafetyControllerSettingContactors))) {
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersContactors).size());
            bool validIndex = false;
            const int index = indexString.toInt(&validIndex);
            if (validIndex && index >= 0 && index < contactorEntries.size()) {
                contactorEntries.replace(index, updateContactorParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersEstops))) {
            if (!isSafetyControllerSettingAvailable(availableSettings, QLatin1String(kSafetyControllerSettingEstops))) {
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersEstops).size());
            bool validIndex = false;
            const int index = indexString.toInt(&validIndex);
            if (validIndex && index >= 0 && index < estopEntries.size()) {
                estopEntries.replace(index, updateEstopParametersInYaml(requestBlock));
            }
        }
    }

    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamPt1000S), pt1000Entries);
    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamContactors), contactorEntries);
    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamEstops), estopEntries);
    return updatedYamlRoot;
}

bool writeSafetyControllerYamlFile(const QString& yamlPath, const QByteArray& yaml) {
    QSaveFile file(yamlPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(yaml) != yaml.size() || !file.commit()) {
        qWarning() << "Unable to publish Safety Controller YAML:" << yamlPath << file.errorString();
        return false;
    }
    return true;
}

YAML::Node jsonToYaml(const QJsonValue& value) {
    if (value.isObject()) {
        YAML::Node node(YAML::NodeType::Map);
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            node[it.key().toStdString()] = jsonToYaml(it.value());
        }
        return node;
    }
    if (value.isArray()) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& child : value.toArray()) {
            node.push_back(jsonToYaml(child));
        }
        return node;
    }
    if (value.isBool()) {
        return YAML::Node(value.toBool());
    }
    if (value.isDouble()) {
        return YAML::Node(value.toDouble());
    }
    if (value.isString()) {
        return YAML::Node(value.toString().toStdString());
    }
    return YAML::Node(YAML::NodeType::Null);
}

QByteArray emitSafetyControllerYaml(const YAML::Node& root) {
    YAML::Emitter emitter;
    emitter << root;
    return emitter.good() ? QByteArray(emitter.c_str()) + '\n' : QByteArray();
}

bool writeSafetyControllerYamlFile(const QString& yamlPath, const QJsonObject& yamlRoot) {
    return writeSafetyControllerYamlFile(yamlPath, emitSafetyControllerYaml(jsonToYaml(yamlRoot)));
}

QByteArray prepareSafetyControllerYaml(const QString& cachePath, const QJsonObject& settings,
                                       const QJsonObject& cachedParameters,
                                       const QStringList& available = loadAvailableSafetyControllerSettings()) {
    try {
        YAML::Node root = YAML::LoadFile(cachePath.toStdString());
        if (!root.IsMap()) {
            return {};
        }
        const QJsonObject updated = updateRequestParametersInYaml(settings, cachedParameters, available);
        // Patch selected entries in the native document, not a schema-limited reconstruction.
        const auto patchEntries = [&](const QString& prefix, const QString& section, const QString& setting) {
            if (!isSafetyControllerSettingAvailable(available, setting)) {
                return;
            }
            const QJsonArray entries = updated.value(section).toArray();
            for (auto it = settings.begin(); it != settings.end(); ++it) {
                if (!it.key().startsWith(prefix)) {
                    continue;
                }
                bool validIndex = false;
                const int index = it.key().mid(prefix.size()).toInt(&validIndex);
                if (!validIndex || index < 0 || index >= entries.size()) {
                    continue;
                }
                YAML::Node replacement = jsonToYaml(entries.at(index));
                YAML::Node original = root[section.toStdString()][index];
                if (original.IsMap() && replacement.IsMap()) {
                    for (const auto& field : replacement) {
                        original[field.first.as<std::string>()] = field.second;
                    }
                } else {
                    root[section.toStdString()][index] = replacement;
                }
            }
        };
        patchEntries(QStringLiteral("pt1000_"), QStringLiteral("pt1000s"), QStringLiteral("pt1000"));
        patchEntries(QStringLiteral("contactors_"), QStringLiteral("contactors"), QStringLiteral("contactors"));
        patchEntries(QStringLiteral("estops_"), QStringLiteral("estops"), QStringLiteral("estops"));
        return emitSafetyControllerYaml(root);
    } catch (const YAML::Exception& error) {
        qWarning() << "Unable to prepare Safety Controller YAML:" << error.what();
        return {};
    }
}

bool isValidSafetyControllerYaml(const QByteArray& yaml) {
    try {
        const YAML::Node root = YAML::Load(yaml.constData());
        if (!root.IsMap() || !root["version"].IsScalar()) {
            return false;
        }
        for (const char* section : {"pt1000s", "contactors", "estops"}) {
            if (!root[section].IsSequence()) {
                return false;
            }
        }
        return true;
    } catch (const YAML::Exception&) {
        return false;
    }
}

bool invalidateSafetyControllerCache(const QString& path) {
    const QString backup = path + QStringLiteral(".invalid");
    if (QFile::exists(backup) && !QFile::remove(backup)) {
        return false;
    }
    return QFile::rename(path, backup);
}

QString restoreUnchangedSafetyControllerCache(const QString& path) {
    if (!QFile::exists(path) && QFile::rename(path + QStringLiteral(".invalid"), path)) {
        return QStringLiteral("unchanged");
    }
    qWarning() << "Unable to restore unchanged Safety Controller cache:" << path;
    return QStringLiteral("unavailable");
}

QString quoteCommandArgument(const QString& argument) {
    QString escapedArgument = argument;
    escapedArgument.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escapedArgument.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escapedArgument + QStringLiteral("\"");
}

QString quoteShellArgument(const QString& argument) {
    QString escapedArgument = argument;
    escapedArgument.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + escapedArgument + QLatin1Char('\'');
}

QString formatCommand(const QString& program, const QStringList& arguments) {
    QStringList parts{quoteShellArgument(program)};
    for (const QString& argument : arguments) {
        parts.append(quoteShellArgument(argument));
    }
    return parts.join(QLatin1Char(' '));
}

QString raDataFlashCommand(const QString& deviceName, const QString& resetGpioLineName,
                           const QString& bootModeGpioLineName, const QString& binPath) {
    return QStringLiteral("ra-update -a data -d /dev/%1 -r %2 -m %3 flash %4")
        .arg(deviceName, quoteCommandArgument(resetGpioLineName), quoteCommandArgument(bootModeGpioLineName), binPath);
}

namespace {
struct SafetyControllerServiceActions {
    std::function<EverestStateAllowedResult(const QList<int>&)> check = [](const QList<int>& indices) {
        return EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, indices);
    };
    std::function<EverestServiceControlResult()> stop = []() { return EverestServiceControl::executeEverestStop(); };
    std::function<EverestServiceControlResult(const QList<int>&)> restart = [](const QList<int>& indices) {
        return EverestServiceControl::executeEverestRestart(g_rpcApiClient, indices);
    };
    std::function<EverestErrorPresentResult(const QList<int>&, bool)> monitor = [](const QList<int>& indices,
                                                                                   bool error) {
        return EverestServiceControl::monitorEverestErrorPresent(g_rpcApiClient, indices, 10000, error);
    };
};

class SafetyControllerWriteOperation final : public QObject {
public:
    SafetyControllerWriteOperation(const ModuleRequest& request, const SafetyControllerDevice& device,
                                   const QString& yamlPath, const QString& binPath, QByteArray updatedParameters,
                                   QObject* owner, std::function<void(const ModuleResponse&)> completed,
                                   ConsoleConnector::ExecOptions execOptions = {},
                                   SafetyControllerServiceActions serviceActions = {}) :
        QObject(owner),
        m_response{.requestId = request.requestId,
                   .group = QLatin1String(kGroupSafety),
                   .action = request.action,
                   .parameters = {},
                   .success = false,
                   .final = true},
        m_device(device),
        m_yamlPath(yamlPath),
        m_binPath(binPath),
        m_updatedParameters(std::move(updatedParameters)),
        m_completed(std::move(completed)),
        m_execOptions(execOptions),
        m_serviceActions(std::move(serviceActions)) {
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        connect(&m_process, &QProcess::started, this, [this]() {
            m_started = true;
            m_startTimer.stop();
            if (m_processError == QStringLiteral("process_start_timeout")) {
                m_process.kill();
                return;
            }
            m_timeoutMs = m_execOptions.syncTimeoutMs;
            m_runTimer.start(m_timeoutMs);
        });
        connect(&m_process, &QProcess::readyReadStandardOutput, this,
                [this]() { m_stdout.append(m_process.readAllStandardOutput()); });
        connect(&m_process, &QProcess::readyReadStandardError, this,
                [this]() { m_stderr.append(m_process.readAllStandardError()); });
        connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    finishProcess(exitCode, exitStatus == QProcess::NormalExit);
                });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                m_processError = m_process.errorString();
                finishProcess(-1, false);
            }
        });
        m_startTimer.setSingleShot(true);
        m_runTimer.setSingleShot(true);
        m_startTimer.setTimerType(Qt::PreciseTimer);
        m_runTimer.setTimerType(Qt::PreciseTimer);
        connect(&m_startTimer, &QTimer::timeout, this, [this]() {
            m_processError = QStringLiteral("process_start_timeout");
            m_timedOut = true;
            m_process.kill();
            if (m_process.state() == QProcess::NotRunning) {
                finishProcess(-1, false);
            }
        });
        connect(&m_runTimer, &QTimer::timeout, this, [this]() {
            m_processError = QStringLiteral("process_run_timeout");
            m_timedOut = true;
            m_process.kill();
            if (m_process.state() == QProcess::NotRunning) {
                finishProcess(-1, false);
            }
        });
    }

    ModuleResponse start() {
        m_operationElapsed.start();
        logPhase(QStringLiteral("binary_creation"));
        m_step = Step::CreateBinary;
        startProcess(QStringLiteral("ra-pb-create"),
                     {QLatin1String(kCmdFlagI), m_yamlPath, QLatin1String(kCmdFlagO), m_binPath});
        return ModuleResponse{
            .requestId = m_response.requestId,
            .group = m_response.group,
            .action = m_response.action,
            .parameters = {},
            .success = true,
            .final = false,
        };
    }

private:
    void logPhase(const QString& phase) {
        m_phase = phase;
#ifdef EVEREST_UI_RPC_DEBUG
        qInfo().noquote() << QStringLiteral("Safety Controller write: request_id=%1; device='/dev/%2'; "
                                            "phase=%3; elapsed_ms=%4")
                                 .arg(m_response.requestId)
                                 .arg(m_device.name, phase)
                                 .arg(m_operationElapsed.elapsed());
#endif
    }

    enum class Step {
        CreateBinary,
        Flash,
        RecoverDump,
        RecoverYaml
    };

    void startProcess(const QString& program, const QStringList& arguments) {
        m_startTimer.stop();
        m_runTimer.stop();
        m_stdout.clear();
        m_stderr.clear();
        m_processError.clear();
        m_command = formatCommand(program, arguments);
        m_started = false;
        m_timedOut = false;
        m_finished = false;
        m_timeoutMs = m_execOptions.startTimeoutMs;
        m_startTimer.start(m_timeoutMs);
        m_process.start(program, arguments);
    }

    void finishProcess(int exitCode, bool normalExit) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_startTimer.stop();
        m_runTimer.stop();
        m_stdout.append(m_process.readAllStandardOutput());
        m_stderr.append(m_process.readAllStandardError());

        ConsoleConnector::RunResult result{
            .exitCode = exitCode,
            .stdoutData = m_stdout,
            .stderrData = m_stderr,
            .started = m_started,
            .timedOut = m_timedOut,
            .normalExit = normalExit,
            .processError = m_processError,
        };
        if (m_step == Step::CreateBinary) {
            if (!result.started || result.timedOut || !result.normalExit || result.exitCode != 0) {
                qWarning().noquote()
                    << QStringLiteral("Safety Controller binary creation failed: command='%1'; started=%2; "
                                      "timed_out=%3; normal_exit=%4; exit_code=%5; process_error='%6'; stdout='%7'; "
                                      "stderr='%8'; timeout_ms=%9")
                           .arg(m_command)
                           .arg(result.started)
                           .arg(result.timedOut)
                           .arg(result.normalExit)
                           .arg(result.exitCode)
                           .arg(result.processError, QString::fromUtf8(result.stdoutData).trimmed(),
                                QString::fromUtf8(result.stderrData).trimmed())
                           .arg(m_timeoutMs);
                m_response.parameters = {{QLatin1String(kError), QLatin1String(kErrorSafetyControllerPbCreateFailed)}};
                complete();
                return;
            }
            logPhase(QStringLiteral("pre_flash_safety"));
            const EverestStateAllowedResult allowed = m_serviceActions.check(m_device.evseIndices);
            if (!allowed.success) {
                QString error = allowed.error;
                if (allowed.error == QLatin1String(kErrorEverestStateNotAllowed)) {
                    error = QStringLiteral("settings can't be applied because ra-update command cannot be run while "
                                           "EVerest is in state \"%1\" and needs to be stopped first")
                                .arg(allowed.state);
                }
                m_response.parameters = {{QLatin1String(kError), error}};
                complete();
                return;
            }
            logPhase(QStringLiteral("stop_everest"));
            const EverestServiceControlResult stopResult = m_serviceActions.stop();
            if (!stopResult.success) {
                m_response.parameters = {{QLatin1String(kError), stopResult.error}};
                complete();
                return;
            }
            if (!invalidateSafetyControllerCache(m_device.yamlPath)) {
                qWarning() << "Safety Controller cache invalidation failed; flash not started: device="
                           << m_device.name;
                m_response.parameters = {
                    {QLatin1String(kError), QStringLiteral("safety_controller_cache_invalidation_failed")},
                    {QStringLiteral("cache_state"), QStringLiteral("unchanged")},
                };
                restartAfterFlash();
                return;
            }
            m_step = Step::Flash;
            logPhase(QStringLiteral("flash"));
            startProcess(QStringLiteral("ra-update"),
                         {QStringLiteral("-a"), QStringLiteral("data"), QStringLiteral("-d"),
                          QStringLiteral("/dev/") + m_device.name, QStringLiteral("-r"), m_device.resetGpioLineName,
                          QStringLiteral("-m"), m_device.bootModeGpioLineName, QStringLiteral("flash"), m_binPath});
            return;
        }

        if (m_step == Step::RecoverDump || m_step == Step::RecoverYaml) {
            finishRecovery(result);
            return;
        }

        const bool flashSucceeded = result.started && !result.timedOut && result.normalExit && result.exitCode == 0;
        if (!flashSucceeded) {
            qWarning().noquote() << QStringLiteral(
                                        "Safety Controller flash failed: command='%1'; device='/dev/%2'; started=%3; "
                                        "timed_out=%4; normal_exit=%5; exit_code=%6; process_error='%7'; stdout='%8'; "
                                        "stderr='%9'; timeout_ms=%10")
                                        .arg(m_command, m_device.name)
                                        .arg(result.started)
                                        .arg(result.timedOut)
                                        .arg(result.normalExit)
                                        .arg(result.exitCode)
                                        .arg(result.processError, QString::fromUtf8(result.stdoutData).trimmed(),
                                             QString::fromUtf8(result.stderrData).trimmed())
                                        .arg(m_timeoutMs);
        }

        qInfo() << "Safety Controller flash completed: request_id=" << m_response.requestId
                << "device=" << m_device.name << "flash_succeeded=" << flashSucceeded;
        if (!flashSucceeded) {
            QString outcome = QStringLiteral("unexpected_exit");
            if (result.timedOut) {
                outcome = QStringLiteral("timeout");
            } else if (!result.started) {
                outcome = QStringLiteral("start_failed");
            } else if (result.normalExit) {
                outcome = QStringLiteral("nonzero_exit");
            }
            m_response.parameters = {
                {QLatin1String(kError), QLatin1String(kErrorSafetyControllerFlashFailed)},
                {QLatin1String(kKeyDeviceName), m_device.name},
                {QStringLiteral("flash_outcome"), outcome},
                {QStringLiteral("exit_code"), result.exitCode},
                {QStringLiteral("cache_state"), QStringLiteral("unavailable")},
            };
            if (!result.started && !result.timedOut) {
                m_response.parameters.insert(QStringLiteral("cache_state"),
                                             restoreUnchangedSafetyControllerCache(m_device.yamlPath));
                restartAfterFlash();
            } else if (!m_recoveryDirectory.isValid()) {
                recoveryFailed(QStringLiteral("temporary_directory_unavailable"));
            } else {
                m_step = Step::RecoverDump;
                logPhase(QStringLiteral("recover_dump"));
                startProcess(QStringLiteral("ra-update"),
                             {QStringLiteral("-a"), QStringLiteral("data"), QStringLiteral("-d"),
                              QStringLiteral("/dev/") + m_device.name, QStringLiteral("-r"), m_device.resetGpioLineName,
                              QStringLiteral("-m"), m_device.bootModeGpioLineName, QStringLiteral("dump-pb"),
                              recoveryBinaryPath()});
            }
            return;
        }
        if (!writeSafetyControllerYamlFile(m_device.yamlPath, m_updatedParameters)) {
            m_response.parameters = {
                {QLatin1String(kError), QLatin1String(kErrorYamlPublishAfterFlash)},
                {QLatin1String(kKeyDeviceName), m_device.name},
                {QStringLiteral("flash_succeeded"), true},
                {QStringLiteral("cache_state"), QStringLiteral("unavailable")},
                {QLatin1String(kKeyMessage), QStringLiteral("The controller was flashed, but cached settings are "
                                                            "unavailable. Check the backend logs.")},
            };
        } else {
            QFile::remove(m_device.yamlPath + QStringLiteral(".invalid"));
        }
        restartAfterFlash();
    }

    QString recoveryBinaryPath() const {
        return m_recoveryDirectory.filePath(QStringLiteral("parameter-block.bin"));
    }

    void recoveryFailed(const QString& error) {
        qWarning().noquote() << "Safety Controller cache recovery failed: device=" << m_device.name
                             << "phase=" << m_phase << "error=" << error << "command=" << m_command
                             << "started=" << m_started << "timed_out=" << m_timedOut
                             << "process_error=" << m_processError << "stdout=" << m_stdout << "stderr=" << m_stderr;
        m_response.parameters.insert(QStringLiteral("recovery_error"), error);
        restartAfterFlash();
    }

    void finishRecovery(const ConsoleConnector::RunResult& result) {
        if (!result.started || result.timedOut || !result.normalExit || result.exitCode != 0) {
            qWarning() << "Safety Controller readback process failed: exit_code=" << result.exitCode
                       << "normal_exit=" << result.normalExit;
            recoveryFailed(QStringLiteral("safety_controller_cache_readback_failed"));
            return;
        }
        if (m_step == Step::RecoverDump) {
            m_step = Step::RecoverYaml;
            logPhase(QStringLiteral("recover_yaml"));
            startProcess(QStringLiteral("ra-pb-dump"), {recoveryBinaryPath()});
            return;
        }
        if (!isValidSafetyControllerYaml(result.stdoutData)) {
            recoveryFailed(QStringLiteral("safety_controller_cache_readback_invalid"));
            return;
        }
        if (!writeSafetyControllerYamlFile(m_device.yamlPath, result.stdoutData)) {
            recoveryFailed(QStringLiteral("safety_controller_cache_publish_failed"));
            return;
        }
        m_response.parameters.insert(QStringLiteral("cache_state"), QStringLiteral("refreshed"));
        QFile::remove(m_device.yamlPath + QStringLiteral(".invalid"));
        restartAfterFlash();
    }

    void restartAfterFlash() {
        logPhase(QStringLiteral("restart_rpc_readiness"));
        const EverestServiceControlResult restartResult = m_serviceActions.restart(m_device.evseIndices);
        if (!m_response.parameters.isEmpty()) {
            if (!restartResult.success) {
                m_response.parameters.insert(QStringLiteral("restart_error"), restartResult.error);
            }
            complete();
            return;
        }
        if (!restartResult.success) {
            m_response.parameters = {{QLatin1String(kError), restartResult.error}};
            complete();
            return;
        }
        logPhase(QStringLiteral("error_present"));
        const EverestErrorPresentResult errorResult =
            m_serviceActions.monitor(m_device.evseIndices, restartResult.errorPresent);
        if (errorResult.success) {
            m_response.parameters = {
                {QLatin1String(kError), QLatin1String(kMessageEverestErrorAfterRestart)},
                {QLatin1String(kKeyMessage), QLatin1String(kMessageEverestErrorAfterRestart)},
            };
            complete();
            return;
        }
        if (errorResult.error != QLatin1String(kInfoEverestErrorPresentNotDetected)) {
            qWarning().noquote() << QStringLiteral("Safety Controller post-restart EVSE monitor failed: "
                                                   "phase=error_present; error=%1")
                                        .arg(errorResult.error);
            m_response.parameters = {{QLatin1String(kError), errorResult.error}};
            complete();
            return;
        }
        m_response.parameters = {{QLatin1String(kKeyDeviceName), m_device.name}};
        m_response.success = true;
        complete();
        return;
    }

    void complete() {
        qInfo().noquote() << QStringLiteral("Safety Controller write completed: request_id=%1; device='/dev/%2'; "
                                            "phase=%3; success=%4; error=%5; elapsed_ms=%6")
                                 .arg(m_response.requestId)
                                 .arg(m_device.name, m_phase)
                                 .arg(m_response.success)
                                 .arg(m_response.parameters.value(QLatin1String(kError)).toString())
                                 .arg(m_operationElapsed.elapsed());
        if (m_completed) {
            m_completed(m_response);
        }
        deleteLater();
    }

    ModuleResponse m_response;
    SafetyControllerDevice m_device;
    QString m_yamlPath;
    QString m_binPath;
    QString m_command;
    QByteArray m_updatedParameters;
    QTemporaryDir m_recoveryDirectory;
    std::function<void(const ModuleResponse&)> m_completed;
    ConsoleConnector::ExecOptions m_execOptions;
    SafetyControllerServiceActions m_serviceActions;
    QElapsedTimer m_operationElapsed;
    QString m_phase;
    QProcess m_process;
    QTimer m_startTimer;
    QTimer m_runTimer;
    QByteArray m_stdout;
    QByteArray m_stderr;
    QString m_processError;
    Step m_step = Step::CreateBinary;
    int m_timeoutMs = 0;
    bool m_timedOut = false;
    bool m_finished = false;
    bool m_started = false;
};
} // namespace

ModuleResponse handleReadRequest(const ModuleRequest& request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSafety),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const YamlLoadResult configResult = loadEffectiveEverestConfig();
    const bool rpcAvailable = g_rpcApiClient && g_rpcApiClient->isReady();
    QJsonArray controllers;
    QStringList resolutionErrors;
    if (configResult.success) {
        const SafetyControllerDeviceResolution resolution =
            resolveSafetyControllerDevices(configResult.yamlRoot, QStringLiteral("/usr/libexec/everest/modules"));
        resolutionErrors = resolution.errors;
        for (const SafetyControllerDevice& device : resolution.devices) {
            controllers.append(safetyControllerDeviceToJson(device, request.parameters, rpcAvailable));
        }
    } else {
        resolutionErrors.append(configResult.error);
    }

    response.parameters = QJsonObject{
        {QLatin1String(kKeyControllers), controllers},
        {QLatin1String(kKeyRpcAvailable), rpcAvailable},
    };
    if (!resolutionErrors.isEmpty()) {
        response.parameters.insert(QLatin1String(kKeyResolutionError), resolutionErrors.join(QLatin1Char(';')));
    }
    response.success = true;
    return response;
}

ModuleResponse startWriteRequest(const ModuleRequest& request, QObject* owner,
                                 std::function<void(const ModuleResponse&)> completed) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSafety),
        .action = request.action,
        .parameters = {},
        .success = false,
        .final = true,
    };
    const QString deviceName = request.parameters.value(QLatin1String(kKeyDeviceName)).toString();
    if (!isSafeDeviceName(deviceName)) {
        response.parameters = {{QLatin1String(kError), QLatin1String(kErrorDeviceInvalid)}};
        return response;
    }
    if (!validatePt1000Settings(request.parameters)) {
        response.parameters = {
            {QLatin1String(kError), QStringLiteral("safety_controller_pt1000_invalid_settings")},
            {QLatin1String(kKeyMessage), QStringLiteral("Enter an abort temperature and resistance offset as valid "
                                                        "numbers for every enabled PT1000 channel.")},
        };
        return response;
    }
    if (!g_rpcApiClient || !g_rpcApiClient->isReady()) {
        response.parameters = {{QLatin1String(kError), QStringLiteral("rpc_api_not_connected")}};
        return response;
    }

    const YamlLoadResult configResult = loadEffectiveEverestConfig();
    if (!configResult.success) {
        response.parameters = {{QLatin1String(kError), configResult.error}};
        return response;
    }
    const SafetyControllerDeviceResolution resolution =
        resolveSafetyControllerDevices(configResult.yamlRoot, QStringLiteral("/usr/libexec/everest/modules"));
    const auto deviceIt =
        std::find_if(resolution.devices.cbegin(), resolution.devices.cend(),
                     [&deviceName](const SafetyControllerDevice& device) { return device.name == deviceName; });
    if (deviceIt == resolution.devices.cend()) {
        response.parameters = {{QLatin1String(kError), QLatin1String(kErrorDeviceNotConfigured)}};
        return response;
    }
    if (!deviceIt->evseMappingResolved || deviceIt->evseIndices.isEmpty()) {
        response.parameters = {{QLatin1String(kError), QLatin1String(kErrorEvseMappingUnavailable)}};
        return response;
    }
    const YamlLoadResult yamlLoadResult = loadYamlFile(deviceIt->yamlPath);
    if (!yamlLoadResult.success) {
        response.parameters = {{QLatin1String(kError), yamlLoadResult.error}};
        return response;
    }
    const SafetyControllerConfigPathResult binPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsBin));
    if (!binPathResult.success) {
        response.parameters = {{QLatin1String(kError), binPathResult.error}};
        return response;
    }
    const SafetyControllerConfigPathResult yamlPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsYaml));
    if (!yamlPathResult.success) {
        response.parameters = {{QLatin1String(kError), yamlPathResult.error}};
        return response;
    }

    QJsonObject settings = request.parameters;
    settings.remove(QLatin1String(kKeyDeviceName));
    const QByteArray updatedParameters =
        prepareSafetyControllerYaml(deviceIt->yamlPath, settings, yamlLoadResult.yamlRoot);
    if (updatedParameters.isEmpty() || !writeSafetyControllerYamlFile(yamlPathResult.path, updatedParameters)) {
        response.parameters = {{QLatin1String(kError), QLatin1String(kErrorSafetyControllerYamlWriteFailed)}};
        return response;
    }

    auto* operation = new SafetyControllerWriteOperation(request, *deviceIt, yamlPathResult.path, binPathResult.path,
                                                         updatedParameters, owner, std::move(completed));
    return operation->start();
}

ModuleResponse handleRequest(const ModuleRequest& request) {
    switch (toSafetyControllerAction(request.action)) {
    case SafetyControllerAction::ReadSettings:
        return handleReadRequest(request);
    case SafetyControllerAction::WriteSettings:
        return ModuleResponse{
            .requestId = request.requestId,
            .group = QLatin1String(kGroupSafety),
            .action = request.action,
            .parameters = {{QLatin1String(kError), QStringLiteral("safety_controller_write_requires_async_dispatch")}},
            .success = false,
            .final = true,
        };
    case SafetyControllerAction::Unknown:
        throw std::runtime_error("SafetyController::handleRequest got unsupported action");
    }

    throw std::runtime_error("SafetyController::handleRequest reached unreachable code");
}
} // namespace SafetyController
