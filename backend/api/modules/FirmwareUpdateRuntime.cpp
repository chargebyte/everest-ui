// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "FirmwareUpdateRuntime.hpp"

#include "FirmwareUpdate.hpp"
#include "ProtocolSchema.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr char kRaucInstallTemplate[] = "rauc install @image_path@";
constexpr char kRebootTemplate[] = "reboot";
constexpr char kFirmwareRebootCommandConfigKey[] = "firmware_reboot_command";
constexpr char kFirmwareImageDirConfigKey[] = "firmware_image_dir";
constexpr char kFirmwareUpdateInProgress[] = "update_in_progress";
constexpr char kFirmwareUpdateStartFailed[] = "firmware_update_start_failed";
constexpr char kFirmwareRebootNotAllowed[] = "not_allowed";
constexpr char kFirmwareUploadNotReady[] = "upload_not_ready";
constexpr char kFirmwareUploadInProgress[] = "upload_in_progress";
constexpr char kFirmwareUploadStartFailed[] = "firmware_upload_start_failed";
constexpr char kFirmwareUploadInvalidParams[] = "invalid_params";
constexpr char kFirmwareUploadNotStarted[] = "upload_not_started";
constexpr char kFirmwareUploadChunkOutOfOrder[] = "chunk_out_of_order";
constexpr char kFirmwareUploadChunkInvalid[] = "invalid_chunk";
constexpr char kFirmwareUploadChunkWriteFailed[] = "firmware_upload_chunk_write_failed";
constexpr char kFirmwareUploadSizeExceeded[] = "upload_size_exceeded";
constexpr char kFirmwareUploadIncomplete[] = "upload_incomplete";
constexpr char kFirmwareUploadFinishFailed[] = "firmware_upload_finish_failed";
constexpr char kFirmwareUploadChecksumMismatch[] = "checksum_mismatch";
}

FirmwareUpdateRuntime::FirmwareUpdateRuntime(QObject *parent)
    : QObject(parent), m_console(new ConsoleConnector(this)) {
    connect(m_console, &ConsoleConnector::streamingStdoutReceived, this,
            [this](const QByteArray &chunk) { processStdoutChunk(chunk); });
    connect(m_console, &ConsoleConnector::streamingFinished, this,
            [this](const ConsoleConnector::RunResult &result) { handleStreamingFinished(result); });
}

ModuleResponse FirmwareUpdateRuntime::handleUpdateRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    if (m_updateRunning) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUpdateInProgress)},
        };
        return response;
    }

    if (!hasFinishedUpload()) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadNotReady)},
        };
        return response;
    }

    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult startResult = m_console->executeTemplate(
        QString::fromLatin1(kRaucInstallTemplate),
        {
            {QStringLiteral("@image_path@"), m_uploadFilePath},
        },
        options,
        ConsoleConnector::ExecMode::StreamingAsync);
    if (startResult.exitCode != 0) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUpdateStartFailed)},
        };
        return response;
    }

    m_updateRunning = true;
    m_currentRequestId = request.requestId;
    m_currentAction = request.action;
    m_stdoutLineBuffer.clear();
    m_ackSent = false;
    m_successSeen = false;
    m_failureSeen = false;
    m_rebootRequired = false;
    response.success = true;
    return response;
}

ModuleResponse FirmwareUpdateRuntime::handleRebootRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    if (m_updateRunning || m_uploadRunning || !m_rebootRequired) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareRebootNotAllowed)},
        };
        return response;
    }

    const QString rebootCommand =
        FirmwareUpdate::loadBackendConfigValue(QString::fromLatin1(kFirmwareRebootCommandConfigKey))
            .trimmed();
    m_rebootRequired = false;
    runDeferredRebootCommand(rebootCommand.isEmpty() ? QString::fromLatin1(kRebootTemplate)
                                                     : rebootCommand);
    response.success = true;
    return response;
}

ModuleResponse FirmwareUpdateRuntime::handleUploadStartRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };
    if (m_uploadRunning) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadInProgress)},
        };
        return response;
    }

    resetUploadState();

    const QJsonObject imageObject = request.parameters.value(QStringLiteral("image")).toObject();
    const QString rawFileName = imageObject.value(QStringLiteral("file_name")).toString().trimmed();
    const QString fileName = QFileInfo(rawFileName).fileName();
    const qint64 sizeBytes =
        imageObject.value(QStringLiteral("size_bytes")).toVariant().toLongLong();
    const int chunkCount = imageObject.value(QStringLiteral("chunk_count")).toInt(-1);
    const int chunkSizeBytes = imageObject.value(QStringLiteral("chunk_size_bytes")).toInt(-1);

    if (fileName.isEmpty() || sizeBytes <= 0 || chunkCount <= 0 || chunkSizeBytes <= 0) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadInvalidParams)},
        };
        return response;
    }

    const FirmwareImageDirResult imageDirResult =
        FirmwareUpdate::loadFirmwareImageDir(QString::fromLatin1(kFirmwareImageDirConfigKey));
    if (!imageDirResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), imageDirResult.error},
        };
        return response;
    }

    const FirmwareImageCleanupResult cleanupResult = FirmwareUpdate::cleanOldFirmwareImages(QString());
    if (!cleanupResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), cleanupResult.error},
        };
        return response;
    }

    const QString targetPath = QDir(imageDirResult.path).filePath(fileName);
    m_uploadFile.setFileName(targetPath);
    if (!m_uploadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        resetUploadState();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadStartFailed)},
        };
        return response;
    }

    m_uploadRunning = true;
    m_rebootRequired = false;
    m_uploadFilePath = targetPath;
    m_uploadFileName = fileName;
    m_expectedUploadSizeBytes = sizeBytes;
    m_writtenUploadSizeBytes = 0;
    m_expectedChunkCount = chunkCount;
    m_expectedChunkSizeBytes = chunkSizeBytes;
    m_nextExpectedChunkIndex = 0;
    m_uploadFinished = false;
    response.success = true;
    return response;
}

