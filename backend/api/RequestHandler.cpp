// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RequestHandler.hpp"

#include "ProtocolSchema.hpp"
#include "ResponseBuilder.hpp"

#include <QFile>
#include <QDataStream>
#include <QJsonDocument>
#include <QWebSocket>

namespace {
bool isAck(const QJsonObject &obj) {
    const QString type = obj.value(QLatin1String(kKeyType)).toString();
    return type == QLatin1String(kTypeAck);
}

bool isPcapChunkAck(const QJsonObject &obj) {
    return obj.value(QLatin1String(kKeyType)).toString() == QLatin1String(kPcapChunkAckType);
}
} // namespace

RequestHandler::RequestHandler(QObject *parent)
    : QObject(parent) {
    m_ackTimer.setSingleShot(true);
    connect(&m_ackTimer, &QTimer::timeout, this, &RequestHandler::resendInFlight);
}

void RequestHandler::setSocket(QWebSocket *socket) {
    if (m_socket) {
        QObject::disconnect(m_socket, nullptr, this, nullptr);
        clearSession();
        m_socket = nullptr;
    }

    if (!socket) {
        clearSession();
        m_socket = nullptr;
        return;
    }

    m_socket = socket;
    ++m_connectionGeneration;
    if (m_socket) {
        connect(m_socket, &QWebSocket::disconnected, this, [this]() {
            if (sender() == m_socket) {
                clearSession();
                m_socket = nullptr;
            }
        });
    }
    trySendNextResponse();
}

void RequestHandler::handleTextMessage(const QString &message) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "JSON parse error" << error.errorString();
        enqueueCurrentResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = 0,
            .group = QStringLiteral("request"),
            .action = QStringLiteral("parse"),
            .parameters = QJsonObject{
                {QLatin1String(kError), QStringLiteral("invalid_json")},
            },
            .success = false,
            .final = true,
        }));
        return;
    }

    const QJsonObject obj = doc.object();
    if (isPcapChunkAck(obj)) {
        handlePcapChunkAck(obj);
        return;
    }
    if (isAck(obj)) {
        handleAck(obj);
        return;
    }

    if (!isValidTemplate(obj)) {
        qWarning() << "JSON template mismatch";
        enqueueCurrentResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = 0,
            .group = QStringLiteral("request"),
            .action = QStringLiteral("template"),
            .parameters = QJsonObject{
                {QLatin1String(kError), QStringLiteral("invalid_template")},
            },
            .success = false,
            .final = true,
        }));
        return;
    }

    const ModuleRequest request = toModuleRequest(obj);
    const qint64 clientRequestId = request.requestId;
    ModuleRequest routedRequest = request;
    routedRequest.requestId = ++m_serverRequestIdCounter;
    m_requestContexts.insert(routedRequest.requestId, RequestContext{
        .clientRequestId = clientRequestId,
        .connectionGeneration = m_connectionGeneration,
        .group = obj.value(FieldNameKey(FieldName::Group)).toString(),
        .action = routedRequest.action,
    });
    if (routedRequest.group == ModuleGroup::Unknown) {
        const QString group = obj.value(FieldNameKey(FieldName::Group)).toString();
        enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = routedRequest.requestId,
            .group = group,
            .action = routedRequest.action,
            .parameters = QJsonObject{
                {QLatin1String(kError), QStringLiteral("unsupported_group")},
            },
            .success = false,
            .final = true,
        }));
        return;
    }

    switch (routedRequest.group) {
    case ModuleGroup::PCAP:
        emit pcapEnqueueRequested(routedRequest);
        return;
    case ModuleGroup::EverestConfig:
    case ModuleGroup::SafetyController:
    case ModuleGroup::OCPPConfig:
    case ModuleGroup::FirmwareUpdate:
    case ModuleGroup::SystemLogs:
    case ModuleGroup::System:
    case ModuleGroup::Unknown:
        emit systemControlEnqueueRequested(routedRequest);
        return;
    }
}

