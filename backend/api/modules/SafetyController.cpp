// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SafetyController.hpp"

#include "BackendConfig.hpp"
#include "ConsoleConnector.hpp"
#include "EverestServiceControl.hpp"
#include "ProtocolSchema.hpp"
#include "RpcApiClient.hpp"
#include "YamlUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QTextStream>

#include <stdexcept>

namespace SafetyController {
namespace {
RpcApiClient *g_rpcApiClient = nullptr;
constexpr char kErrorMissing[] = "_missing";
constexpr char kErrorEverestStateNotAllowed[] = "everest_state_not_allowed";
constexpr char kErrorSafetyControllerDumpFailed[] = "safety_controller_dump_failed";
constexpr char kErrorSafetyControllerPbDumpFailed[] = "safety_controller_pb_dump_failed";
constexpr char kErrorSafetyControllerYamlWriteFailed[] = "safety_controller_yaml_write_failed";
constexpr char kErrorSafetyControllerPbCreateFailed[] = "safety_controller_pb_create_failed";
constexpr char kErrorSafetyControllerFlashFailed[] = "safety_controller_flash_failed";
constexpr char kErrorSafetyControllerYamlMissingReloadRequired[] = "safety_controller_yaml_missing_reload_required";
constexpr char kInfoEverestErrorPresentNotDetected[] = "everest_error_present_not_detected";
constexpr char kErrorStdErr[] = "stderr";
constexpr char kParametersError[] = "error";
constexpr char kParametersPt1000[] = "pt1000_";
constexpr char kParametersContactors[] = "contactors_";
constexpr char kParametersEstops[] = "estops_";
constexpr char kCmdRaDataDump[] = "ra-update -a data dump";
constexpr char kCmdRaDataFlash[] = "ra-update -a data flash";
constexpr char kCmdRaPbDump[] = "ra-pb-dump";
constexpr char kCmdRaPbCreate[] = "ra-pb-create";
constexpr char kCmdFlagI[] = "-i";
constexpr char kCmdFlagO[] = "-o";
constexpr char kCmdBinPath[] = "{bin_path}";
constexpr char kCmdYamlPath[] = "{yaml_path}";
constexpr char kSftyCtrlrParamDisabled[] = "disabled";
constexpr char kSftyCtrlrParamAbortTemp[] = "abort-temperature";
constexpr char kSftyCtrlrParamResistanceOffset[] = "resistance-offset";
constexpr char kSftyCtrlrParamOvertempProtection[] = "overtemperature-protection";
constexpr char kSftyCtrlrParamType[] = "type";
constexpr char kSftyCtrlrParamCloseTime[] = "close-time";
constexpr char kSftyCtrlrParamOpenTime[] = "open-time";
constexpr char kSftyCtrlrParamEnabled[] = "enabled";
constexpr char kSftyCtrlrParamPt1000S[] = "pt1000s";
constexpr char kSftyCtrlrParamContactors[] = "contactors";
constexpr char kSftyCtrlrParamEstops[] = "estops";
constexpr char kUnitCelsius[] = " \u00b0C";
constexpr char kUnitOhm[] = " \u03a9";
constexpr char kUnitMs[] = " ms";
constexpr char kGroupSafety[] = "safety";
constexpr char kConfSafetyControllerSettingsBin[] = "safety_controller_settings_bin";
constexpr char kConfSafetyControllerSettingsYaml[] = "safety_controller_settings_yaml";

SafetyControllerAction toSafetyControllerAction(const QString &action) {
    if (action == QLatin1String(kActionReadSettings)) {
        return SafetyControllerAction::ReadSettings;
    }
    if (action == QLatin1String(kActionWriteSettings)) {
        return SafetyControllerAction::WriteSettings;
    }

    return SafetyControllerAction::Unknown;
}

QString stripUnitSuffix(const QJsonValue &value) {
    const QString text = value.toString().trimmed();
    return text.section(QLatin1Char(' '), 0, 0);
}

QString jsonValueToText(const QJsonValue &value) {
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
} // namespace

void setRpcApiClient(RpcApiClient *rpcApiClient) {
    g_rpcApiClient = rpcApiClient;
}
QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

SafetyControllerConfigPathResult loadSafetyControllerSettingsPath(const QString &configKey) {
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

ModuleResponse readSafetyControllerSettingsAsBin(const QString &binPath, ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QLatin1String(kErrorEverestStateNotAllowed)) {
            error =
                QStringLiteral("settings can't be read because ra-update command cannot be run while EVerest is in state \"%1\" and needs to be stopped first")
                    .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), error},
        };
        return response;
    }

    const EverestServiceControlResult stopResult =
        EverestServiceControl::executeEverestStop();
    if (!stopResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), stopResult.error},
        };
        return response;
    }

    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QLatin1String(kCmdRaDataDump) + QStringLiteral(" ") + QLatin1String(kCmdBinPath),
        {{QLatin1String(kCmdBinPath), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

    const EverestServiceControlResult restartResult =
        EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), restartResult.error},
        };
        return response;
    }

    if (result.exitCode == 0) {
        return response;
    }

    response.parameters = QJsonObject{
        {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerDumpFailed)},
        {QLatin1String(kErrorStdErr), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
}

