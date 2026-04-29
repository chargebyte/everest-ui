// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef LOGS_HPP
#define LOGS_HPP

#include "RequestResponseTypes.hpp"

enum class LogsAction {
    Read,
    Download,
    Unknown
};

struct LogsConfigPathResult {
    bool success = false;
    QString path;
    QString error;
};

struct LogsReadResult {
    bool success = false;
    QJsonObject parameters;
    QString error;
};

namespace Logs {
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleDownloadRequest(const ModuleRequest &request);
LogsConfigPathResult loadLogsSettingsPath(const QString &configKey);
QString loadBackendConfigValue(const QString &configKey);
LogsReadResult readLogFilesInformation(const QString &logPaths);
}

#endif // LOGS_HPP