void RequestHandler::enqueueResponse(const QJsonObject &response) {
    if (!m_socket) {
        return;
    }

    const qint64 serverRequestId = static_cast<qint64>(response.value(QLatin1String(kKeyRequestId)).toDouble());
    const auto contextIt = m_requestContexts.constFind(serverRequestId);
    if (contextIt == m_requestContexts.constEnd()) {
        return;
    }
    if (contextIt->connectionGeneration != m_connectionGeneration) {
        m_requestContexts.remove(serverRequestId);
        return;
    }

    RequestContext context = contextIt.value();
    QJsonObject clientResponse = response;
    clientResponse.insert(QLatin1String(kKeyRequestId), context.clientRequestId);

    if (context.group == QLatin1String(kGroupPcap) &&
        context.action == QLatin1String(kActionRead)) {
        const QJsonObject parameters = response.value(QLatin1String(kKeyParameters)).toObject();
        const QJsonValue captureRequestId = parameters.value(QLatin1String(kKeyCaptureRequestId));
        if (captureRequestId.isDouble()) {
            context.relatedPcapWriteRequestId = static_cast<qint64>(captureRequestId.toDouble());
            m_requestContexts.insert(serverRequestId, context);
            QJsonObject clientParameters = clientResponse.value(QLatin1String(kKeyParameters)).toObject();
            clientParameters.remove(QLatin1String(kKeyCaptureRequestId));
            clientResponse.insert(QLatin1String(kKeyParameters), clientParameters);
        }
    }

    const bool final = response.value(QLatin1String(kKeyFinal)).toBool(true);
    if (final && !retainContext(response, context)) {
        m_requestContexts.remove(serverRequestId);
    }
    if (final && context.group == QLatin1String(kGroupPcap) &&
        context.action == QLatin1String(kActionRead)) {
        removePcapWriteContext(context.relatedPcapWriteRequestId);
    }

    enqueueResponseObject(clientResponse);
}

void RequestHandler::clearSession() {
    m_ackTimer.stop();
    m_hasInFlight = false;
    m_inFlightId.clear();
    m_inFlightResponse = QJsonObject();
    m_inFlightServerRequestId = -1;
    m_responseQueue.clear();
    m_requestContexts.clear();
}

void RequestHandler::enqueueCurrentResponse(const QJsonObject &response) {
    if (!m_socket) {
        return;
    }
    enqueueResponseObject(response);
}

void RequestHandler::enqueueResponseObject(const QJsonObject &response) {
    m_responseQueue.enqueue(response);
    trySendNextResponse();
}

bool RequestHandler::retainContext(const QJsonObject &response, const RequestContext &context) const {
    if (!response.value(QLatin1String(kKeyFinal)).toBool(true)) {
        return true;
    }
    if (context.group != QLatin1String(kGroupPcap) || context.action != QLatin1String(kActionWrite)) {
        return false;
    }

    const bool success = response.value(QLatin1String(kKeyOk)).toBool(false);
    const QJsonObject parameters = response.value(QLatin1String(kKeyParameters)).toObject();
    return success || parameters.value(QLatin1String(kError)).toString() ==
                         QLatin1String(kErrorPcapLimitReached);
}

void RequestHandler::removePcapWriteContext(qint64 serverRequestId) {
    if (serverRequestId <= 0) {
        return;
    }
    const auto it = m_requestContexts.constFind(serverRequestId);
    if (it != m_requestContexts.constEnd() &&
        it->connectionGeneration == m_connectionGeneration &&
        it->group == QLatin1String(kGroupPcap) &&
        it->action == QLatin1String(kActionWrite)) {
        m_requestContexts.remove(serverRequestId);
    }
}

qint64 RequestHandler::findPcapReadContext(qint64 clientRequestId) const {
    qint64 result = -1;
    for (auto it = m_requestContexts.constBegin(); it != m_requestContexts.constEnd(); ++it) {
        if (it->connectionGeneration == m_connectionGeneration &&
            it->clientRequestId == clientRequestId &&
            it->group == QLatin1String(kGroupPcap) &&
            it->action == QLatin1String(kActionRead) && it.key() > result) {
            result = it.key();
        }
    }
    return result;
}

void RequestHandler::handlePcapChunkAck(const QJsonObject &object) {
    const QJsonValue requestIdValue = object.value(QLatin1String(kKeyRequestId));
    const QJsonValue sequenceValue = object.value(QLatin1String(kKeySequence));
    if (!requestIdValue.isDouble() || !sequenceValue.isDouble()) {
        return;
    }
    const qint64 serverRequestId = findPcapReadContext(static_cast<qint64>(requestIdValue.toDouble()));
    if (serverRequestId <= 0) {
        return;
    }
    emit pcapChunkRequested(serverRequestId,
                            static_cast<quint32>(sequenceValue.toDouble()) + 1);
}