ModuleResponse convertSafetyControllerBinToYaml(const QString &binPath,
                                                const QString &yamlPath,
                                                ModuleResponse response) {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QLatin1String(kCmdRaPbDump) + QStringLiteral(" ") + QLatin1String(kCmdBinPath),
        {{QLatin1String(kCmdBinPath), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

    if (result.exitCode != 0) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerPbDumpFailed)},
            {QLatin1String(kErrorStdErr), QString::fromUtf8(result.stderrData).trimmed()},
        };
        return response;
    }

    QFile yamlFile(yamlPath);
    if (!yamlFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerYamlWriteFailed)},
        };
        return response;
    }

    if (yamlFile.write(result.stdoutData) < 0) {
        yamlFile.close();
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerYamlWriteFailed)},
        };
        return response;
    }

    yamlFile.close();
    return response;
}

ModuleResponse readSafetyControllerSettingsAsYaml(const QString &binPath,
                                                  const QString &yamlPath,
                                                  ModuleResponse response) {
    response = readSafetyControllerSettingsAsBin(binPath, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    return convertSafetyControllerBinToYaml(binPath, yamlPath, response);
}

QJsonObject readPt1000ParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
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

QJsonObject readContactorParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString() && yamlEntry.toString() == QLatin1String(kSftyCtrlrParamDisabled)) {
        filledBlock.insert(QLatin1String(kSftyCtrlrParamType), QLatin1String(kSftyCtrlrParamDisabled));
        filledBlock.insert(QLatin1String(kSftyCtrlrParamCloseTime), QStringLiteral(""));
        filledBlock.insert(QLatin1String(kSftyCtrlrParamOpenTime), QStringLiteral(""));
        return filledBlock;
    }

    if (!yamlEntry.isObject()) {
        return filledBlock;
    }

    const QJsonObject yamlObject = yamlEntry.toObject();
    filledBlock.insert(QLatin1String(kSftyCtrlrParamType), yamlObject.value(QLatin1String(kSftyCtrlrParamType)));
    filledBlock.insert(QLatin1String(kSftyCtrlrParamCloseTime),
                       stripUnitSuffix(yamlObject.value(QLatin1String(kSftyCtrlrParamCloseTime))));
    filledBlock.insert(QLatin1String(kSftyCtrlrParamOpenTime),
                       stripUnitSuffix(yamlObject.value(QLatin1String(kSftyCtrlrParamOpenTime))));
    return filledBlock;
}

QJsonObject readEstopParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString()) {
        filledBlock.insert(QLatin1String(kSftyCtrlrParamEnabled), yamlEntry.toString());
    }

    return filledBlock;
}

QJsonObject readRequestedParametersFromYaml(const QJsonObject &requestParameters,
                                            const QJsonObject &yamlRoot) {
    QJsonObject filledParameters = requestParameters;
    const QJsonArray pt1000Entries = yamlRoot.value(QLatin1String(kSftyCtrlrParamPt1000S)).toArray();
    const QJsonArray contactorEntries = yamlRoot.value(QLatin1String(kSftyCtrlrParamContactors)).toArray();
    const QJsonArray estopEntries = yamlRoot.value(QLatin1String(kSftyCtrlrParamEstops)).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QLatin1String(kParametersPt1000))) {
            const QString indexString = parameterKey.mid(QLatin1String(kParametersPt1000).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
                filledParameters.insert(parameterKey,
                                        readPt1000ParametersFromYaml(requestBlock, pt1000Entries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersContactors))) {
            const QString indexString =
                parameterKey.mid(QLatin1String(kParametersContactors).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                filledParameters.insert(
                    parameterKey, readContactorParametersFromYaml(requestBlock, contactorEntries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersEstops))) {
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

QJsonValue updatePt1000ParametersInYaml(const QJsonObject &requestBlock) {
    const bool overtemperatureProtection =
        requestBlock.value(QLatin1String(kSftyCtrlrParamOvertempProtection)).toBool();
    if (!overtemperatureProtection) {
        return QLatin1String(kSftyCtrlrParamDisabled);
    }

    return QJsonObject{
        {QLatin1String(kSftyCtrlrParamAbortTemp),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamAbortTemp))) +
             QLatin1String(kUnitCelsius)},
        {QLatin1String(kSftyCtrlrParamResistanceOffset),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamResistanceOffset))) +
             QLatin1String(kUnitOhm)},
    };
}

