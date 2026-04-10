// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QTimer>
#include <cstddef>

class QWebSocket;

#include "ProtocolSchema.hpp"

class RequestHandler final : public QObject {
    Q_OBJECT

public:
    enum class QueueTarget {
        Pcap,
        SystemControl,
    };

    explicit RequestHandler(QObject *parent = nullptr);
    void setSocket(QWebSocket *socket);

public slots:
    void handleTextMessage(const QString &message);
    void enqueueResponse(const QJsonObject &response);

signals:
    void pcapEnqueueRequested(const QJsonObject &request);
    void systemControlEnqueueRequested(const QJsonObject &request);

private:
    void trySendNextResponse();
    void handleAck(const QJsonObject &responseObj);
    void resendInFlight();

    QWebSocket *m_socket = nullptr;
    QQueue<QJsonObject> m_responseQueue;
    QJsonObject m_inFlightResponse;
    QString m_inFlightId;
    bool m_hasInFlight = false;
    qint64 m_responseIdCounter = 0;
    QTimer m_ackTimer;
};

#endif // REQUEST_HANDLER_HPP
