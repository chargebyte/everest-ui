// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "StaticServer.hpp"
#include "AppTitleResolver.hpp"
#include "AuthManager.hpp"
#include "RequestParsing.hpp"
#include "RequestSecurity.hpp"
#include "StaticContent.hpp"
#include "UiOccupancyTracker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QTextStream>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kHeaderTimeoutMs = 5000;

StaticResponse makeTextResponse(int statusCode, const QString &statusText, const QByteArray &body) {
    StaticResponse response;
    response.statusCode = statusCode;
    response.statusText = statusText;
    response.body = body;
    response.mime = QStringLiteral("text/plain");
    return response;
}

StaticResponse makeJsonResponse(int statusCode, const QString &statusText, const QJsonObject &body) {
    StaticResponse response;
    response.statusCode = statusCode;
    response.statusText = statusText;
    response.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    response.mime = QStringLiteral("application/json");
    response.headers.append({QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")});
    return response;
}

QJsonObject parseJsonObjectBody(const ParsedRequest &request, bool &ok) {
    ok = false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }
    ok = true;
    return document.object();
}

QByteArray sessionCookieHeader(const QString &sessionId) {
    return QByteArray(AuthManager::kSessionCookieName) + "=" + sessionId.toUtf8() +
           QByteArrayLiteral("; HttpOnly; SameSite=Strict; Path=/");
}

QByteArray clearSessionCookieHeader() {
    return QByteArray(AuthManager::kSessionCookieName) +
           QByteArrayLiteral("=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
}

QString formatPeerAddress(const QHostAddress &address, quint16 port) {
    const QString addressText = address.toString();
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        return QStringLiteral("[%1]:%2").arg(addressText).arg(port);
    }
    return QStringLiteral("%1:%2").arg(addressText).arg(port);
}

} // namespace

