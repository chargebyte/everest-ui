// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SAFETY_CONTROLLER_HPP
#define SAFETY_CONTROLLER_HPP

#include "RequestResponseTypes.hpp"

#include <QJsonArray>
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
ModuleResponse convertSafetyControllerYamlToBin(const QString &yamlPath,
                                                const QString &binPath,
                                                ModuleResponse response);
ModuleResponse flashSafetyControllerBin(const QString &binPath, ModuleResponse response);
SafetyControllerConfigPathResult loadSafetyControllerSettingsPath(const QString &configKey);
QString loadBackendConfigValue(const QString &configKey);
QJsonObject readRequestedParametersFromYaml(const QJsonObject &requestParameters,
                                            const QJsonObject &yamlRoot);
QJsonObject updateRequestParametersInYaml(const QJsonObject &requestParameters,
                                          const QJsonObject &yamlRoot);
QJsonObject readPt1000ParametersFromYaml(const QJsonObject &requestBlock,
                                         const QJsonValue &yamlEntry);
QJsonObject readContactorParametersFromYaml(const QJsonObject &requestBlock,
                                            const QJsonValue &yamlEntry);
QJsonObject readEstopParametersFromYaml(const QJsonObject &requestBlock, const QJsonValue &yamlEntry);
QJsonValue updatePt1000ParametersInYaml(const QJsonObject &requestBlock);
QJsonValue updateContactorParametersInYaml(const QJsonObject &requestBlock);
QJsonValue updateEstopParametersInYaml(const QJsonObject &requestBlock);
bool writeSafetyControllerYamlFile(const QString &yamlPath, const QJsonObject &yamlRoot);
}

#endif // SAFETY_CONTROLLER_HPP
