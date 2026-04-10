// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef REQUEST_RESPONSE_TYPES_HPP
#define REQUEST_RESPONSE_TYPES_HPP

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

enum class ModuleGroup {
    EverestConfig,
    SafetyController,
    OCPPConfig,
    FirmwareUpdate,
    Logs,
    Unknown
};

struct ModuleRequest {
    qint64 requestId = 0;
    ModuleGroup group = ModuleGroup::Unknown;
    QString action;
    QJsonObject parameters;
};

struct ModuleResponse {
    qint64 requestId = 0;
    QString group;
    QString action;
    QJsonObject parameters;
    bool success = false;
};

#endif // REQUEST_RESPONSE_TYPES_HPP
