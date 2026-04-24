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

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("safety"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    const SafetyControllerConfigPathResult binPathResult =
        loadSafetyControllerSettingsPath(QStringLiteral("safety_controller_settings_bin"));
    if (!binPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), binPathResult.error},
        };
        return response;
    }

    const SafetyControllerConfigPathResult yamlPathResult =
        loadSafetyControllerSettingsPath(QStringLiteral("safety_controller_settings_yaml"));
    if (!yamlPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), yamlPathResult.error},
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
            {QStringLiteral("error"), yamlLoadResult.error},
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
        .group = QStringLiteral("safety"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    const SafetyControllerConfigPathResult binPathResult =
        loadSafetyControllerSettingsPath(QStringLiteral("safety_controller_settings_bin"));
    if (!binPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), binPathResult.error},
        };
        return response;
    }

    const SafetyControllerConfigPathResult yamlPathResult =
        loadSafetyControllerSettingsPath(QStringLiteral("safety_controller_settings_yaml"));
    if (!yamlPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), yamlPathResult.error},
        };
        return response;
    }

    const YamlLoadResult yamlLoadResult = loadYamlFile(yamlPathResult.path);
    if (!yamlLoadResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("safety_controller_yaml_missing_reload_required")},
        };
        return response;
    }

    QJsonObject updatedParameters =
        updateRequestParametersInYaml(request.parameters, yamlLoadResult.yamlRoot);
    if (!writeSafetyControllerYamlFile(yamlPathResult.path, updatedParameters)) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("safety_controller_yaml_write_failed")},
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

ModuleResponse readSafetyControllerSettingsAsYaml(const QString &binPath,
                                                  const QString &yamlPath,
                                                  ModuleResponse response) {
    response = readSafetyControllerSettingsAsBin(binPath, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    return convertSafetyControllerBinToYaml(binPath, yamlPath, response);
}

ModuleResponse readSafetyControllerSettingsAsBin(const QString &binPath, ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QStringLiteral("everest_state_not_allowed")) {
            error =
                QStringLiteral("settings can't be read because ra-update command cannot be run while EVerest is in state \"%1\" and needs to be stopped first")
                    .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QStringLiteral("error"), error},
        };
        return response;
    }

    const EverestServiceControlResult stopResult =
        EverestServiceControl::executeEverestStop();
    if (!stopResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), stopResult.error},
        };
        return response;
    }

    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QStringLiteral("ra-update -a data dump {bin_path}"),
        {{QStringLiteral("{bin_path}"), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

    const EverestServiceControlResult restartResult =
        EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), restartResult.error},
        };
        return response;
    }

    if (result.exitCode == 0) {
        return response;
    }

    response.parameters = QJsonObject{
        {QStringLiteral("error"), QStringLiteral("safety_controller_dump_failed")},
        {QStringLiteral("stderr"), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
}

ModuleResponse convertSafetyControllerBinToYaml(const QString &binPath,
                                                const QString &yamlPath,
                                                ModuleResponse response) {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QStringLiteral("ra-pb-dump {bin_path}"),
        {{QStringLiteral("{bin_path}"), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

    if (result.exitCode != 0) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("safety_controller_pb_dump_failed")},
            {QStringLiteral("stderr"), QString::fromUtf8(result.stderrData).trimmed()},
        };
        return response;
    }

    QFile yamlFile(yamlPath);
    if (!yamlFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("safety_controller_yaml_write_failed")},
        };
        return response;
    }

    if (yamlFile.write(result.stdoutData) < 0) {
        yamlFile.close();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QStringLiteral("safety_controller_yaml_write_failed")},
        };
        return response;
    }

    yamlFile.close();
    return response;
}

