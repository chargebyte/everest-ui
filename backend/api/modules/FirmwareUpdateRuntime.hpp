// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef FIRMWARE_UPDATE_RUNTIME_HPP
#define FIRMWARE_UPDATE_RUNTIME_HPP

#include "ConsoleConnector.hpp"
#include "RequestResponseTypes.hpp"

#include <QObject>

class FirmwareUpdateRuntime final : public QObject {
    Q_OBJECT

public:
    explicit FirmwareUpdateRuntime(QObject *parent = nullptr);

    ModuleResponse handleUpdateRequest(const ModuleRequest &request);

signals:
    void responseReady(const ModuleResponse &response);

private:
    void processStdoutChunk(const QByteArray &chunk);
    void flushStdoutRemainder();
    void handleStdoutLine(const QString &line);
    void handleStreamingFinished(const ConsoleConnector::RunResult &result);

    ConsoleConnector *m_console = nullptr;
    bool m_updateRunning = false;
    qint64 m_currentRequestId = 0;
    QString m_currentAction;
    QByteArray m_stdoutLineBuffer;
    bool m_ackSent = false;
    bool m_successSeen = false;
    bool m_failureSeen = false;
};

#endif // FIRMWARE_UPDATE_RUNTIME_HPP
