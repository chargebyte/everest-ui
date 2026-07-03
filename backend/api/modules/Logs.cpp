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
#include <QtGlobal>

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
constexpr char kErrorLogsPathNotFound[] = "logs_path_not_found";
constexpr char kErrorLogsPathNotDirectory[] = "logs_path_not_a_directory";
constexpr char kErrorLogsPathNotReadable[] = "logs_path_not_readable";
constexpr char kErrorZipFailed[] = "zip_failed";
constexpr char kParametersFiles[] = "files";
constexpr char kConfLogsPath[] = "logs_path";
constexpr char kFileName[] = "name";
constexpr char kFileLastModified[] = "last_modified";
constexpr char kFileDefaultName[] = "logs_bundle.tar.gz";
constexpr char kCmdFlafCzf[] = "-czf";
constexpr char kCmdFlagC[] = "-C";
constexpr char kCmdFlagTar[] = "tar";

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif

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
        .group = QLatin1String(kGroupLogs),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const LogsConfigPathResult logsPathResult = loadLogsSettingsPath(QLatin1String(kConfLogsPath));
    if (!logsPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), logsPathResult.error},
        };
        return response;
    }

    const LogsReadResult readResult = readLogFilesInformation(logsPathResult.path);
    if (!readResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), readResult.error},
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
        .group = QLatin1String(kGroupLogs),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const LogsDownloadResult downloadResult = createLogsArchive(request.parameters);
    if (!downloadResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), downloadResult.error},
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
        .error = configKey + QLatin1String(kErrorMissing),
    };
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

LogsReadResult readLogFilesInformation(const QString &logPaths) {
    QJsonObject parameters = {};
    QJsonObject files = {};
    
    QList<QString> logPathsList;
    for (const QString &entry : logPaths.split(',', kSkipEmptyParts)) {
        logPathsList.append(entry.trimmed());
    }

    int16_t idCounter = 0;
    for (const QString& path: logPathsList) {
        if (!QFileInfo(path).exists()) {
            return LogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorLogsPathNotFound)
            };
        }

        if (!QFileInfo(path).isDir()) {
            return LogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorLogsPathNotDirectory)
            };
        }

        if (!QFileInfo(path).isReadable()) {
            return LogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorLogsPathNotReadable)
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
                             { QLatin1String(kFileName), (path + fileInfo.fileName()) },
                             { QLatin1String(kParametersSizeBytes), size },
                             { QLatin1String(kFileLastModified), lastModified.toString(Qt::ISODate) }
                         });

            idCounter++;
        }
    }

    parameters.insert(QLatin1String(kParametersFiles), files);
    return LogsReadResult{
        .success = true,
        .parameters = parameters,
        .error = QString()
    };

}

LogsDownloadResult createLogsArchive(const QJsonObject &requestParameters) {
    const QJsonObject selectedFiles = requestParameters.value(QLatin1String(kParametersFiles)).toObject();
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

    const QString archiveFileName = QLatin1String(kFileDefaultName);
    const QString tarPath = archiveDir.filePath(archiveFileName);

    QStringList args;
    args << QLatin1String(kCmdFlafCzf);
    args << tarPath;

    for (const QString &file : files) {
        QFileInfo info(file);

        args << kCmdFlagC
             << info.absolutePath()
             << info.fileName();
    }

    const int exitCode = QProcess::execute(QLatin1String(kCmdFlagTar), args);
    if (exitCode != 0) {
        return LogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorZipFailed),
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