ModuleResponse convertSafetyControllerYamlToBin(const QString &yamlPath,
                                                const QString &binPath,
                                                ModuleResponse response) {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QStringLiteral("ra-pb-create -i {yaml_path} -o {bin_path}"),
        {
            {QStringLiteral("{yaml_path}"), yamlPath},
            {QStringLiteral("{bin_path}"), binPath},
        },
        options,
        ConsoleConnector::ExecMode::Sync);

    if (result.exitCode == 0) {
        return response;
    }

    response.parameters = QJsonObject{
        {QStringLiteral("error"), QStringLiteral("safety_controller_pb_create_failed")},
        {QStringLiteral("stderr"), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
}

ModuleResponse flashSafetyControllerBin(const QString &binPath, ModuleResponse response) {
    const EverestStateAllowedResult stateAllowedResult =
        EverestServiceControl::checkEverestStateAllowed(g_rpcApiClient, 1);
    if (!stateAllowedResult.success) {
        QString error = stateAllowedResult.error;
        if (stateAllowedResult.error == QStringLiteral("everest_state_not_allowed")) {
            error =
                QStringLiteral("settings can't be applied because ra-update command cannot be run while EVerest is in state \"%1\" and needs to be stopped first")
                    .arg(stateAllowedResult.state);
        }

        response.parameters = QJsonObject{
            {QStringLiteral("error"), error},
        };
        return response;
    }

    const EverestServiceControlResult stopResult =
        EverestServiceControl::executeEverestStop();
    if (!stopResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), stopResult.error},
        };
        return response;
    }

    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QStringLiteral("ra-update -a data flash {bin_path}"),
        {{QStringLiteral("{bin_path}"), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

    const EverestServiceControlResult restartResult =
        EverestServiceControl::executeEverestRestart(g_rpcApiClient);
    if (!restartResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), restartResult.error},
        };
        return response;
    }

    if (result.exitCode == 0) {
        const EverestErrorPresentResult errorResult =
            EverestServiceControl::monitorEverestErrorPresent(g_rpcApiClient, 1);
        if (errorResult.success) {
            response.parameters = QJsonObject{
                {QStringLiteral("error"),
                 QStringLiteral("settings can't be applied because the safety controller configuration put EVerest into an error state")},
            };
            return response;
        }

        if (errorResult.error != QStringLiteral("everest_error_present_not_detected")) {
            response.parameters = QJsonObject{
                {QStringLiteral("error"), errorResult.error},
            };
            return response;
        }

        return response;
    }

    response.parameters = QJsonObject{
        {QStringLiteral("error"), QStringLiteral("safety_controller_flash_failed")},
        {QStringLiteral("stderr"), QString::fromUtf8(result.stderrData).trimmed()},
    };
    return response;
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
        .error = configKey + QStringLiteral("_missing"),
    };
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

QJsonObject readRequestedParametersFromYaml(const QJsonObject &requestParameters,
                                            const QJsonObject &yamlRoot) {
    QJsonObject filledParameters = requestParameters;
    const QJsonArray pt1000Entries = yamlRoot.value(QStringLiteral("pt1000s")).toArray();
    const QJsonArray contactorEntries = yamlRoot.value(QStringLiteral("contactors")).toArray();
    const QJsonArray estopEntries = yamlRoot.value(QStringLiteral("estops")).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QStringLiteral("pt1000_"))) {
            const QString indexString = parameterKey.mid(QStringLiteral("pt1000_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
                filledParameters.insert(parameterKey,
                                        readPt1000ParametersFromYaml(requestBlock, pt1000Entries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QStringLiteral("contactors_"))) {
            const QString indexString =
                parameterKey.mid(QStringLiteral("contactors_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                filledParameters.insert(
                    parameterKey, readContactorParametersFromYaml(requestBlock, contactorEntries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QStringLiteral("estops_"))) {
            const QString indexString = parameterKey.mid(QStringLiteral("estops_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < estopEntries.size()) {
                filledParameters.insert(parameterKey,
                                        readEstopParametersFromYaml(requestBlock, estopEntries.at(index)));
            }
        }
    }

    return filledParameters;
}

QJsonObject updateRequestParametersInYaml(const QJsonObject &requestParameters,
                                          const QJsonObject &yamlRoot) {
    QJsonObject updatedYamlRoot = yamlRoot;
    QJsonArray pt1000Entries = updatedYamlRoot.value(QStringLiteral("pt1000s")).toArray();
    QJsonArray contactorEntries = updatedYamlRoot.value(QStringLiteral("contactors")).toArray();
    QJsonArray estopEntries = updatedYamlRoot.value(QStringLiteral("estops")).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QStringLiteral("pt1000_"))) {
            const QString indexString = parameterKey.mid(QStringLiteral("pt1000_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
                pt1000Entries.replace(index,
                                      updatePt1000ParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QStringLiteral("contactors_"))) {
            const QString indexString = parameterKey.mid(QStringLiteral("contactors_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                contactorEntries.replace(
                    index, updateContactorParametersInYaml(requestBlock));
            }
            continue;
        }

        if (parameterKey.startsWith(QStringLiteral("estops_"))) {
            const QString indexString = parameterKey.mid(QStringLiteral("estops_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < estopEntries.size()) {
                estopEntries.replace(index,
                                     updateEstopParametersInYaml(requestBlock));
            }
        }
    }

    updatedYamlRoot.insert(QStringLiteral("pt1000s"), pt1000Entries);
    updatedYamlRoot.insert(QStringLiteral("contactors"), contactorEntries);
    updatedYamlRoot.insert(QStringLiteral("estops"), estopEntries);
    return updatedYamlRoot;
}

QJsonObject readPt1000ParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString() && yamlEntry.toString() == QStringLiteral("disabled")) {
        filledBlock.insert(QStringLiteral("abort-temperature"), QStringLiteral(""));
        filledBlock.insert(QStringLiteral("resistance-offset"), QStringLiteral(""));
        filledBlock.insert(QStringLiteral("overtemperature-protection"), false);
        return filledBlock;
    }

    if (!yamlEntry.isObject()) {
        return filledBlock;
    }

    const QJsonObject yamlObject = yamlEntry.toObject();
    filledBlock.insert(QStringLiteral("abort-temperature"),
                       stripUnitSuffix(yamlObject.value(QStringLiteral("abort-temperature"))));
    filledBlock.insert(QStringLiteral("resistance-offset"),
                       stripUnitSuffix(yamlObject.value(QStringLiteral("resistance-offset"))));
    filledBlock.insert(QStringLiteral("overtemperature-protection"), true);
    return filledBlock;
}

QJsonObject readContactorParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString() && yamlEntry.toString() == QStringLiteral("disabled")) {
        filledBlock.insert(QStringLiteral("type"), QStringLiteral("disabled"));
        filledBlock.insert(QStringLiteral("close-time"), QStringLiteral(""));
        filledBlock.insert(QStringLiteral("open-time"), QStringLiteral(""));
        return filledBlock;
    }

    if (!yamlEntry.isObject()) {
        return filledBlock;
    }

    const QJsonObject yamlObject = yamlEntry.toObject();
    filledBlock.insert(QStringLiteral("type"), yamlObject.value(QStringLiteral("type")));
    filledBlock.insert(QStringLiteral("close-time"),
                       stripUnitSuffix(yamlObject.value(QStringLiteral("close-time"))));
    filledBlock.insert(QStringLiteral("open-time"),
                       stripUnitSuffix(yamlObject.value(QStringLiteral("open-time"))));
    return filledBlock;
}

