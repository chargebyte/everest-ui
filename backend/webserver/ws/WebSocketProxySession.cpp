// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "WebSocketProxySession.hpp"
#include "UiOccupancyTracker.hpp"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QTextStream>
#include <QWebSocket>
#include <QWebSocketProtocol>

namespace {
QString formatPeerAddress(const QHostAddress &address, quint16 port) {
    const QString addressText = address.toString();
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        return QStringLiteral("[%1]:%2").arg(addressText).arg(port);
    }
    return QStringLiteral("%1:%2").arg(addressText).arg(port);
}
}

WebSocketProxySession::WebSocketProxySession(QWebSocket *clientSocket,
                                             const QUrl &backendUrl,
                                             UiOccupancyTracker *uiOccupancyTracker,
                                             QObject *parent)
    : QObject(parent),
      m_client(clientSocket),
      m_uiOccupancyTracker(uiOccupancyTracker),
      m_backend(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)) {
    if (m_uiOccupancyTracker && !m_uiOccupancyTracker->tryClaim(peerAddress())) {
        QTextStream(stdout) << "Rejecting UI session from " << peerAddress();
        if (m_uiOccupancyTracker->hasOwner()) {
            QTextStream(stdout) << " while active UI session is held by "
                                << m_uiOccupancyTracker->ownerPeerAddress();
        }
        QTextStream(stdout) << "\n";
        m_client->close(QWebSocketProtocol::CloseCodePolicyViolated,
                        QStringLiteral("ui already in use"));
        deleteLater();
        return;
    }

    m_ownsOccupancy = m_uiOccupancyTracker != nullptr;
    if (m_ownsOccupancy) {
        QTextStream(stdout) << "Active UI session claimed by " << peerAddress() << "\n";
    }

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
        releaseOccupancy();
        if (m_backend->state() == QAbstractSocket::ConnectedState ||
            m_backend->state() == QAbstractSocket::ConnectingState) {
            m_backend->close(QWebSocketProtocol::CloseCodeGoingAway,
                             QStringLiteral("client disconnected"));
        }
        deleteLater();
    });

    // Close propagation: backend side closed -> close browser side.
    connect(m_backend, &QWebSocket::disconnected, this, [this]() {
        releaseOccupancy();
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
    releaseOccupancy();
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

void WebSocketProxySession::releaseOccupancy() {
    if (!m_ownsOccupancy || !m_uiOccupancyTracker) {
        return;
    }

    m_uiOccupancyTracker->release();
    QTextStream(stdout) << "Active UI session released for " << peerAddress() << "\n";
    m_ownsOccupancy = false;
}

QString WebSocketProxySession::peerAddress() const {
    return formatPeerAddress(m_client->peerAddress(), m_client->peerPort());
}
