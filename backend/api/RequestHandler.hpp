// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include "RequestResponseTypes.hpp"
#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QTimer>
#include <cstddef>

class QWebSocket;

class RequestHandler final : public QObject {
    Q_OBJECT

public:
    explicit RequestHandler(QObject *parent = nullptr);
    void setSocket(QWebSocket *socket);

public slots:
    void handleTextMessage(const QString &message);
    void enqueueResponse(const QJsonObject &response);

signals:
    void pcapEnqueueRequested(const ModuleRequest &request);
    void systemControlEnqueueRequested(const ModuleRequest &request);

private:
    void trySendNextResponse();
    void handleAck(const QJsonObject &responseObj);
    void resendInFlight();
    static bool isValidTemplate(const QJsonObject &obj);
    static ModuleRequest toModuleRequest(const QJsonObject &obj);
    static ModuleGroup toModuleGroup(const QString &group);

    QWebSocket *m_socket = nullptr;
    QQueue<QJsonObject> m_responseQueue;
    QJsonObject m_inFlightResponse;
    QString m_inFlightId;
    bool m_hasInFlight = false;
    qint64 m_responseIdCounter = 0;
    QTimer m_ackTimer;
};

#endif // REQUEST_HANDLER_HPP
