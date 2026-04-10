// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RequestParsing.hpp"

#include <QString>

namespace {
int effectivePort(const QUrl &url) {
    if (url.port() > 0) {
        return url.port();
    }
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
        return 443;
    }
    return 80;
}

bool isAllowedOrigin(const QByteArray &originHeaderValue,
                     bool enforceOrigin,
                     const QUrl &allowOriginUrl) {
    if (!enforceOrigin) {
        return true;
    }

    const QUrl originUrl(QString::fromUtf8(originHeaderValue.trimmed()));
    if (!originUrl.isValid() || originUrl.host().isEmpty()) {
        return false;
    }

    return originUrl.scheme().compare(allowOriginUrl.scheme(), Qt::CaseInsensitive) == 0 &&
           originUrl.host().compare(allowOriginUrl.host(), Qt::CaseInsensitive) == 0 &&
           effectivePort(originUrl) == effectivePort(allowOriginUrl);
}
} // namespace

bool isHeaderComplete(const QByteArray &peek, int maxRequestBytes, StaticResponse *errorResponse) {
    if (errorResponse) {
        *errorResponse = StaticResponse{};
    }

    if (peek.contains("\r\n\r\n")) {
        return true;
    }

    if (peek.size() > maxRequestBytes && errorResponse) {
        errorResponse->statusCode = 431;
        errorResponse->statusText = QStringLiteral("Request Header Fields Too Large");
        errorResponse->body = QByteArrayLiteral("Request Header Fields Too Large");
        errorResponse->mime = QStringLiteral("text/plain");
    }

    return false;
}

bool isRequestValid(const QByteArray &peek,
                    ParsedRequest &request,
                    StaticResponse *errorResponse) {
    if (errorResponse) {
        *errorResponse = StaticResponse{};
    }
    request = {};
    request.lines = peek.split('\n');
    if (request.lines.isEmpty()) {
        if (errorResponse) {
            errorResponse->statusCode = 400;
            errorResponse->statusText = QStringLiteral("Bad Request");
            errorResponse->body = QByteArrayLiteral("Bad Request");
            errorResponse->mime = QStringLiteral("text/plain");
        }
        return false;
    }

    const QByteArray requestLine = request.lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        if (errorResponse) {
            errorResponse->statusCode = 400;
            errorResponse->statusText = QStringLiteral("Bad Request");
            errorResponse->body = QByteArrayLiteral("Bad Request");
            errorResponse->mime = QStringLiteral("text/plain");
        }
        return false;
    }

    request.method = parts.at(0);
    request.path = parts.at(1);
    if (request.method != "GET") {
        if (errorResponse) {
            errorResponse->statusCode = 405;
            errorResponse->statusText = QStringLiteral("Method Not Allowed");
            errorResponse->body = QByteArrayLiteral("Method Not Allowed");
            errorResponse->mime = QStringLiteral("text/plain");
        }
        return false;
    }
    return true;
}

bool isWebSocketUpgradeRequest(const ParsedRequest &request,
                               const QByteArray &wsPath,
                               bool enforceOrigin,
                               const QUrl &allowOriginUrl) {
    QByteArray normalizedPath = request.path;
    const int queryPos = normalizedPath.indexOf('?');
    if (queryPos >= 0) {
        normalizedPath = normalizedPath.left(queryPos);
    }
    if (normalizedPath != wsPath) {
        return false;
    }

    bool hasUpgrade = false;
    bool hasConnection = false;
    bool hasAllowedOrigin = !enforceOrigin;
    for (int i = 1; i < request.lines.size(); ++i) {
        const QByteArray line = request.lines.at(i).trimmed();
        if (line.isEmpty()) {
            break;
        }

        const int sep = line.indexOf(':');
        if (sep <= 0) {
            continue;
        }

        const QByteArray key = line.left(sep).trimmed().toLower();
        const QByteArray value = line.mid(sep + 1).trimmed().toLower();
        if (key == "upgrade" && value == "websocket") {
            hasUpgrade = true;
            continue;
        }
        if (key == "connection" && value.contains("upgrade")) {
            hasConnection = true;
        }
        if (key == "origin") {
            hasAllowedOrigin = isAllowedOrigin(line.mid(sep + 1), enforceOrigin, allowOriginUrl);
        }
    }

    return hasUpgrade && hasConnection && hasAllowedOrigin;
}
