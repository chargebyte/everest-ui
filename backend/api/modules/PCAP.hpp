// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef PCAP_HPP
#define PCAP_HPP

#include "RequestResponseTypes.hpp"
#include "ConsoleConnector.hpp"

#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>

enum class PCAPAction {
    ReadInterfaces,
    Read,
    Write,
    Unknown
};

enum class PCAPState {
    Idle,
    Starting,
    Recording,
    Stopping,
    Ready,
};

class PCAP final : public QObject {
    Q_OBJECT

public:
    explicit PCAP(QObject *parent = nullptr);
    ModuleResponse handleRequest(const ModuleRequest &request);
    ModuleResponse handleReadInterfacesRequest(const ModuleRequest &request);
    ModuleResponse handleReadRequest(const ModuleRequest &request);
    ModuleResponse handleWriteRequest(const ModuleRequest &request);

public slots:
    void enqueueRequest(const ModuleRequest &request);
    void handleClientDisconnected();

signals:
    void responseReady(const QJsonObject &response);

private:
    void processQueue();
    static PCAPAction toPcapAction(const QString &action);
    static QString extractInterface(const ModuleRequest &request);
    static QString extractFilter(const ModuleRequest &request);
    static QString fileNameFromPath(const QString &filePath);
    static qint64 configuredLimit(const QString &key, qint64 defaultValue);
    ModuleResponse startCapture(const ModuleRequest &request);
    ModuleResponse readCapture(const ModuleRequest &request);
    void handleCaptureFinished(const ConsoleConnector::RunResult &result);
    void checkCaptureLimits();
    void stopCaptureForLimit(const QString &limitName);
    void cleanupCapture(bool removeFile);
    ModuleResponse captureError(const char *error, const QString &details = {}, qint64 requestId = -1) const;
    ModuleResponse readCaptureFile(const ModuleRequest &request);

    ConsoleConnector *m_console = nullptr;
    bool m_busy = false;
    PCAPState m_state = PCAPState::Idle;
    QString m_lastFile;
    qint64 m_captureRequestId = 0;
    QQueue<ModuleRequest> m_queue;
    QString m_startFailureDetails;
    QTimer *m_limitTimer = nullptr;
    QElapsedTimer m_captureElapsed;
    qint64 m_maxSizeBytes = 0;
    qint64 m_maxDurationSeconds = 0;
};

#endif // PCAP_HPP