QJsonObject readEstopParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString()) {
        filledBlock.insert(QStringLiteral("enabled"), yamlEntry.toString());
    }

    return filledBlock;
}

QJsonValue updatePt1000ParametersInYaml(const QJsonObject &requestBlock) {
    const bool overtemperatureProtection =
        requestBlock.value(QStringLiteral("overtemperature-protection")).toBool();
    if (!overtemperatureProtection) {
        return QStringLiteral("disabled");
    }

    return QJsonObject{
        {QStringLiteral("abort-temperature"),
         jsonValueToText(requestBlock.value(QStringLiteral("abort-temperature"))) +
             QStringLiteral(" \u00b0C")},
        {QStringLiteral("resistance-offset"),
         jsonValueToText(requestBlock.value(QStringLiteral("resistance-offset"))) +
             QStringLiteral(" \u03a9")},
    };
}

QJsonValue updateContactorParametersInYaml(const QJsonObject &requestBlock) {
    const QString type = requestBlock.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("disabled")) {
        return QStringLiteral("disabled");
    }

    return QJsonObject{
        {QStringLiteral("type"), type},
        {QStringLiteral("close-time"),
         jsonValueToText(requestBlock.value(QStringLiteral("close-time"))) +
             QStringLiteral(" ms")},
        {QStringLiteral("open-time"),
         jsonValueToText(requestBlock.value(QStringLiteral("open-time"))) +
             QStringLiteral(" ms")},
    };
}

QJsonValue updateEstopParametersInYaml(const QJsonObject &requestBlock) {
    return requestBlock.value(QStringLiteral("enabled"));
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

    writeSequence(QStringLiteral("pt1000s"),
                  yamlRoot.value(QStringLiteral("pt1000s")).toArray(),
                  {QStringLiteral("abort-temperature"), QStringLiteral("resistance-offset")});
    writeSequence(QStringLiteral("contactors"),
                  yamlRoot.value(QStringLiteral("contactors")).toArray(),
                  {QStringLiteral("type"), QStringLiteral("close-time"), QStringLiteral("open-time")});
    writeSequence(QStringLiteral("estops"),
                  yamlRoot.value(QStringLiteral("estops")).toArray(),
                  {});

    return stream.status() == QTextStream::Ok;
}
} // namespace SafetyController
