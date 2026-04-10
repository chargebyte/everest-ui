// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef STATIC_SERVER_HPP
#define STATIC_SERVER_HPP

#include "ServerConfig.hpp"
#include "StaticResponse.hpp"

#include <QTcpServer>

class QTcpSocket;
class QTimer;

class StaticServer final : public QTcpServer {
    Q_OBJECT

public:
    explicit StaticServer(const ServerConfig &cfg, QObject *parent = nullptr);

signals:
    void webSocketUpgradeRequested(QTcpSocket *socket);

protected:
    void incomingConnection(qintptr handle) override;

private:
    void handleRequest(QTcpSocket *socket, QTimer *headerTimer);
    static void sendResponseAndClose(QTcpSocket *socket, const StaticResponse &response);

    QString m_rootDir;
    QByteArray m_wsPath;
    int m_maxRequestBytes = 8192;
    bool m_enforceOrigin = false;
    QUrl m_allowOriginUrl;
};

#endif // STATIC_SERVER_HPP
