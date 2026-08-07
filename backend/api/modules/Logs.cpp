// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "Logs.hpp"

#include "ProtocolSchema.hpp"
#include "BackendConfig.hpp"

#include <QDir>
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
    if (action == QLatin1String(kActionExtract)) {
        return LogsAction::Extract;
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
constexpr char kJournalctlPath[] = "/bin/journalctl";
constexpr char kJournalOutput[] = "short-iso-precise";
constexpr char kJournalBoot[] = "boot";
constexpr char kJournalService[] = "service";
constexpr char kJournalOutputMode[] = "output";
constexpr char kJournalBootCurrent[] = "current";
constexpr char kJournalBootTwo[] = "two";
constexpr char kJournalBootAll[] = "all";
constexpr char kJournalOutputDownload[] = "download";
constexpr char kJournalOutputText[] = "text";
constexpr char kJournalOutputNewTab[] = "new_tab";
constexpr char kJournalServiceUnit[] = "everest.service";
constexpr char kJournalFileName[] = "journal-extract.txt";
constexpr char kErrorJournalStartFailed[] = "journal_start_failed";
constexpr char kErrorJournalTimeout[] = "journal_timeout";
constexpr char kErrorJournalFailed[] = "journal_failed";

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif

bool isFileInConfiguredLogPath(const QFileInfo &fileInfo, const QString &logPaths) {
    if (fileInfo.isSymLink()) {
        return false;
    }

    const QString canonicalFilePath = fileInfo.canonicalFilePath();
    if (canonicalFilePath.isEmpty()) {
        return false;
    }

    for (const QString &entry : logPaths.split(',', kSkipEmptyParts)) {
        const QFileInfo logPathInfo(entry.trimmed());
        const QString canonicalLogPath = logPathInfo.canonicalFilePath();
        if (canonicalLogPath.isEmpty()) {
            continue;
        }

        const QString relativeFilePath = QDir(canonicalLogPath).relativeFilePath(canonicalFilePath);
        if (relativeFilePath != QLatin1String("..") &&
            !relativeFilePath.startsWith(QLatin1String("../"))) {
            return true;
        }
    }

    return false;
}

ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toLogsAction(request.action)) {
    case LogsAction::Read:
        return handleReadRequest(request);
    case LogsAction::Download:
        return handleDownloadRequest(request);
    case LogsAction::Extract:
        return handleExtractRequest(request);
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

ModuleResponse handleExtractRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupLogs),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const QJsonObject parameters = request.parameters;
    const QString boot = parameters.value(QLatin1String(kJournalBoot)).toString(
        QLatin1String(kJournalBootCurrent));
    const QString outputMode = parameters.value(QLatin1String(kJournalOutputMode)).toString(
        QLatin1String(kJournalOutputDownload));
    const QJsonValue serviceValue = parameters.value(QLatin1String(kJournalService));
    if ((boot != QLatin1String(kJournalBootCurrent) &&
         boot != QLatin1String(kJournalBootTwo) &&
         boot != QLatin1String(kJournalBootAll)) ||
        (outputMode != QLatin1String(kJournalOutputDownload) &&
         outputMode != QLatin1String(kJournalOutputText) &&
         outputMode != QLatin1String(kJournalOutputNewTab)) ||
        (!serviceValue.isUndefined() && !serviceValue.isBool())) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorInvalidParams)},
        };
        return response;
    }

    QStringList args{
        QStringLiteral("--no-pager"),
        QStringLiteral("--quiet"),
        QStringLiteral("--output=%1").arg(QLatin1String(kJournalOutput)),
    };
    if (boot == QLatin1String(kJournalBootCurrent)) {
        args << QStringLiteral("--boot=0");
    } else if (boot == QLatin1String(kJournalBootTwo)) {
        args << QStringLiteral("--boot=0") << QStringLiteral("--boot=-1");
    }

    if (parameters.value(QLatin1String(kJournalService)).toBool(false)) {
        args << QStringLiteral("--unit=%1").arg(QLatin1String(kJournalServiceUnit));
    }

    QProcess process;
    process.start(QLatin1String(kJournalctlPath), args);
    if (!process.waitForStarted(2000)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorJournalStartFailed)},
        };
        return response;
    }

    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(1000);
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorJournalTimeout)},
        };
        return response;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorJournalFailed)},
        };
        return response;
    }

    const QByteArray journalData = process.readAllStandardOutput();
    response.parameters = QJsonObject{
        {QLatin1String(kKeyFile), QLatin1String(kJournalFileName)},
        {QLatin1String(kKeyDataB64), QString::fromLatin1(journalData.toBase64())},
    };
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
                             { QLatin1String(kFileName), fileInfo.absoluteFilePath() },
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

    const LogsConfigPathResult logsPathResult = loadLogsSettingsPath(QLatin1String(kConfLogsPath));
    if (!logsPathResult.success) {
        return LogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = logsPathResult.error,
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
        if (!fileInfo.exists() || !fileInfo.isFile() ||
            !isFileInConfiguredLogPath(fileInfo, logsPathResult.path)) {
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
