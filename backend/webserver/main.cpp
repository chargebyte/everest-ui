// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "ServerConfig.hpp"
#include "StaticServer.hpp"
#include "AppTitleResolver.hpp"
#include "AuthManager.hpp"
#include "InstallPaths.hpp"
#include "UiOccupancyTracker.hpp"
#include "WebSocketProxySession.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQueue>
#include <QTcpSocket>
#include <QTextStream>
#include <QWebSocket>
#include <QWebSocketProtocol>
#include <QWebSocketServer>

namespace {
QString resolveFrontendConfigPath() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral(EVEREST_UI_INSTALL_FRONTEND_CONFIG),
        QDir(applicationDir).absoluteFilePath(QStringLiteral("../config/frontend.conf")),
        QDir(applicationDir).absoluteFilePath(QStringLiteral("../frontend.conf")),
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return candidates.constFirst();
}

struct PendingWebSocketUpgrade {
    QString sessionId;
    QString peerAddress;
};
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("webui"));

    const QString configPath = resolveFrontendConfigPath();
    ServerConfig cfg;
    QString configError;
    if (!loadAndValidateServerConfig(configPath, cfg, configError)) {
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
    QWebSocketServer wsServer(QStringLiteral("webui-ws"), QWebSocketServer::NonSecureMode);
    QQueue<PendingWebSocketUpgrade> pendingUpgrades;

    QObject::connect(&server, &StaticServer::webSocketUpgradeRequested,
                     &server, [&server, &wsServer, &pendingUpgrades](
                                  QTcpSocket *socket,
                                  const QString &sessionId,
                                  const QString &peerAddress) {
                         if (!socket) {
                            return;
                         }
                         pendingUpgrades.enqueue({sessionId, peerAddress});
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
                     [&wsServer, &cfg, &pendingUpgrades, &uiOccupancyTracker]() {
                         QWebSocket *client = wsServer.nextPendingConnection();
                         if (!client) {
                             return;
                         }
                         if (pendingUpgrades.isEmpty()) {
                             QTextStream(stdout) << "WS client accepted without upgrade metadata\n";
                             client->close(QWebSocketProtocol::CloseCodeProtocolError,
                                           QStringLiteral("missing upgrade metadata"));
                             client->deleteLater();
                             return;
                         }

                         const PendingWebSocketUpgrade pendingUpgrade = pendingUpgrades.dequeue();
                         if (cfg.debugLog) {
                             QTextStream(stdout) << "WS client accepted, proxying to "
                                                 << cfg.backendUrl.toString() << "\n";
                         }
                         // WebSocket-upgrade flow, Step 3 + Step 4:
                         // For this upgraded browser socket, create a proxy session that
                         // connects to backend WS and then bridges traffic both ways.
                         new WebSocketProxySession(client, cfg.backendUrl,
                                                   pendingUpgrade.sessionId,
                                                   pendingUpgrade.peerAddress,
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
