// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef WEB_SOCKET_PROXY_SESSION_HPP
#define WEB_SOCKET_PROXY_SESSION_HPP

#include <QObject>
#include <QUrl>

class QWebSocket;

class WebSocketProxySession final : public QObject {
    Q_OBJECT

public:
    WebSocketProxySession(QWebSocket *clientSocket,
                          const QUrl &backendUrl,
                          QObject *parent = nullptr);

private:
    static constexpr int kMaxPendingMessages = 256;
    static constexpr qint64 kMaxPendingBytes = 2 * 1024 * 1024;

    bool canQueueMessage(qint64 messageBytes) const;
    void closeDueToQueueOverflow();

    QWebSocket *m_client = nullptr;
    QWebSocket *m_backend = nullptr;
    bool m_backendConnected = false;
    qint64 m_pendingBytes = 0;
    QList<QString> m_pendingText;
    QList<QByteArray> m_pendingBinary;
};

#endif // WEB_SOCKET_PROXY_SESSION_HPP