void RequestHandler::enqueuePcapChunk(qint64 serverRequestId, quint32 sequence, bool final,
                                      const QByteArray &payload) {
    if (!m_socket) {
        return;
    }
    const auto contextIt = m_requestContexts.constFind(serverRequestId);
    if (contextIt == m_requestContexts.constEnd() ||
        contextIt->connectionGeneration != m_connectionGeneration ||
        contextIt->group != QLatin1String(kGroupPcap) ||
        contextIt->action != QLatin1String(kActionRead)) {
        return;
    }

    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.writeRawData(kPcapBinaryMagic, 4);
    stream << kPcapBinaryVersion << static_cast<quint8>(final ? 1 : 0)
           << static_cast<quint16>(0) << static_cast<quint64>(contextIt->clientRequestId)
           << sequence;
    frame.append(payload);
    m_socket->sendBinaryMessage(frame);

    if (final) {
        const qint64 relatedWriteRequestId = contextIt->relatedPcapWriteRequestId;
        m_requestContexts.remove(serverRequestId);
        removePcapWriteContext(relatedWriteRequestId);
    }
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
    m_inFlightServerRequestId = -1;
    for (auto it = m_requestContexts.constBegin(); it != m_requestContexts.constEnd(); ++it) {
        if (it->connectionGeneration == m_connectionGeneration &&
            it->clientRequestId == static_cast<qint64>(response.value(QLatin1String(kKeyRequestId)).toDouble()) &&
            it->group == QLatin1String(kGroupPcap) && it->action == QLatin1String(kActionRead)) {
            m_inFlightServerRequestId = it.key();
            break;
        }
    }
    m_hasInFlight = true;
    m_ackTimer.start(2000);
}

void RequestHandler::handleAck(const QJsonObject &responseObj) {
    const QString responseId = responseObj.value(QLatin1String(kKeyResponseId)).toString();
    if (!m_hasInFlight || responseId != m_inFlightId) {
        return;
    }

    const bool startsPcapTransfer =
        m_inFlightResponse.value(QLatin1String(kKeyParameters)).toObject()
            .value(QLatin1String(kKeyTransfer)).toString() == QLatin1String(kTransferBinary);
    const qint64 serverRequestId = m_inFlightServerRequestId;
    m_ackTimer.stop();
    m_hasInFlight = false;
    m_inFlightId.clear();
    m_inFlightResponse = QJsonObject();
    m_inFlightServerRequestId = -1;
    if (startsPcapTransfer && serverRequestId > 0) {
        emit pcapChunkRequested(serverRequestId, 0);
    }
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

bool RequestHandler::isValidTemplate(const QJsonObject &obj) {
    const QJsonValue requestId = obj.value(FieldNameKey(FieldName::RequestId));
    const QJsonValue groupValue = obj.value(FieldNameKey(FieldName::Group));
    const QJsonValue actionValue = obj.value(FieldNameKey(FieldName::Action));
    const QJsonValue parametersValue = obj.value(FieldNameKey(FieldName::Parameters));

    return requestId.isDouble() && groupValue.isString() && actionValue.isString() &&
        parametersValue.isObject();
}

ModuleRequest RequestHandler::toModuleRequest(const QJsonObject &obj) {
    const QString group = obj.value(FieldNameKey(FieldName::Group)).toString();
    const ModuleGroup moduleGroup = toModuleGroup(group);
    if (moduleGroup == ModuleGroup::Unknown) {
        qWarning().noquote() << "Unsupported request group:" << group;
    }

    return ModuleRequest{
        .requestId = static_cast<qint64>(obj.value(FieldNameKey(FieldName::RequestId)).toDouble()),
        .group = moduleGroup,
        .action = obj.value(FieldNameKey(FieldName::Action)).toString(),
        .parameters = obj.value(FieldNameKey(FieldName::Parameters)).toObject(),
    };
}

ModuleGroup RequestHandler::toModuleGroup(const QString &group) {
    if (group == QLatin1String(kGroupPcap)) {
        return ModuleGroup::PCAP;
    }

    if (group == QLatin1String(kGroupEverest)) {
        return ModuleGroup::EverestConfig;
    }
    if (group == QLatin1String(kGroupSafety)) {
        return ModuleGroup::SafetyController;
    }
    if (group == QLatin1String(kGroupOcpp)) {
        return ModuleGroup::OCPPConfig;
    }
    if (group == QLatin1String(kGroupFirmware)) {
        return ModuleGroup::FirmwareUpdate;
    }
    if (group == QLatin1String(kGroupSystemLogs)) {
        return ModuleGroup::SystemLogs;
    }
    if (group == QLatin1String(kGroupSystem)) {
        return ModuleGroup::System;
    }

    return ModuleGroup::Unknown;
}
