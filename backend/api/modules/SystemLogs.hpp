// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SYSTEM_LOGS_HPP
#define SYSTEM_LOGS_HPP

#include "RequestResponseTypes.hpp"

enum class SystemLogsAction {
    Read,
    Download,
    Extract,
    Unknown
};

struct SystemLogsConfigPathResult {
    bool success = false;
    QString path;
    QString error;
};

struct SystemLogsReadResult {
    bool success = false;
    QJsonObject parameters;
    QString error;
};

struct SystemLogsDownloadResult {
    bool success = false;
    QJsonObject parameters;
    QString error;
};

namespace SystemLogs {
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleDownloadRequest(const ModuleRequest &request);
ModuleResponse handleExtractRequest(const ModuleRequest &request);
SystemLogsConfigPathResult loadSystemLogsSettingsPath(const QString &configKey);
QString loadBackendConfigValue(const QString &configKey);
SystemLogsReadResult readSystemLogFilesInformation(const QString &systemLogPaths);
SystemLogsDownloadResult createSystemLogsArchive(const QJsonObject &requestParameters);
}

#endif // SYSTEM_LOGS_HPP
