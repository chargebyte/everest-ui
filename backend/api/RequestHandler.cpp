// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RequestHandler.hpp"

#include "RequestResponseTypes.hpp"
#include "ResponseBuilder.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QWebSocket>

namespace {

bool isValidTemplate(const QJsonObject &obj) {
    const QJsonValue requestId = obj.value(FieldNameKey(FieldName::RequestId));
    const QJsonValue groupValue = obj.value(FieldNameKey(FieldName::Group));
    const QJsonValue actionValue = obj.value(FieldNameKey(FieldName::Action));
    const QJsonValue parametersValue = obj.value(FieldNameKey(FieldName::Parameters));

    if (!requestId.isDouble() || !groupValue.isString() || !actionValue.isString() ||
        !parametersValue.isObject()) {
        return false;
    }

    return true;
}

bool isAck(const QJsonObject &obj) {
    const QString type = obj.value(QLatin1String(kKeyType)).toString();
    return type == QLatin1String(kTypeAck);
}
} // namespace

RequestHandler::QueueTarget decideQueueTarget(const QJsonObject &obj) {
    if (obj.value(FieldNameKey(FieldName::Group)).toString() != QLatin1String(kGroupPcap)) {
        return RequestHandler::QueueTarget::SystemControl;
    }

    return RequestHandler::QueueTarget::Pcap;
}

RequestHandler::RequestHandler(QObject *parent)
    : QObject(parent) {
    m_ackTimer.setSingleShot(true);
    connect(&m_ackTimer, &QTimer::timeout, this, &RequestHandler::resendInFlight);
}

void RequestHandler::setSocket(QWebSocket *socket) {
    if (m_socket) {
        QObject::disconnect(m_socket, nullptr, this, nullptr);
    }

    m_socket = socket;
    if (m_socket) {
        connect(m_socket, &QWebSocket::disconnected, this, [this]() {
            m_ackTimer.stop();
            m_hasInFlight = false;
            m_inFlightId.clear();
            m_inFlightResponse = QJsonObject();
        });
    }
    trySendNextResponse();
}

void RequestHandler::handleTextMessage(const QString &message) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "JSON parse error" << error.errorString();
        enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = 0,
            .group = QStringLiteral("request"),
            .action = QStringLiteral("parse"),
            .parameters = QJsonObject{
                {QStringLiteral("error"), QStringLiteral("invalid_json")},
            },
            .success = false,
        }));
        return;
    }

    const QJsonObject obj = doc.object();
    if (isAck(obj)) {
        handleAck(obj);
        return;
    }

    if (!isValidTemplate(obj)) {
        qWarning() << "JSON template mismatch";
        enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = 0,
            .group = QStringLiteral("request"),
            .action = QStringLiteral("template"),
            .parameters = QJsonObject{
                {QStringLiteral("error"), QStringLiteral("invalid_template")},
            },
            .success = false,
        }));
        return;
    }

    QJsonObject normalized = obj;

    switch (decideQueueTarget(normalized)) {
    case RequestHandler::QueueTarget::Pcap:
        emit pcapEnqueueRequested(normalized);
        return;
    case RequestHandler::QueueTarget::SystemControl:
        emit systemControlEnqueueRequested(normalized);
        return;
    }
}

void RequestHandler::enqueueResponse(const QJsonObject &response) {
    m_responseQueue.enqueue(response);
    trySendNextResponse();
}

void RequestHandler::trySendNextResponse() {
    if (!m_socket || m_responseQueue.isEmpty() || m_hasInFlight) {
        return;
    }

    QJsonObject response = m_responseQueue.dequeue();
    const QString responseId = QStringLiteral("r-%1").arg(++m_responseIdCounter);
    response.insert(QLatin1String(kKeyResponseId), responseId);
    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    m_socket->sendTextMessage(QString::fromUtf8(payload));
    m_inFlightResponse = response;
    m_inFlightId = responseId;
    m_hasInFlight = true;
    m_ackTimer.start(2000);
}

void RequestHandler::handleAck(const QJsonObject &responseObj) {
    const QString responseId = responseObj.value(QLatin1String(kKeyResponseId)).toString();
    if (!m_hasInFlight || responseId != m_inFlightId) {
        return;
    }

    m_ackTimer.stop();
    m_hasInFlight = false;
    m_inFlightId.clear();
    m_inFlightResponse = QJsonObject();
    trySendNextResponse();
}

void RequestHandler::resendInFlight() {
    if (!m_socket || !m_hasInFlight) {
        return;
    }

    const QByteArray payload = QJsonDocument(m_inFlightResponse).toJson(QJsonDocument::Compact);
    m_socket->sendTextMessage(QString::fromUtf8(payload));
    m_ackTimer.start(2000);
}
