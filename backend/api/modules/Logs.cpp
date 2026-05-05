// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "Logs.hpp"

#include "ProtocolSchema.hpp"
#include "BackendConfig.hpp"

#include <QDebug>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

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

    const LogsReadResult readResult = readLogFilesInformation(logsPathResult.path);
    if (!readResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), readResult.error},
        };
        return response;
    }

    response.parameters = readResult.parameters;
    response.success = true;

    return response;
}

ModuleResponse handleDownloadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("logs"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    const LogsDownloadResult downloadResult = createLogsArchive(request.parameters);
    if (!downloadResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), downloadResult.error},
        };
        return response;
    }

    response.parameters = downloadResult.parameters;
    response.success = true;
    return response;
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

LogsReadResult readLogFilesInformation(const QString &logPaths) {
    QJsonObject parameters = {};
    QJsonObject files = {};
    
    QList<QString> logPathsList;
    for (const QString &entry : logPaths.split(',', Qt::SkipEmptyParts)) {
        logPathsList.append(entry.trimmed());
    }

    int16_t idCounter = 0;
    for (const QString& path: logPathsList) {
        if (!QFileInfo(path).exists()) {
            return LogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QString("logs_path_not_found")
            };
        }

        if (!QFileInfo(path).isDir()) {
            return LogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QString("logs_path_not_a_directory")
            };
        }

        if (!QFileInfo(path).isReadable()) {
            return LogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QString("logs_path_not_readable")
            };
        }

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
    return LogsReadResult{
        .success = true,
        .parameters = parameters,
        .error = QString()
    };

}

LogsDownloadResult createLogsArchive(const QJsonObject &requestParameters) {
    const QJsonObject selectedFiles = requestParameters.value(QStringLiteral("files")).toObject();
    if (selectedFiles.isEmpty()) {
        return LogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorNoFilesSelected),
        };
    }

    QStringList files;
    const auto selectedFileIds = selectedFiles.keys();
    for (const QString &selectedFileId : selectedFileIds) {
        const QString filePath = selectedFiles.value(selectedFileId).toString().trimmed();
        if (filePath.isEmpty()) {
            return LogsDownloadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorInvalidParams),
            };
        }

        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            return LogsDownloadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorFileNotFound),
            };
        }

        if (!fileInfo.isReadable()) {
            return LogsDownloadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorReadFailed),
            };
        }
        files.append(filePath);
    }

    QTemporaryDir archiveDir;
    if (!archiveDir.isValid()) {
        return LogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorFileIoFailed),
        };
    }

    const QString archiveFileName = QStringLiteral("logs_bundle.tar.gz");
    const QString tarPath = archiveDir.filePath(archiveFileName);

    QStringList args;
    args << QStringLiteral("-czf");
    args << tarPath;

    for (const QString &file : files) {
        QFileInfo info(file);

        args << "-C"
             << info.absolutePath()
             << info.fileName();
    }

    const int exitCode = QProcess::execute(QStringLiteral("tar"), args);
    if (exitCode != 0) {
        return LogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QStringLiteral("zip_failed"),
        };
    }

    QFile tarFile(tarPath);
    if (!tarFile.open(QIODevice::ReadOnly)) {
        return LogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorFileIoFailed),
        };
    }

    const QByteArray tarData = tarFile.readAll();
    const QByteArray tarDataB64 = tarData.toBase64();

    return LogsDownloadResult{
        .success = true,
        .parameters = QJsonObject{
            {QLatin1String(kKeyFile), archiveFileName},
            {QLatin1String(kKeyDataB64), QString::fromLatin1(tarDataB64)},
        },
        .error = QString(),
    };
}
} // namespace Logs
