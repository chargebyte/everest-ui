// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "ServerConfig.hpp"
#include "StaticServer.hpp"
#include "AppTitleResolver.hpp"
#include "AuthManager.hpp"
#include "UiOccupancyTracker.hpp"
#include "WebSocketProxySession.hpp"

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTextStream>
#include <QWebSocket>
#include <QWebSocketProtocol>
#include <QWebSocketServer>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("everest-ui-webserver"));

    const UiConfig::Paths configPaths = UiConfig::pathsFor(UiConfig::Kind::Frontend);
    ServerConfig cfg;
    QString configError;
    if (!loadAndValidateServerConfig(configPaths, cfg, configError)) {
        QTextStream(stderr) << configError << "\n";
        return 1;
    }

    AuthManager authManager(cfg.canonicalAuthFile);
    QString authError;
    if (!authManager.initialize(authError)) {
        QTextStream(stderr) << authError << "\n";
        return 1;
    }

    AppTitleResolver appTitleResolver(cfg.appTitle, cfg.backendUrl);
    UiOccupancyTracker uiOccupancyTracker;
    StaticServer server(cfg, &authManager, &appTitleResolver, &uiOccupancyTracker);
    QWebSocketServer wsServer(QStringLiteral("everest-ui-ws"), QWebSocketServer::NonSecureMode);

    QObject::connect(&server, &StaticServer::webSocketUpgradeRequested,
                     &server, [&server, &wsServer](QTcpSocket *socket) {
                         if (!socket) {
                            return;
                         }
                         // WebSocket-upgrade flow, Step 2:
                         // Pass the same TCP socket to QWebSocketServer so Qt completes
                         // the HTTP->WebSocket upgrade handshake (HTTP 101).
                         // After this, the browser-side connection is available as
                         // QWebSocket via nextPendingConnection().
                         socket->disconnect(&server);
                         socket->setParent(&wsServer);
                         wsServer.handleConnection(socket);
                     });

    QObject::connect(&wsServer, &QWebSocketServer::newConnection, &wsServer,
                     [&wsServer, &cfg, &uiOccupancyTracker]() {
                         QWebSocket *client = wsServer.nextPendingConnection();
                         if (!client) {
                             return;
                         }
                         if (cfg.debugLog) {
                             QTextStream(stdout) << "WS client accepted, proxying to "
                                                 << cfg.backendUrl.toString() << "\n";
                         }
                         // WebSocket-upgrade flow, Step 3 + Step 4:
                         // For this upgraded browser socket, create a proxy session that
                         // connects to backend WS and then bridges traffic both ways.
                         new WebSocketProxySession(client, cfg.backendUrl,
                                                   &uiOccupancyTracker, client);
                     });

    if (!server.listen(cfg.bindAddress, cfg.port)) {
        QTextStream(stderr) << "Failed to listen on port " << cfg.port << "\n";
        return 1;
    }

    QTextStream(stdout) << "Serving " << cfg.canonicalRoot
                        << " on http://" << cfg.bind << ":" << cfg.port << "\n"
                        << "WS endpoint: " << cfg.normalizedWsPath << "\n"
                        << "WS backend: " << cfg.backendUrl.toString() << "\n"
                        << "Auth file: " << cfg.canonicalAuthFile << "\n"
                        << "Max request bytes: " << cfg.maxRequestBytes << "\n"
                        << "Log level: " << cfg.logLevel << "\n";
    if (cfg.enforceOrigin) {
        QTextStream(stdout) << "Allowed Origin: "
                            << cfg.allowOriginUrl.toString(QUrl::FullyDecoded)
                            << "\n";
    }
    return app.exec();
}
