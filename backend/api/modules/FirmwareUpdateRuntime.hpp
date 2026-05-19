// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef FIRMWARE_UPDATE_RUNTIME_HPP
#define FIRMWARE_UPDATE_RUNTIME_HPP

#include "ConsoleConnector.hpp"
#include "RequestResponseTypes.hpp"

#include <QFile>
#include <QObject>

class FirmwareUpdateRuntime final : public QObject {
    Q_OBJECT

public:
    explicit FirmwareUpdateRuntime(QObject *parent = nullptr);

    ModuleResponse handleUpdateRequest(const ModuleRequest &request);
    ModuleResponse handleRebootRequest(const ModuleRequest &request);
    ModuleResponse handleUploadStartRequest(const ModuleRequest &request);
    ModuleResponse handleUploadChunkRequest(const ModuleRequest &request);
    ModuleResponse handleUploadFinishRequest(const ModuleRequest &request);

signals:
    void responseReady(const ModuleResponse &response);

private:
    void processStdoutChunk(const QByteArray &chunk);
    void flushStdoutRemainder();
    void handleStdoutLine(const QString &line);
    void handleStreamingFinished(const ConsoleConnector::RunResult &result);
    void runDeferredRebootCommand(const QString &command);
    bool hasFinishedUpload() const;
    void resetUploadState();
    void abortUploadAndRemovePartialFile();

    ConsoleConnector *m_console = nullptr;
    bool m_updateRunning = false;
    bool m_uploadRunning = false;
    qint64 m_currentRequestId = 0;
    QString m_currentAction;
    QByteArray m_stdoutLineBuffer;
    bool m_ackSent = false;
    bool m_successSeen = false;
    bool m_failureSeen = false;
    QFile m_uploadFile;
    QString m_uploadFilePath;
    QString m_uploadFileName;
    qint64 m_expectedUploadSizeBytes = 0;
    qint64 m_writtenUploadSizeBytes = 0;
    int m_expectedChunkCount = 0;
    int m_expectedChunkSizeBytes = 0;
    int m_nextExpectedChunkIndex = 0;
    bool m_uploadFinished = false;
    bool m_rebootRequired = false;
};

#endif // FIRMWARE_UPDATE_RUNTIME_HPP