QJsonValue updateContactorParametersInYaml(const QJsonObject &requestBlock) {
    const QString type = requestBlock.value(QLatin1String(kSftyCtrlrParamType)).toString();
    if (type == QLatin1String(kSftyCtrlrParamDisabled)) {
        return QLatin1String(kSftyCtrlrParamDisabled);
    }

    return QJsonObject{
        {QLatin1String(kSftyCtrlrParamType), type},
        {QLatin1String(kSftyCtrlrParamCloseTime),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamCloseTime))) +
             QLatin1String(kUnitMs)},
        {QLatin1String(kSftyCtrlrParamOpenTime),
         jsonValueToText(requestBlock.value(QLatin1String(kSftyCtrlrParamOpenTime))) +
             QLatin1String(kUnitMs)},
    };
}

QJsonValue updateEstopParametersInYaml(const QJsonObject &requestBlock) {
    return requestBlock.value(QLatin1String(kSftyCtrlrParamEnabled));
}

QJsonObject updateRequestParametersInYaml(const QJsonObject &requestParameters,
                                          const QJsonObject &yamlRoot) {
    QJsonObject updatedYamlRoot = yamlRoot;
    QJsonArray pt1000Entries = updatedYamlRoot.value(QLatin1String(kSftyCtrlrParamPt1000S)).toArray();
    QJsonArray contactorEntries = updatedYamlRoot.value(QLatin1String(kSftyCtrlrParamContactors)).toArray();
    QJsonArray estopEntries = updatedYamlRoot.value(QLatin1String(kSftyCtrlrParamEstops)).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QLatin1String(kParametersPt1000))) {
            const QString indexString = parameterKey.mid(QLatin1String(kParametersPt1000).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
                pt1000Entries.replace(index,
                                      updatePt1000ParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersContactors))) {
            const QString indexString = parameterKey.mid(QLatin1String(kParametersContactors).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                contactorEntries.replace(
                    index, updateContactorParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QLatin1String(kParametersEstops))) {
            const QString indexString = parameterKey.mid(QLatin1String(kParametersEstops).size());
            const int index = indexString.toInt();
            if (index >= 0 && index < estopEntries.size()) {
                estopEntries.replace(index,
                                     updateEstopParametersInYaml(requestBlock));
            }
        }
    }

    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamPt1000S), pt1000Entries);
    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamContactors), contactorEntries);
    updatedYamlRoot.insert(QLatin1String(kSftyCtrlrParamEstops), estopEntries);
    return updatedYamlRoot;
}

