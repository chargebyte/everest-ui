// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SystemLogs.hpp"

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

SystemLogsAction toSystemLogsAction(const QString &action) {
    if (action == QLatin1String(kActionRead)) {
        return SystemLogsAction::Read;
    }
    if (action == QLatin1String(kActionDownload)) {
        return SystemLogsAction::Download;
    }
    if (action == QLatin1String(kActionExtract)) {
        return SystemLogsAction::Extract;
    }

    return SystemLogsAction::Unknown;
}

namespace SystemLogs {
constexpr char kErrorSystemLogsPathNotFound[] = "system_logs_path_not_found";
constexpr char kErrorSystemLogsPathNotDirectory[] = "system_logs_path_not_a_directory";
constexpr char kErrorSystemLogsPathNotReadable[] = "system_logs_path_not_readable";
constexpr char kErrorZipFailed[] = "zip_failed";
constexpr char kParametersFiles[] = "files";
constexpr char kConfSystemLogsPath[] = "system_logs_path";
constexpr char kFileName[] = "name";
constexpr char kFileLastModified[] = "last_modified";
constexpr char kFileDefaultName[] = "system_logs_bundle.tar.gz";
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
constexpr char kJournalFileName[] = "system-log-extract.txt";
constexpr char kErrorJournalStartFailed[] = "journal_start_failed";
constexpr char kErrorJournalTimeout[] = "journal_timeout";
constexpr char kErrorJournalFailed[] = "journal_failed";

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif

bool isFileInConfiguredSystemLogsPath(const QFileInfo &fileInfo, const QString &systemLogPaths) {
    if (fileInfo.isSymLink()) {
        return false;
    }

    const QString canonicalFilePath = fileInfo.canonicalFilePath();
    if (canonicalFilePath.isEmpty()) {
        return false;
    }

    for (const QString &entry : systemLogPaths.split(',', kSkipEmptyParts)) {
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
    switch (toSystemLogsAction(request.action)) {
    case SystemLogsAction::Read:
        return handleReadRequest(request);
    case SystemLogsAction::Download:
        return handleDownloadRequest(request);
    case SystemLogsAction::Extract:
        return handleExtractRequest(request);
    case SystemLogsAction::Unknown:
        throw std::runtime_error("SystemLogs::handleRequest got unsupported action");
    }

    throw std::runtime_error("SystemLogs::handleRequest reached unreachable code");
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSystemLogs),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const SystemLogsConfigPathResult systemLogsPathResult =
        loadSystemLogsSettingsPath(QLatin1String(kConfSystemLogsPath));
    if (!systemLogsPathResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), systemLogsPathResult.error},
        };
        return response;
    }

    const SystemLogsReadResult readResult =
        readSystemLogFilesInformation(systemLogsPathResult.path);
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
        .group = QLatin1String(kGroupSystemLogs),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const SystemLogsDownloadResult downloadResult =
        createSystemLogsArchive(request.parameters);
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
        .group = QLatin1String(kGroupSystemLogs),
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

    const QStringList commonArgs{
        QStringLiteral("--no-pager"),
        QStringLiteral("--quiet"),
        QStringLiteral("--output=%1").arg(QLatin1String(kJournalOutput)),
    };
    QStringList boots;
    if (boot == QLatin1String(kJournalBootCurrent)) {
        boots << QStringLiteral("0");
    } else if (boot == QLatin1String(kJournalBootTwo)) {
        boots << QStringLiteral("-1") << QStringLiteral("0");
    } else {
        boots << QString();
    }

    QByteArray journalData;
    for (const QString &bootId : boots) {
        QStringList args = commonArgs;
        if (!bootId.isEmpty()) {
            args << QStringLiteral("--boot=%1").arg(bootId);
        }
        if (parameters.value(QLatin1String(kJournalService)).toBool(false)) {
            args << QStringLiteral("--unit=%1").arg(QLatin1String(kJournalServiceUnit));
        }

        QProcess process;
        process.start(QLatin1String(kJournalctlPath), args);
        if (!process.waitForStarted(2000)) {
            if (bootId == QLatin1String("-1")) {
                continue;
            }
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorJournalStartFailed)},
            };
            return response;
        }

        if (!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(1000);
            if (bootId == QLatin1String("-1")) {
                continue;
            }
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorJournalTimeout)},
            };
            return response;
        }

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (bootId == QLatin1String("-1")) {
                continue;
            }
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorJournalFailed)},
            };
            return response;
        }

        journalData.append(process.readAllStandardOutput());
    }

    response.parameters = QJsonObject{
        {QLatin1String(kKeyFile), QLatin1String(kJournalFileName)},
        {QLatin1String(kKeyDataB64), QString::fromLatin1(journalData.toBase64())},
    };
    response.success = true;
    return response;
}

