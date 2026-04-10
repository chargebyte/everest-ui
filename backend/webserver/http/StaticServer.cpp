// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "StaticServer.hpp"
#include "RequestParsing.hpp"
#include "StaticContent.hpp"

#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kHeaderTimeoutMs = 5000;

} // namespace

StaticServer::StaticServer(const ServerConfig &cfg, QObject *parent)
    : QTcpServer(parent),
      m_rootDir(cfg.canonicalRoot),
      m_wsPath(cfg.normalizedWsPath.toUtf8()),
      m_maxRequestBytes(cfg.maxRequestBytes),
      m_enforceOrigin(cfg.enforceOrigin),
      m_allowOriginUrl(cfg.allowOriginUrl) {}


void StaticServer::incomingConnection(qintptr handle) {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(handle)) {
        socket->deleteLater();
        return;
    }

    auto *headerTimer = new QTimer(socket);
    headerTimer->setSingleShot(true);
    headerTimer->start(kHeaderTimeoutMs);

    connect(headerTimer, &QTimer::timeout, socket, [socket]() {
        if (socket->state() == QAbstractSocket::ConnectedState) {
            StaticResponse response;
            response.statusCode = 408;
            response.statusText = QStringLiteral("Request Timeout");
            response.body = QByteArrayLiteral("Request Timeout");
            response.mime = QStringLiteral("text/plain");
            StaticServer::sendResponseAndClose(socket, response);
        }
    });

    connect(socket, &QTcpSocket::readyRead, this, [this, socket, headerTimer]() {
        handleRequest(socket, headerTimer);
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void StaticServer::handleRequest(QTcpSocket *socket, QTimer *headerTimer) {
    const QByteArray peek = socket->peek(m_maxRequestBytes + 1);
    StaticResponse response;
    if (!isHeaderComplete(peek, m_maxRequestBytes, &response)) {
        if (response.statusCode == 431) {
            sendResponseAndClose(socket, response);
        }
        return;
    }
    headerTimer->stop();

    ParsedRequest request;
    if (!isRequestValid(peek, request, &response)) {
        sendResponseAndClose(socket, response);
        return;
    }

    // WebSocket-upgrade flow, Step 1:
    // Detect browser intent to upgrade this HTTP connection to WebSocket
    // ("GET /ws" + Upgrade headers). If detected, hand off this same TCP socket
    // to the WebSocket path instead of serving static content.
    if (isWebSocketUpgradeRequest(request, m_wsPath, m_enforceOrigin, m_allowOriginUrl)) {
        emit webSocketUpgradeRequested(socket);
        if (socket->parent() == this) {
            response.statusCode = 501;
            response.statusText = QStringLiteral("Not Implemented");
            response.body = QByteArrayLiteral("WebSocket endpoint not available yet");
            response.mime = QStringLiteral("text/plain");
            sendResponseAndClose(socket, response);
        }
        return;
    }

    // drain already-parsed HTTP request bytes from the socket
    const QByteArray data = socket->readAll();
    Q_UNUSED(data);

    const QString requestPath = StaticContent::normalizeRequestPath(request.path);
    response = StaticContent::loadFile(m_rootDir, requestPath);
    sendResponseAndClose(socket, response);
}

void StaticServer::sendResponseAndClose(QTcpSocket *socket, const StaticResponse &response) {
    socket->write(response.toHttpWire());
    socket->disconnectFromHost();
}