ModuleResponse FirmwareUpdateRuntime::handleUploadChunkRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };
    if (!m_uploadRunning || !m_uploadFile.isOpen() || m_uploadFinished) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadNotStarted)},
        };
        return response;
    }

    const QJsonObject imageObject = request.parameters.value(QStringLiteral("image")).toObject();
    const int chunkIndex = imageObject.value(QStringLiteral("chunk_index")).toInt(-1);
    const QString dataB64 = imageObject.value(QStringLiteral("dataB64")).toString().trimmed();

    if (chunkIndex < 0 || dataB64.isEmpty()) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadInvalidParams)},
        };
        return response;
    }

    if (chunkIndex != m_nextExpectedChunkIndex || chunkIndex >= m_expectedChunkCount) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadChunkOutOfOrder)},
        };
        return response;
    }

    const QByteArray chunkData = QByteArray::fromBase64(dataB64.toLatin1());
    if (chunkData.isEmpty()) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadChunkInvalid)},
        };
        return response;
    }

    if (chunkData.size() > m_expectedChunkSizeBytes) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadChunkInvalid)},
        };
        return response;
    }

    if (m_writtenUploadSizeBytes + chunkData.size() > m_expectedUploadSizeBytes) {
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadSizeExceeded)},
        };
        return response;
    }

    const qint64 bytesWritten = m_uploadFile.write(chunkData);
    if (bytesWritten != chunkData.size()) {
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadChunkWriteFailed)},
        };
        return response;
    }

    m_writtenUploadSizeBytes += bytesWritten;
    m_nextExpectedChunkIndex += 1;

    response.success = true;
    return response;
}

ModuleResponse FirmwareUpdateRuntime::handleUploadFinishRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };
    if (!m_uploadRunning || !m_uploadFile.isOpen() || m_uploadFinished) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadNotStarted)},
        };
        return response;
    }

    const QString expectedSha256 = request.parameters
                                       .value(QStringLiteral("image"))
                                       .toObject()
                                       .value(QStringLiteral("sha256"))
                                       .toString()
                                       .trimmed()
                                       .toLower();
    if (expectedSha256.isEmpty()) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadInvalidParams)},
        };
        return response;
    }

    if (m_nextExpectedChunkIndex != m_expectedChunkCount ||
        m_writtenUploadSizeBytes != m_expectedUploadSizeBytes) {
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadIncomplete)},
        };
        return response;
    }

    if (!m_uploadFile.flush()) {
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadFinishFailed)},
        };
        return response;
    }

    m_uploadFile.close();

    QFile uploadedFile(m_uploadFilePath);
    if (!uploadedFile.open(QIODevice::ReadOnly)) {
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadFinishFailed)},
        };
        return response;
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    if (!hasher.addData(&uploadedFile)) {
        uploadedFile.close();
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadFinishFailed)},
        };
        return response;
    }

    const QString actualSha256 = QString::fromLatin1(hasher.result().toHex()).toLower();
    uploadedFile.close();
    if (actualSha256 != expectedSha256) {
        abortUploadAndRemovePartialFile();
        response.parameters = QJsonObject{
            {QStringLiteral("error"), QString::fromLatin1(kFirmwareUploadChecksumMismatch)},
        };
        return response;
    }

    m_uploadRunning = false;
    m_uploadFinished = true;
    response.success = true;
    return response;
}

void FirmwareUpdateRuntime::processStdoutChunk(const QByteArray &chunk) {
    m_stdoutLineBuffer.append(chunk);

    qsizetype newlineIndex = m_stdoutLineBuffer.indexOf('\n');
    while (newlineIndex >= 0) {
        QByteArray line = m_stdoutLineBuffer.left(newlineIndex);
        m_stdoutLineBuffer.remove(0, newlineIndex + 1);

        if (!line.isEmpty() && line.endsWith('\r')) {
            line.chop(1);
        }

        handleStdoutLine(QString::fromUtf8(line));
        newlineIndex = m_stdoutLineBuffer.indexOf('\n');
    }
}

