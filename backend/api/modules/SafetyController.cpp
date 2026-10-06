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
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include <algorithm>
#include <stdexcept>

namespace SafetyController {
QJsonObject readRequestedParametersFromYaml(const QJsonObject& requestParameters, const QJsonObject& yamlRoot);
namespace {
RpcApiClient* g_rpcApiClient = nullptr;
constexpr char kModuleEvseManager[] = "EvseManager";
constexpr char kErrorSafetyControllerYamlWriteFailed[] = "safety_controller_yaml_write_failed";
constexpr char kErrorSafetyControllerPbCreateFailed[] = "safety_controller_pb_create_failed";
constexpr char kErrorSafetyControllerFlashFailed[] = "safety_controller_flash_failed";
constexpr char kErrorStdErr[] = "stderr";
constexpr char kParametersPt1000[] = "pt1000_";
constexpr char kParametersContactors[] = "contactors_";
constexpr char kParametersEstops[] = "estops_";
constexpr char kCmdRaPbCreate[] = "ra-pb-create";
constexpr char kCmdFlagI[] = "-i";
constexpr char kCmdFlagO[] = "-o";
constexpr char kCmdBinPath[] = "{bin_path}";
constexpr char kCmdYamlPath[] = "{yaml_path}";
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
constexpr char kErrorYamlPublishAfterFlash[] = "safety_controller_yaml_publish_failed_after_flash";

struct SafetyControllerDevice {
    QString name;
    QString resetGpioLineName;
    QString bootModeGpioLineName;
    QString bspInstance;
    QString driverModule;
    QString yamlPath;
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

    for (const QString& instanceName : activeModules.keys()) {
        const QJsonObject evseModule = activeModules.value(instanceName).toObject();
        if (evseModule.value(QLatin1String(kEverestConfModule)).toString() != QLatin1String(kModuleEvseManager)) {
            continue;
        }
        foundEvseManager = true;
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

            const bool alreadyResolved =
                std::any_of(resolution.devices.cbegin(), resolution.devices.cend(),
                            [&deviceName](const SafetyControllerDevice& device) { return device.name == deviceName; });
            if (alreadyResolved) {
                continue;
            }
            resolution.devices.append(
                {deviceName, resetGpioLineName, bootModeGpioLineName, bspInstance, driverModule,
                 QDir(QLatin1String(kCacheDirectory)).filePath(deviceName + QStringLiteral(".yaml"))});
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
        controller.insert(QLatin1String(kKeyMessage),
                          QStringLiteral("Safety Controller settings are unavailable at %1").arg(device.yamlPath));
        return controller;
    }

    controller.insert(QLatin1String(kKeyAvailable), true);
    controller.insert(QLatin1String(kKeyWritable), rpcAvailable);
    controller.insert(QLatin1String(kKeySettings),
                      readRequestedParametersFromYaml(requestedParameters, yamlResult.yamlRoot));
    if (!rpcAvailable) {
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
        filledBlock.insert(QLatin1String(kSftyCtrlrParamAbortTemp), QStringLiteral(""));
        filledBlock.insert(QLatin1String(kSftyCtrlrParamResistanceOffset), QStringLiteral(""));
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

QJsonObject updateRequestParametersInYaml(const QJsonObject& requestParameters, const QJsonObject& yamlRoot) {
    QJsonObject updatedYamlRoot = yamlRoot;
    const QStringList availableSettings = loadAvailableSafetyControllerSettings();
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
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
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
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                contactorEntries.replace(index, updateContactorParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersEstops))) {
            if (!isSafetyControllerSettingAvailable(availableSettings, QLatin1String(kSafetyControllerSettingEstops))) {
                continue;
            }

            const QString indexString = parameterKey.mid(QLatin1String(kParametersEstops).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < estopEntries.size()) {
                estopEntries.replace(index, updateEstopParametersInYaml(requestBlock));
            }
        }
    }

    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamPt1000S), pt1000Entries);
    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamContactors), contactorEntries);
    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamEstops), estopEntries);
    return updatedYamlRoot;
}

