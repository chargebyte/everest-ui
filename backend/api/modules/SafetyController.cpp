// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SafetyController.hpp"

#include "BackendConfig.hpp"
#include "ConsoleConnector.hpp"
#include "ProtocolSchema.hpp"
#include "YamlUtils.hpp"

#include <QFile>
#include <QJsonArray>

#include <stdexcept>

namespace SafetyController {
namespace {
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
} // namespace

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

    response = ensureSafetyControllerSettingsYaml(binPathResult.path, yamlPathResult.path, response);
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
        fillRequestedReadParameters(request.parameters, yamlLoadResult.yamlRoot);
    response.success = true;
    return response;
}

ModuleResponse handleWriteRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("safety"),
        .action = request.action,
        .parameters = request.parameters,
        .success = false,
    };
}

ModuleResponse ensureSafetyControllerSettingsYaml(const QString &binPath,
                                                 const QString &yamlPath,
                                                 ModuleResponse response) {
    response = dumpSafetyControllerSettingsBin(binPath, response);
    if (!response.parameters.isEmpty()) {
        return response;
    }

    return convertSafetyControllerBinToYaml(binPath, yamlPath, response);
}

ModuleResponse dumpSafetyControllerSettingsBin(const QString &binPath, ModuleResponse response) {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QStringLiteral("ra-update -a data dump {bin_path}"),
        {{QStringLiteral("{bin_path}"), binPath}},
        options,
        ConsoleConnector::ExecMode::Sync);

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

QJsonObject fillRequestedReadParameters(const QJsonObject &requestParameters,
                                        const QJsonObject &yamlRoot) {
    QJsonObject filledParameters = requestParameters;
    const QJsonArray pt1000Entries = yamlRoot.value(QStringLiteral("pt1000s")).toArray();
    const QJsonArray contactorEntries = yamlRoot.value(QStringLiteral("contactors")).toArray();
    const QJsonArray estopEntries = yamlRoot.value(QStringLiteral("estops")).toArray();

    const auto parameterKeys = requestParameters.keys();
    for (const QString &parameterKey : parameterKeys) {
        const QJsonObject requestBlock = requestParameters.value(parameterKey).toObject();
        if (parameterKey.startsWith(QStringLiteral("pt1000_"))) {
            const QString indexString = parameterKey.sliced(QStringLiteral("pt1000_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < pt1000Entries.size()) {
                filledParameters.insert(parameterKey,
                                        fillPt1000Parameters(requestBlock, pt1000Entries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QStringLiteral("contactors_"))) {
            const QString indexString =
                parameterKey.sliced(QStringLiteral("contactors_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < contactorEntries.size()) {
                filledParameters.insert(
                    parameterKey, fillContactorParameters(requestBlock, contactorEntries.at(index)));
            }
            continue;
        }

        if (parameterKey.startsWith(QStringLiteral("estops_"))) {
            const QString indexString = parameterKey.sliced(QStringLiteral("estops_").size());
            const int index = indexString.toInt();
            if (index >= 0 && index < estopEntries.size()) {
                filledParameters.insert(parameterKey,
                                        fillEstopParameters(requestBlock, estopEntries.at(index)));
            }
        }
    }

    return filledParameters;
}

QJsonObject fillPt1000Parameters(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
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

QJsonObject fillContactorParameters(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
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

QJsonObject fillEstopParameters(const QJsonObject &requestBlock, const QJsonValue &yamlEntry) {
    QJsonObject filledBlock = requestBlock;

    if (yamlEntry.isString()) {
        filledBlock.insert(QStringLiteral("enabled"), yamlEntry.toString());
    }

    return filledBlock;
}
} // namespace SafetyController