SystemLogsConfigPathResult loadSystemLogsSettingsPath(const QString &configKey) {
    const QString value = loadBackendConfigValue(configKey);
    if (!value.isEmpty()) {
        return SystemLogsConfigPathResult{
            .success = true,
            .path = value,
            .error = QString(),
        };
    }

    return SystemLogsConfigPathResult{
        .success = false,
        .path = QString(),
        .error = configKey + QLatin1String(kErrorMissing),
    };
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

SystemLogsReadResult readSystemLogFilesInformation(const QString &systemLogPaths) {
    QJsonObject parameters = {};
    QJsonObject files = {};
    
    QList<QString> systemLogPathsList;
    for (const QString &entry : systemLogPaths.split(',', kSkipEmptyParts)) {
        systemLogPathsList.append(entry.trimmed());
    }

    int16_t idCounter = 0;
    for (const QString& path: systemLogPathsList) {
        if (!QFileInfo(path).exists()) {
            return SystemLogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorSystemLogsPathNotFound)
            };
        }

        if (!QFileInfo(path).isDir()) {
            return SystemLogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorSystemLogsPathNotDirectory)
            };
        }

        if (!QFileInfo(path).isReadable()) {
            return SystemLogsReadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorSystemLogsPathNotReadable)
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
    return SystemLogsReadResult{
        .success = true,
        .parameters = parameters,
        .error = QString()
    };

}

SystemLogsDownloadResult createSystemLogsArchive(const QJsonObject &requestParameters) {
    const QJsonObject selectedFiles = requestParameters.value(QLatin1String(kParametersFiles)).toObject();
    if (selectedFiles.isEmpty()) {
        return SystemLogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorNoFilesSelected),
        };
    }

    const SystemLogsConfigPathResult systemLogsPathResult =
        loadSystemLogsSettingsPath(QLatin1String(kConfSystemLogsPath));
    if (!systemLogsPathResult.success) {
        return SystemLogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = systemLogsPathResult.error,
        };
    }

    QStringList files;
    const auto selectedFileIds = selectedFiles.keys();
    for (const QString &selectedFileId : selectedFileIds) {
        const QString filePath = selectedFiles.value(selectedFileId).toString().trimmed();
        if (filePath.isEmpty()) {
            return SystemLogsDownloadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorInvalidParams),
            };
        }

        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile() ||
            !isFileInConfiguredSystemLogsPath(fileInfo, systemLogsPathResult.path)) {
            return SystemLogsDownloadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorFileNotFound),
            };
        }

        if (!fileInfo.isReadable()) {
            return SystemLogsDownloadResult{
                .success = false,
                .parameters = QJsonObject{},
                .error = QLatin1String(kErrorReadFailed),
            };
        }
        files.append(filePath);
    }

    QTemporaryDir archiveDir;
    if (!archiveDir.isValid()) {
        return SystemLogsDownloadResult{
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
        return SystemLogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorZipFailed),
        };
    }

    QFile tarFile(tarPath);
    if (!tarFile.open(QIODevice::ReadOnly)) {
        return SystemLogsDownloadResult{
            .success = false,
            .parameters = QJsonObject{},
            .error = QLatin1String(kErrorFileIoFailed),
        };
    }

    const QByteArray tarData = tarFile.readAll();
    const QByteArray tarDataB64 = tarData.toBase64();

    return SystemLogsDownloadResult{
        .success = true,
        .parameters = QJsonObject{
            {QLatin1String(kKeyFile), archiveFileName},
            {QLatin1String(kKeyDataB64), QString::fromLatin1(tarDataB64)},
        },
        .error = QString(),
    };
}
} // namespace SystemLogs