bool writeSafetyControllerYamlFile(const QString& yamlPath, const QJsonObject& yamlRoot) {
    QSaveFile yamlFile(yamlPath);
    if (!yamlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&yamlFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
    stream << "version: " << formatYamlScalar(yamlRoot.value(QStringLiteral("version"))) << "\n\n";

    const auto writeSequence = [&stream](const QString& key, const QJsonArray& entries,
                                         const QStringList& objectFieldOrder) {
        stream << key << ":\n";
        for (const QJsonValue& entry : entries) {
            if (entry.isObject()) {
                const QJsonObject entryObject = entry.toObject();
                bool firstField = true;
                for (const QString& fieldName : objectFieldOrder) {
                    if (!entryObject.contains(fieldName)) {
                        continue;
                    }

                    stream << (firstField ? QStringLiteral("  - ") : QStringLiteral("    ")) << fieldName << ": "
                           << formatYamlScalar(entryObject.value(fieldName)) << "\n";
                    firstField = false;
                }
                continue;
            }

            stream << "  - " << formatYamlScalar(entry) << "\n";
        }
    };

    writeSequence(QLatin1String(kSftyCtrlrParamPt1000S),
                  yamlRoot.value(QLatin1String(kSftyCtrlrParamPt1000S)).toArray(),
                  {QLatin1String(kSftyCtrlrParamAbortTemp), QLatin1String(kSftyCtrlrParamResistanceOffset)});
    writeSequence(
        QLatin1String(kSftyCtrlrParamContactors), yamlRoot.value(QLatin1String(kSftyCtrlrParamContactors)).toArray(),
        {QLatin1String(kKeyType), QLatin1String(kSftyCtrlrParamCloseTime), QLatin1String(kSftyCtrlrParamOpenTime)});
    writeSequence(QLatin1String(kSftyCtrlrParamEstops), yamlRoot.value(QLatin1String(kSftyCtrlrParamEstops)).toArray(),
                  {});

    return stream.status() == QTextStream::Ok && yamlFile.commit();
}

ModuleResponse convertSafetyControllerYamlToBin(const QString& yamlPath, const QString& binPath,
                                                ModuleResponse response) {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;

    const auto command = QLatin1String(kCmdRaPbCreate) + QStringLiteral(" ") + QLatin1String(kCmdFlagI) +
                         QStringLiteral(" ") + QLatin1String(kCmdYamlPath) + QStringLiteral(" ") +
                         QLatin1String(kCmdFlagO) + QStringLiteral(" ") + QLatin1String(kCmdBinPath);
    const ConsoleConnector::RunResult result = console.executeTemplate(command,
                                                                       {
                                                                           {QLatin1String(kCmdYamlPath), yamlPath},
                                                                           {QLatin1String(kCmdBinPath), binPath},
                                                                       },
                                                                       options, ConsoleConnector::ExecMode::Sync);

    if (result.exitCode == 0) {
        return response;
    }

    response.parameters = QJsonObject{
        {QLatin1String(kError), QLatin1String(kErrorSafetyControllerPbCreateFailed)},
        {QLatin1String(kErrorStdErr), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
}

QString quoteCommandArgument(const QString& argument) {
    QString escapedArgument = argument;
    escapedArgument.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escapedArgument.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escapedArgument + QStringLiteral("\"");
}

QString raDataFlashCommand(const QString& deviceName, const QString& resetGpioLineName,
                           const QString& bootModeGpioLineName, const QString& binPath) {
    return QStringLiteral("ra-update -a data -d /dev/%1 -r %2 -m %3 flash %4")
        .arg(deviceName, quoteCommandArgument(resetGpioLineName), quoteCommandArgument(bootModeGpioLineName), binPath);
}

ModuleResponse flashSafetyControllerBin(const QString& binPath, const QString& deviceName,
                                        const QString& resetGpioLineName, const QString& bootModeGpioLineName,
                                        ModuleResponse response, bool& flashSucceeded) {
    flashSucceeded = false;
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QLatin1String(kErrorEverestStateNotAllowed)) {
            error = QStringLiteral("settings can't be applied because ra-update "
                                   "command cannot be run while EVerest is in state "
                                   "\"%1\" and needs to be stopped first")
                        .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QLatin1String(kError), error},
        };
        return response;
    }

    const EverestServiceControlResult stopResult = EverestServiceControl::executeEverestStop();
    if (!stopResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), stopResult.error},
        };
        return response;
    }

    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result =
        console.executeTemplate(raDataFlashCommand(deviceName, resetGpioLineName, bootModeGpioLineName, binPath), {},
                                options, ConsoleConnector::ExecMode::Sync);

    flashSucceeded = result.started && !result.timedOut && result.normalExit && result.exitCode == 0;
    if (!flashSucceeded) {
        qWarning().noquote()
            << QStringLiteral("Safety Controller flash failed: command='%1'; device='/dev/%2'; "
                              "started=%3; timed_out=%4; normal_exit=%5; exit_code=%6; process_error='%7'; "
                              "stdout='%8'; stderr='%9'")
                   .arg(raDataFlashCommand(deviceName, resetGpioLineName, bootModeGpioLineName, binPath), deviceName)
                   .arg(result.started)
                   .arg(result.timedOut)
                   .arg(result.normalExit)
                   .arg(result.exitCode)
                   .arg(result.processError, QString::fromUtf8(result.stdoutData).trimmed(),
                        QString::fromUtf8(result.stderrData).trimmed());
    }

    const EverestServiceControlResult restartResult = EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), restartResult.error},
        };
        return response;
    }

    if (result.exitCode == 0) {
        const EverestErrorPresentResult errorResult =
            EverestServiceControl::monitorEverestErrorPresent(g_rpcApiClient, 1);
        if (errorResult.success) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QStringLiteral("settings put EVerest into an error, please revert "
                                                       "immediately")},
            };
            return response;
        }

        if (errorResult.error != QLatin1String(kInfoEverestErrorPresentNotDetected)) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), errorResult.error},
            };
            return response;
        }

        return response;
    }

    QString flashOutcome = QStringLiteral("unexpected_exit");
    if (!result.started) {
        flashOutcome = QStringLiteral("start_failed");
    } else if (result.timedOut) {
        flashOutcome = QStringLiteral("timeout");
    } else if (result.normalExit) {
        flashOutcome = QStringLiteral("nonzero_exit");
    }

    response.parameters = QJsonObject{
        {QLatin1String(kError), QLatin1String(kErrorSafetyControllerFlashFailed)},
        {QStringLiteral("flash_outcome"), flashOutcome},
        {QStringLiteral("exit_code"), result.exitCode},
    };
    return response;
}

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

