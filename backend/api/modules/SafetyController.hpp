// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SAFETY_CONTROLLER_HPP
#define SAFETY_CONTROLLER_HPP

#include "RequestResponseTypes.hpp"

#include <QJsonObject>
#include <QString>

enum class SafetyControllerAction {
    ReadSettings,
    WriteSettings,
    Unknown
};

struct SafetyControllerConfigPathResult {
    bool success = false;
    QString path;
    QString error;
};

namespace SafetyController {
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleWriteRequest(const ModuleRequest &request);
ModuleResponse readSafetyControllerSettingsAsYaml(const QString &binPath,
                                                  const QString &yamlPath,
                                                  ModuleResponse response);
ModuleResponse readSafetyControllerSettingsAsBin(const QString &binPath, ModuleResponse response);
ModuleResponse convertSafetyControllerBinToYaml(const QString &binPath,
                                                const QString &yamlPath,
                                                ModuleResponse response);
SafetyControllerConfigPathResult loadSafetyControllerSettingsPath(const QString &configKey);
QString loadBackendConfigValue(const QString &configKey);
QJsonObject readRequestedParametersFromYaml(const QJsonObject &requestParameters,
                                            const QJsonObject &yamlRoot);
QJsonObject readPt1000ParametersFromYaml(const QJsonObject &requestBlock,
                                         const QJsonValue &yamlEntry);
QJsonObject readContactorParametersFromYaml(const QJsonObject &requestBlock,
                                            const QJsonValue &yamlEntry);
QJsonObject readEstopParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry);
}

#endif // SAFETY_CONTROLLER_HPP