StaticServer::StaticServer(const ServerConfig &cfg,
                           AuthManager *authManager,
                           AppTitleResolver *appTitleResolver,
                           UiOccupancyTracker *uiOccupancyTracker,
                           QObject *parent)
    : QTcpServer(parent),
      m_passwordReset(cfg.passwordResetBootStatusPath, cfg.passwordResetWindowSeconds),
      m_allowedHosts(RequestSecurity::allowedHosts(cfg.allowedHosts, cfg.allowOriginUrl.host())),
      m_authManager(authManager),
      m_appTitleResolver(appTitleResolver),
      m_uiOccupancyTracker(uiOccupancyTracker),
      m_rootDir(cfg.canonicalRoot),
      m_wsPath(cfg.normalizedWsPath.toUtf8()),
      m_maxRequestBytes(cfg.maxRequestBytes),
      m_enforceOrigin(cfg.enforceOrigin),
      m_allowOriginUrl(cfg.allowOriginUrl) { m_passwordReset.initialize(); }


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
    if (!isRequestComplete(peek, m_maxRequestBytes, &response)) {
        if (response.statusCode != 500) {
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

    const bool sensitiveAuthMutation = request.normalizedPath == "/auth/reset" ||
                                       request.normalizedPath == "/auth/setup";
    if (sensitiveAuthMutation) {
        const int hostStatus = RequestSecurity::validateHost(request, m_allowedHosts);
        if (hostStatus != 200) {
            if (hostStatus == 421) {
                sendResponseAndClose(
                    socket,
                    makeJsonResponse(421, QStringLiteral("Misdirected Request"),
                                     QJsonObject{{QStringLiteral("error"),
                                                  QStringLiteral("host_not_allowed")}}));
            } else {
                sendResponseAndClose(socket, makeTextResponse(
                    hostStatus, QStringLiteral("Bad Request"), QByteArrayLiteral("Invalid Host")));
            }
            return;
        }
    }

    if (isAuthEndpoint(request.normalizedPath)) {
        socket->readAll();
        response = handleAuthRequest(request,
                                     formatPeerAddress(socket->peerAddress(), socket->peerPort()));
        sendResponseAndClose(socket, response);
        return;
    }

    // WebSocket-upgrade flow, Step 1:
    // Detect browser intent to upgrade this HTTP connection to WebSocket
    // ("GET /ws" + Upgrade headers). If detected, hand off this same TCP socket
    // to the WebSocket path instead of serving static content.
    if (isWebSocketUpgradeRequest(request, m_wsPath, m_enforceOrigin, m_allowOriginUrl)) {
        if (!isAuthenticated(request)) {
            response = makeTextResponse(401, QStringLiteral("Unauthorized"),
                                        QByteArrayLiteral("Unauthorized"));
            sendResponseAndClose(socket, response);
            return;
        }
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

    if (!isPublicFrontendAsset(request) && !isAuthenticated(request)) {
        response = makeTextResponse(401, QStringLiteral("Unauthorized"),
                                    QByteArrayLiteral("Unauthorized"));
        sendResponseAndClose(socket, response);
        return;
    }

    const QString requestPath = StaticContent::normalizeRequestPath(request.normalizedPath);
    response = StaticContent::loadFile(m_rootDir, requestPath);
    sendResponseAndClose(socket, response);
}

bool StaticServer::isAuthEndpoint(const QByteArray &path) const {
    return path == QByteArrayLiteral("/auth/status") || path == QByteArrayLiteral("/auth/setup") ||
           path == QByteArrayLiteral("/auth/login") || path == QByteArrayLiteral("/auth/logout") ||
           path == QByteArrayLiteral("/auth/reset");
}

bool StaticServer::isPublicFrontendAsset(const ParsedRequest &request) const {
    if (request.method != "GET") {
        return false;
    }

    const QByteArray path = request.normalizedPath;
    if (path == "/" || path == "/index.html") {
        return true;
    }
    return path.startsWith("/js/") || path.startsWith("/css/") || path.startsWith("/assets/");
}

bool StaticServer::isAuthenticated(const ParsedRequest &request) {
    if (!m_authManager || !m_authManager->hasUser()) {
        return false;
    }

    return m_authManager->validateSession(sessionIdFromRequest(request));
}

QString StaticServer::sessionIdFromRequest(const ParsedRequest &request) const {
    return QString::fromUtf8(request.cookies.value(QByteArray(AuthManager::kSessionCookieName)));
}

StaticResponse StaticServer::handleAuthRequest(const ParsedRequest &request,
                                               const QString &peerAddress) {
    if (!m_authManager) {
        return makeJsonResponse(500, QStringLiteral("Internal Server Error"),
                                QJsonObject{{QStringLiteral("error"), QStringLiteral("auth_unavailable")}});
    }

    if (request.normalizedPath == "/auth/status") {
        if (request.method != "GET") {
            return makeTextResponse(405, QStringLiteral("Method Not Allowed"),
                                    QByteArrayLiteral("Method Not Allowed"));
        }

        const bool authenticated = isAuthenticated(request);
        const bool uiBusy = authenticated && m_uiOccupancyTracker && m_uiOccupancyTracker->isBusy();
        if (uiBusy) {
            QTextStream(stdout) << "Blocked UI access from " << peerAddress
                                << " while active UI session is held by "
                                << m_uiOccupancyTracker->ownerPeerAddress() << "\n";
        }

        return makeJsonResponse(200, QStringLiteral("OK"),
                                QJsonObject{{QStringLiteral("setupRequired"),
                                             m_authManager->setupRequired()},
                                            {QStringLiteral("authenticated"),
                                             authenticated},
                                            {QStringLiteral("uiBusy"), uiBusy},
                                            {QStringLiteral("passwordReset"), m_passwordReset.status(m_authManager->hasUser())},
                                            {QStringLiteral("appTitle"),
                                             m_appTitleResolver
                                                 ? m_appTitleResolver->title()
                                                 : QStringLiteral("EVerest WebUI")}});
    }

    if (request.method != "POST") {
        return makeTextResponse(405, QStringLiteral("Method Not Allowed"),
                                QByteArrayLiteral("Method Not Allowed"));
    }

    if (request.normalizedPath == "/auth/reset" || request.normalizedPath == "/auth/setup") {
        const int validation = RequestSecurity::validateAuthMutation(request);
        if (validation != 200) {
            if (validation == 500) {
                return makeJsonResponse(
                    500, QStringLiteral("Internal Server Error"),
                    QJsonObject{{QStringLiteral("error"), QStringLiteral("internal_error")}});
            }
            if (validation == 415) {
                return makeJsonResponse(
                    415, QStringLiteral("Unsupported Media Type"),
                    QJsonObject{{QStringLiteral("error"),
                                 QStringLiteral("unsupported_media_type")}});
            }
            return makeJsonResponse(
                validation, QStringLiteral("Forbidden"),
                QJsonObject{{QStringLiteral("error"), QStringLiteral("origin_forbidden")}});
        }
    }

    if (request.normalizedPath == "/auth/reset") {
        bool valid = false;
        const auto body = parseJsonObjectBody(request, valid);
        if (!valid || body.value("confirm") != QJsonValue(true)) {
            return makeJsonResponse(400, "Bad Request", QJsonObject{{"error", "reset_confirmation_required"}});
        }
        QString error;
        const int code = m_passwordReset.reset(*m_authManager, error);
        if (code != 200) {
            return makeJsonResponse(code, code == 503 ? "Service Unavailable" :
                code == 403 ? "Forbidden" : "Internal Server Error", QJsonObject{{"error", error}});
        }
        emit credentialsReset();
        auto response = makeJsonResponse(200, "OK", QJsonObject{{"success", true}});
        response.headers.append({"Set-Cookie", clearSessionCookieHeader()});
        return response;
    }

    if (request.normalizedPath == "/auth/logout") {
        m_authManager->removeSession(QString::fromUtf8(
            request.cookies.value(QByteArray(AuthManager::kSessionCookieName))));
        StaticResponse response = makeJsonResponse(
            200, QStringLiteral("OK"), QJsonObject{{QStringLiteral("success"), true}});
        response.headers.append({QByteArrayLiteral("Set-Cookie"), clearSessionCookieHeader()});
        return response;
    }

    bool validJson = false;
    const QJsonObject body = parseJsonObjectBody(request, validJson);
    if (!validJson) {
        return makeJsonResponse(400, QStringLiteral("Bad Request"),
                                QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid_json")}});
    }

    const QString username = body.value(QStringLiteral("username")).toString();
    const QString password = body.value(QStringLiteral("password")).toString();
    if (username.isEmpty() || password.isEmpty()) {
        return makeJsonResponse(
            400, QStringLiteral("Bad Request"),
            QJsonObject{{QStringLiteral("error"), QStringLiteral("missing_credentials")}});
    }

    if (request.normalizedPath == "/auth/setup") {
        if (!m_authManager->setupRequired()) {
            return makeJsonResponse(
                409, QStringLiteral("Conflict"),
                QJsonObject{{QStringLiteral("error"), QStringLiteral("setup_not_required")}});
        }

        QString errorMessage;
        if (!m_authManager->createUser(username, password, errorMessage)) {
            return makeJsonResponse(400, QStringLiteral("Bad Request"),
                                    QJsonObject{{QStringLiteral("error"), errorMessage}});
        }
        return makeJsonResponse(200, QStringLiteral("OK"),
                                QJsonObject{{QStringLiteral("success"), true}});
    }

    if (request.normalizedPath == "/auth/login") {
        if (!m_authManager->hasUser()) {
            return makeJsonResponse(
                403, QStringLiteral("Forbidden"),
                QJsonObject{{QStringLiteral("error"), QStringLiteral("setup_required")}});
        }
        if (!m_authManager->authenticate(username, password)) {
            return makeJsonResponse(
                401, QStringLiteral("Unauthorized"),
                QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid_credentials")}});
        }

        const QString sessionId = m_authManager->createSession(username);
        if (sessionId.isEmpty()) {
            return makeJsonResponse(
                500, QStringLiteral("Internal Server Error"),
                QJsonObject{{QStringLiteral("error"), QStringLiteral("session_failed")}});
        }

        StaticResponse response = makeJsonResponse(
            200, QStringLiteral("OK"), QJsonObject{{QStringLiteral("success"), true}});
        response.headers.append({QByteArrayLiteral("Set-Cookie"), sessionCookieHeader(sessionId)});
        return response;
    }

    return makeJsonResponse(404, QStringLiteral("Not Found"),
                            QJsonObject{{QStringLiteral("error"), QStringLiteral("not_found")}});
}

void StaticServer::sendResponseAndClose(QTcpSocket *socket, const StaticResponse &response) {
    socket->write(response.toHttpWire());
    socket->disconnectFromHost();
}