void FirmwareUpdateRuntime::flushStdoutRemainder() {
    if (m_stdoutLineBuffer.isEmpty()) {
        return;
    }

    QByteArray line = m_stdoutLineBuffer;
    m_stdoutLineBuffer.clear();
    if (!line.isEmpty() && line.endsWith('\r')) {
        line.chop(1);
    }

    handleStdoutLine(QString::fromUtf8(line));
}

bool FirmwareUpdateRuntime::hasFinishedUpload() const {
    if (m_uploadRunning || !m_uploadFinished || m_uploadFilePath.isEmpty()) {
        return false;
    }

    const QFileInfo uploadedFileInfo(m_uploadFilePath);
    return uploadedFileInfo.exists() && uploadedFileInfo.isFile() && uploadedFileInfo.isReadable();
}

void FirmwareUpdateRuntime::handleStdoutLine(const QString &line) {
    if (!m_updateRunning) {
        return;
    }

    const QString trimmedLine = line.trimmed();
    if (trimmedLine.isEmpty()) {
        return;
    }

    static const QRegularExpression progressRegex(QStringLiteral("^\\s*(\\d+)%\\s+(.+?)\\s*$"));
    const QRegularExpressionMatch progressMatch = progressRegex.match(line);
    if (progressMatch.hasMatch()) {
        bool ok = false;
        const int progress = progressMatch.captured(1).toInt(&ok);
        const QString stage = progressMatch.captured(2).trimmed();

        if (ok) {
            emit responseReady(ModuleResponse{
                .requestId = m_currentRequestId,
                .group = QStringLiteral("firmware"),
                .action = m_currentAction + QStringLiteral(".progress"),
                .parameters = QJsonObject{
                    {QStringLiteral("progress"), progress},
                    {QStringLiteral("stage"), stage},
                },
                .success = true,
                .final = false,
            });

            if (!m_ackSent &&
                stage.startsWith(QStringLiteral("Checking slot ")) &&
                stage.endsWith(QStringLiteral(" done."))) {
                m_ackSent = true;
                emit responseReady(ModuleResponse{
                    .requestId = m_currentRequestId,
                    .group = QStringLiteral("firmware"),
                    .action = m_currentAction,
                    .parameters = QJsonObject{},
                    .success = true,
                    .final = false,
                });
            }
        }
    }

    if (trimmedLine.endsWith(QStringLiteral("succeeded"))) {
        m_successSeen = true;
    }

    if (trimmedLine.contains(QStringLiteral("failed"), Qt::CaseInsensitive)) {
        m_failureSeen = true;
    }
}

void FirmwareUpdateRuntime::handleStreamingFinished(const ConsoleConnector::RunResult &result) {
    flushStdoutRemainder();

    if (!m_updateRunning) {
        return;
    }

    if (result.exitCode == 0 && m_successSeen) {
        m_rebootRequired = true;
        emit responseReady(ModuleResponse{
            .requestId = m_currentRequestId,
            .group = QStringLiteral("firmware"),
            .action = m_currentAction,
            .parameters = QJsonObject{
                {QStringLiteral("restart_required"), true},
            },
            .success = true,
            .final = true,
        });
    } else {
        m_rebootRequired = false;
        emit responseReady(ModuleResponse{
            .requestId = m_currentRequestId,
            .group = QStringLiteral("firmware"),
            .action = m_currentAction,
            .parameters = QJsonObject{
                {QStringLiteral("error"), QString::fromLatin1(kErrorFlashFailed)},
            },
            .success = false,
            .final = true,
        });
    }

    m_updateRunning = false;
    resetUploadState();
    m_currentRequestId = 0;
    m_currentAction.clear();
    m_stdoutLineBuffer.clear();
    m_ackSent = false;
    m_successSeen = false;
    m_failureSeen = false;
}

void FirmwareUpdateRuntime::runDeferredRebootCommand(const QString &command) {
    QTimer::singleShot(0, this, [command]() {
        ConsoleConnector console;
        ConsoleConnector::ExecOptions options;
        options.syncTimeoutMs = 5000;
        const ConsoleConnector::RunResult result =
            console.executeTemplate(command, {}, options, ConsoleConnector::ExecMode::Sync);

        if (result.exitCode != 0) {
            qWarning() << "Firmware reboot command failed with exit code" << result.exitCode
                       << "stderr:" << result.stderrData;
        }
    });
}

void FirmwareUpdateRuntime::resetUploadState() {
    if (m_uploadFile.isOpen()) {
        m_uploadFile.close();
    }

    m_uploadRunning = false;
    m_uploadFile.setFileName(QString());
    m_uploadFilePath.clear();
    m_uploadFileName.clear();
    m_expectedUploadSizeBytes = 0;
    m_writtenUploadSizeBytes = 0;
    m_expectedChunkCount = 0;
    m_expectedChunkSizeBytes = 0;
    m_nextExpectedChunkIndex = 0;
    m_uploadFinished = false;
}

void FirmwareUpdateRuntime::abortUploadAndRemovePartialFile() {
    const QString filePath = m_uploadFilePath;
    resetUploadState();

    if (!filePath.isEmpty()) {
        QFile::remove(filePath);
    }
}
