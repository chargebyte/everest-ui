// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include "RequestResponseTypes.hpp"
#include <QObject>
#include <QHash>
#include <QByteArray>
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
    void enqueuePcapChunk(qint64 serverRequestId, quint32 sequence, bool final,
                          const QByteArray &payload);

signals:
    void pcapEnqueueRequested(const ModuleRequest &request);
    void pcapChunkRequested(qint64 serverRequestId, quint32 sequence);
    void systemControlEnqueueRequested(const ModuleRequest &request);

private:
    struct RequestContext {
        qint64 clientRequestId = 0;
        quint64 connectionGeneration = 0;
        QString group;
        QString action;
        qint64 relatedPcapWriteRequestId = 0;
    };

    void trySendNextResponse();
    void handleAck(const QJsonObject &responseObj);
    void resendInFlight();
    void clearSession();
    void enqueueCurrentResponse(const QJsonObject &response);
    void enqueueResponseObject(const QJsonObject &response);
    bool retainContext(const QJsonObject &response, const RequestContext &context) const;
    void removePcapWriteContext(qint64 serverRequestId);
    void handlePcapChunkAck(const QJsonObject &object);
    qint64 findPcapReadContext(qint64 clientRequestId) const;
    static bool isValidTemplate(const QJsonObject &obj);
    static ModuleRequest toModuleRequest(const QJsonObject &obj);
    static ModuleGroup toModuleGroup(const QString &group);

    QWebSocket *m_socket = nullptr;
    QQueue<QJsonObject> m_responseQueue;
    QJsonObject m_inFlightResponse;
    QString m_inFlightId;
    bool m_hasInFlight = false;
    qint64 m_inFlightServerRequestId = -1;
    qint64 m_responseIdCounter = 0;
    qint64 m_serverRequestIdCounter = 0;
    quint64 m_connectionGeneration = 0;
    QHash<qint64, RequestContext> m_requestContexts;
    QTimer m_ackTimer;
};

#endif // REQUEST_HANDLER_HPP
