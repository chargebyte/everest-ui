// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "Logs.hpp"

#include "ProtocolSchema.hpp"
#include "BackendConfig.hpp"

#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>

LogsAction toLogsAction(const QString &action) {
    if (action == QLatin1String(kActionRead)) {
        return LogsAction::Read;
    }
    if (action == QLatin1String(kActionDownload)) {
        return LogsAction::Download;
    }

    return LogsAction::Unknown;
}

namespace Logs {
ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toLogsAction(request.action)) {
    case LogsAction::Read:
        return handleReadRequest(request);
    case LogsAction::Download:
        return handleDownloadRequest(request);
    case LogsAction::Unknown:
        throw std::runtime_error("Logs::handleRequest got unsupported action");
    }

    throw std::runtime_error("Logs::handleRequest reached unreachable code");
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("logs"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    const LogsConfigPathResult logsPathResult = loadLogsSettingsPath(QStringLiteral("logs_paths"));
    if (!logsPathResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), logsPathResult.error},
        };
        return response;
    }

    response.parameters = readLogFilesInformation(logsPathResult.path);
    response.success = true;

    return response;
}

ModuleResponse handleDownloadRequest(const ModuleRequest &request) {

}

LogsConfigPathResult loadLogsSettingsPath(const QString &configKey) {
    const QString value = loadBackendConfigValue(configKey);
    if (!value.isEmpty()) {
        return LogsConfigPathResult{
            .success = true,
            .path = value,
            .error = QString(),
        };
    }

    return LogsConfigPathResult{
        .success = false,
        .path = QString(),
        .error = configKey + QStringLiteral("_missing"),
    };
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

QJsonObject readLogFilesInformation(const QString &logPaths) {
    QJsonObject parameters = {};
    QJsonObject files = {};
    
    const QList logPathsList = logPaths.split(',');

    int16_t idCounter = 0;
    for (const QString& path: logPathsList) {
        QDirIterator it(path,
                        QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            it.next();
            QFileInfo fileInfo = it.fileInfo();

            qint64 size = fileInfo.size();
            QDateTime lastModified = fileInfo.lastModified();

            files.insert(QString::number(idCounter),
                         QJsonObject{
                             { QStringLiteral("name"), (path + fileInfo.fileName()) },
                             { QStringLiteral("size_bytes"), size },
                             { QStringLiteral("last_modified"), lastModified.toString(Qt::ISODate) }
                         });

            idCounter++;
        }
    }

    parameters.insert(QStringLiteral("files"), files);
    return parameters;
}
} // namespace Logs
