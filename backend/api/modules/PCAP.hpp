// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef PCAP_HPP
#define PCAP_HPP

#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QString>

class ConsoleConnector;

class PCAP final : public QObject {
    Q_OBJECT

public:
    explicit PCAP(QObject *parent = nullptr);

public slots:
    void enqueueRequest(const QJsonObject &request);

signals:
    void responseReady(const QJsonObject &response);

private:
    void processQueue();
    void startCapture(const QJsonObject &request);
    void readCapture(const QJsonObject &request);

    ConsoleConnector *m_console = nullptr;
    bool m_busy = false;
    bool m_recording = false;
    QString m_lastFile;
    QQueue<QJsonObject> m_queue;
};

#endif // PCAP_HPP
