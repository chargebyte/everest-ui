// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef STATIC_SERVER_HPP
#define STATIC_SERVER_HPP

#include "ServerConfig.hpp"
#include "StaticResponse.hpp"

#include <QTcpServer>

class AuthManager;
struct ParsedRequest;
class QTcpSocket;
class QTimer;

class StaticServer final : public QTcpServer {
    Q_OBJECT

public:
    explicit StaticServer(const ServerConfig &cfg, AuthManager *authManager, QObject *parent = nullptr);

signals:
    void webSocketUpgradeRequested(QTcpSocket *socket);

protected:
    void incomingConnection(qintptr handle) override;

private:
    void handleRequest(QTcpSocket *socket, QTimer *headerTimer);
    bool isAuthEndpoint(const QByteArray &path) const;
    bool isPublicFrontendAsset(const ParsedRequest &request) const;
    bool isAuthenticated(const ParsedRequest &request);
    StaticResponse handleAuthRequest(const ParsedRequest &request);
    static void sendResponseAndClose(QTcpSocket *socket, const StaticResponse &response);

    AuthManager *m_authManager = nullptr;
    QString m_rootDir;
    QByteArray m_wsPath;
    int m_maxRequestBytes = 8192;
    bool m_enforceOrigin = false;
    QUrl m_allowOriginUrl;
};

#endif // STATIC_SERVER_HPP
