// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef PCAP_HPP
#define PCAP_HPP

#include "RequestResponseTypes.hpp"

#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QString>

class ConsoleConnector;

enum class PCAPAction {
    ReadInterfaces,
    Read,
    Write,
    Unknown
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

signals:
    void responseReady(const QJsonObject &response);

private:
    void processQueue();
    static PCAPAction toPcapAction(const QString &action);
    static QString extractInterface(const ModuleRequest &request);
    static QString extractFilter(const ModuleRequest &request);
    static QString fileNameFromPath(const QString &filePath);
    ModuleResponse startCapture(const ModuleRequest &request);
    ModuleResponse readCapture(const ModuleRequest &request);

    ConsoleConnector *m_console = nullptr;
    bool m_busy = false;
    bool m_recording = false;
    QString m_lastFile;
    QQueue<ModuleRequest> m_queue;
};

#endif // PCAP_HPP