bool writeSafetyControllerYamlFile(const QString &yamlPath, const QJsonObject &yamlRoot) {
    QFile yamlFile(yamlPath);
    if (!yamlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&yamlFile);
    stream.setCodec("UTF-8");
    stream << "version: "
           << formatYamlScalar(yamlRoot.value(QStringLiteral("version"))) << "\n\n";

    const auto writeSequence = [&stream](const QString &key,
                                         const QJsonArray &entries,
                                         const QStringList &objectFieldOrder) {
        stream << key << ":\n";
        for (const QJsonValue &entry : entries) {
            if (entry.isObject()) {
                const QJsonObject entryObject = entry.toObject();
                bool firstField = true;
                for (const QString &fieldName : objectFieldOrder) {
                    if (!entryObject.contains(fieldName)) {
                        continue;
                    }

                    stream << (firstField ? QStringLiteral("  - ") : QStringLiteral("    "))
                           << fieldName << ": "
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
    writeSequence(QLatin1String(kSftyCtrlrParamContactors),
                  yamlRoot.value(QLatin1String(kSftyCtrlrParamContactors)).toArray(),
                  {QLatin1String(kSftyCtrlrParamType), QLatin1String(kSftyCtrlrParamCloseTime), QLatin1String(kSftyCtrlrParamOpenTime)});
    writeSequence(QLatin1String(kSftyCtrlrParamEstops),
                  yamlRoot.value(QLatin1String(kSftyCtrlrParamEstops)).toArray(),
                  {});

    return stream.status() == QTextStream::Ok;
}

ModuleResponse convertSafetyControllerYamlToBin(const QString &yamlPath,
                                                const QString &binPath,
                                                ModuleResponse response) {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QLatin1String(kCmdRaPbCreate) + QStringLiteral(" ") + QLatin1String(kCmdFlagI) + QLatin1String(kCmdYamlPath) + QStringLiteral(" ") + QLatin1String(kCmdFlagO) + QLatin1String(kCmdBinPath),
        {
            {QLatin1String(kCmdYamlPath), yamlPath},
            {QLatin1String(kCmdBinPath), binPath},
        },
        options,
        ConsoleConnector::ExecMode::Sync);

    if (result.exitCode == 0) {
        return response;
    }

    response.parameters = QJsonObject{
        {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerPbCreateFailed)},
        {QLatin1String(kErrorStdErr), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
}

ModuleResponse flashSafetyControllerBin(const QString &binPath, ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QLatin1String(kErrorEverestStateNotAllowed)) {
            error =
                QStringLiteral("settings can't be applied because ra-update command cannot be run while EVerest is in state \"%1\" and needs to be stopped first")
                    .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), error},
        };
        return response;
    }

    const EverestServiceControlResult stopResult =
        EverestServiceControl::executeEverestStop();
    if (!stopResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), stopResult.error},
        };
        return response;
    }

    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QLatin1String(kCmdRaDataFlash) + QStringLiteral(" ") + QLatin1String(kCmdBinPath),
        {{QLatin1String(kCmdBinPath), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

    const EverestServiceControlResult restartResult =
        EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), restartResult.error},
        };
        return response;
    }

    if (result.exitCode == 0) {
        const EverestErrorPresentResult errorResult =
            EverestServiceControl::monitorEverestErrorPresent(g_rpcApiClient, 1);
        if (errorResult.success) {
            response.parameters = QJsonObject{
                {QLatin1String(kParametersError),
                 QStringLiteral("settings put EVerest into an error, please revert immediately")},
            };
            return response;
        }

        if (errorResult.error != QLatin1String(kInfoEverestErrorPresentNotDetected)) {
            response.parameters = QJsonObject{
                {QLatin1String(kParametersError), errorResult.error},
            };
            return response;
        }

        return response;
    }

    response.parameters = QJsonObject{
        {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerFlashFailed)},
        {QLatin1String(kErrorStdErr), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSafety),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const SafetyControllerConfigPathResult binPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsBin));
    if (!binPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), binPathResult.error},
        };
        return response;
    }

    const SafetyControllerConfigPathResult yamlPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsYaml));
    if (!yamlPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), yamlPathResult.error},
        };
        return response;
    }

    response = readSafetyControllerSettingsAsYaml(binPathResult.path, yamlPathResult.path, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    const YamlLoadResult yamlLoadResult = loadYamlFile(yamlPathResult.path);
    if (!yamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), yamlLoadResult.error},
        };
        return response;
    }

    response.parameters =
        readRequestedParametersFromYaml(request.parameters, yamlLoadResult.yamlRoot);
    response.success = true;
    return response;
}

ModuleResponse handleWriteRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSafety),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const SafetyControllerConfigPathResult binPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsBin));
    if (!binPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), binPathResult.error},
        };
        return response;
    }

    const SafetyControllerConfigPathResult yamlPathResult =
        loadSafetyControllerSettingsPath(QLatin1String(kConfSafetyControllerSettingsYaml));
    if (!yamlPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), yamlPathResult.error},
        };
        return response;
    }

    const YamlLoadResult yamlLoadResult = loadYamlFile(yamlPathResult.path);
    if (!yamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerYamlMissingReloadRequired)},
        };
        return response;
    }

    QJsonObject updatedParameters =
        updateRequestParametersInYaml(request.parameters, yamlLoadResult.yamlRoot);
    if (!writeSafetyControllerYamlFile(yamlPathResult.path, updatedParameters)) {
        response.parameters = QJsonObject{
            {QLatin1String(kParametersError), QLatin1String(kErrorSafetyControllerYamlWriteFailed)},
        };
        return response;
    }

    response = convertSafetyControllerYamlToBin(yamlPathResult.path, binPathResult.path, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    response = flashSafetyControllerBin(binPathResult.path, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    response.success = true;
    return response;
}

ModuleResponse handleRequest(const ModuleRequest &request) {
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
