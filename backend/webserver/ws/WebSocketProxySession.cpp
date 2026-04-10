// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "WebSocketProxySession.hpp"

#include <QAbstractSocket>
#include <QtGlobal>
#include <QWebSocket>
#include <QWebSocketProtocol>

WebSocketProxySession::WebSocketProxySession(QWebSocket *clientSocket,
                                             const QUrl &backendUrl,
                                             QObject *parent)
    : QObject(parent),
      m_client(clientSocket),
      m_backend(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)) {
    // WebSocket-upgrade flow, Step 3:
    // Once the backend WS is connected, flush browser messages queued during
    // backend connection setup.
    connect(m_backend, &QWebSocket::connected, this, [this]() {
        m_backendConnected = true;
        for (const QString &msg : m_pendingText) {
            m_backend->sendTextMessage(msg);
        }
        m_pendingText.clear();
        for (const QByteArray &msg : m_pendingBinary) {
            m_backend->sendBinaryMessage(msg);
        }
        m_pendingBinary.clear();
        m_pendingBytes = 0;
    });

    // WebSocket-upgrade flow, Step 4a: backend -> browser forwarding.
    connect(m_backend, &QWebSocket::textMessageReceived, this,
            [this](const QString &msg) { m_client->sendTextMessage(msg); });
    connect(m_backend, &QWebSocket::binaryMessageReceived, this,
            [this](const QByteArray &msg) { m_client->sendBinaryMessage(msg); });

    // WebSocket-upgrade flow, Step 4b: browser -> backend forwarding.
    // Buffer until backend WS connection is established.
    connect(m_client, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        if (m_backendConnected) {
            m_backend->sendTextMessage(msg);
        } else {
            const qint64 messageBytes = msg.toUtf8().size();
            if (!canQueueMessage(messageBytes)) {
                closeDueToQueueOverflow();
                return;
            }
            m_pendingText.append(msg);
            m_pendingBytes += messageBytes;
        }
    });
    connect(m_client, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &msg) {
        if (m_backendConnected) {
            m_backend->sendBinaryMessage(msg);
        } else {
            if (!canQueueMessage(msg.size())) {
                closeDueToQueueOverflow();
                return;
            }
            m_pendingBinary.append(msg);
            m_pendingBytes += msg.size();
        }
    });

    // Close propagation: browser side closed -> close backend side.
    connect(m_client, &QWebSocket::disconnected, this, [this]() {
        if (m_backend->state() == QAbstractSocket::ConnectedState ||
            m_backend->state() == QAbstractSocket::ConnectingState) {
            m_backend->close(QWebSocketProtocol::CloseCodeGoingAway,
                             QStringLiteral("client disconnected"));
        }
        deleteLater();
    });

    // Close propagation: backend side closed -> close browser side.
    connect(m_backend, &QWebSocket::disconnected, this, [this]() {
        if (m_client->state() == QAbstractSocket::ConnectedState) {
            m_client->close(m_backend->closeCode(), m_backend->closeReason());
        }
        deleteLater();
    });

    // Error propagation: backend connect/runtime error -> close browser side.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_backend, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_client->state() == QAbstractSocket::ConnectedState) {
            m_client->close(QWebSocketProtocol::CloseCodeBadOperation,
                            QStringLiteral("backend connection error"));
        }
    });
#else
    connect(m_backend, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, [this](QAbstractSocket::SocketError) {
                if (m_client->state() == QAbstractSocket::ConnectedState) {
                    m_client->close(QWebSocketProtocol::CloseCodeBadOperation,
                                    QStringLiteral("backend connection error"));
                }
            });
#endif

    // WebSocket-upgrade flow, Step 3 kickoff:
    // Start backend WS connection for this upgraded browser session.
    m_backend->open(backendUrl);
}

bool WebSocketProxySession::canQueueMessage(qint64 messageBytes) const {
    const int pendingMessageCount = m_pendingText.size() + m_pendingBinary.size();
    if (pendingMessageCount >= kMaxPendingMessages) {
        return false;
    }
    return (m_pendingBytes + messageBytes) <= kMaxPendingBytes;
}

void WebSocketProxySession::closeDueToQueueOverflow() {
    if (m_client->state() == QAbstractSocket::ConnectedState) {
        m_client->close(QWebSocketProtocol::CloseCodeTooMuchData,
                        QStringLiteral("proxy pending queue overflow"));
    }
    if (m_backend->state() == QAbstractSocket::ConnectedState ||
        m_backend->state() == QAbstractSocket::ConnectingState) {
        m_backend->close(QWebSocketProtocol::CloseCodeGoingAway,
                         QStringLiteral("proxy pending queue overflow"));
    }
    deleteLater();
}