ModuleResponse handleWriteRequest(const ModuleRequest& request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSafety),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const QString deviceName = request.parameters.value(QLatin1String(kKeyDeviceName)).toString();
    if (!isSafeDeviceName(deviceName)) {
        response.parameters = {{QLatin1String(kError), QLatin1String(kErrorDeviceInvalid)}};
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

    const YamlLoadResult yamlLoadResult = loadYamlFile(deviceIt->yamlPath);
    if (!yamlLoadResult.success) {
        response.parameters = {{QLatin1String(kError), yamlLoadResult.error}};
        return response;
    }

    const SafetyControllerConfigPathResult binPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsBin));
    if (!binPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), binPathResult.error},
        };
        return response;
    }

    const SafetyControllerConfigPathResult yamlPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsYaml));
    if (!yamlPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), yamlPathResult.error},
        };
        return response;
    }

    QJsonObject settings = request.parameters;
    settings.remove(QLatin1String(kKeyDeviceName));
    QJsonObject updatedParameters = updateRequestParametersInYaml(settings, yamlLoadResult.yamlRoot);
    if (!writeSafetyControllerYamlFile(yamlPathResult.path, updatedParameters)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorSafetyControllerYamlWriteFailed)},
        };
        return response;
    }

    response = convertSafetyControllerYamlToBin(yamlPathResult.path, binPathResult.path, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    bool flashSucceeded = false;
    response = flashSafetyControllerBin(binPathResult.path, deviceName, deviceIt->resetGpioLineName,
                                        deviceIt->bootModeGpioLineName, response, flashSucceeded);
    if (flashSucceeded && !writeSafetyControllerYamlFile(deviceIt->yamlPath, updatedParameters)) {
        const QJsonObject flashResponseParameters = response.parameters;
        response.parameters = {
            {QLatin1String(kError), QLatin1String(kErrorYamlPublishAfterFlash)},
            {QLatin1String(kKeyDeviceName), deviceName},
            {QStringLiteral("flash_succeeded"), true},
            {QStringLiteral("message"), QStringLiteral("The controller was flashed, but its cached YAML could "
                                                       "not be updated at %1.")
                                            .arg(deviceIt->yamlPath)},
        };
        if (!flashResponseParameters.isEmpty()) {
            response.parameters.insert(QStringLiteral("post_flash_error"), flashResponseParameters);
        }
        return response;
    }
    if (!response.parameters.isEmpty()) {
        return response;
    }

    response.parameters = {{QLatin1String(kKeyDeviceName), deviceName}};
    response.success = true;
    return response;
}

ModuleResponse handleRequest(const ModuleRequest& request) {
    switch (toSafetyControllerAction(request.action)) {
    case SafetyControllerAction::ReadSettings:
        return handleReadRequest(request);
    case SafetyControllerAction::WriteSettings:
        return handleWriteRequest(request);
    case SafetyControllerAction::Unknown:
        throw std::runtime_error("SafetyController::handleRequest got unsupported action");
    }

    throw std::runtime_error("SafetyController::handleRequest reached unreachable code");
}
} // namespace SafetyController
