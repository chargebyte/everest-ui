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

int headerEndIndex(const QByteArray &data) {
    return data.indexOf("\r\n\r\n");
}

int contentLength(const QByteArray &data, int headerEnd) {
    const QList<QByteArray> lines = data.left(headerEnd).split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int sep = line.indexOf(':');
        if (sep <= 0) {
            continue;
        }
        if (line.left(sep).trimmed().toLower() != "content-length") {
            continue;
        }
        bool ok = false;
        const int length = line.mid(sep + 1).trimmed().toInt(&ok);
        return ok ? length : -1;
    }
    return 0;
}

void fillBadRequest(StaticResponse *errorResponse, const QByteArray &message) {
    if (!errorResponse) {
        return;
    }
    errorResponse->statusCode = 400;
    errorResponse->statusText = QStringLiteral("Bad Request");
    errorResponse->body = message;
    errorResponse->mime = QStringLiteral("text/plain");
}

QByteArray normalizedPathFrom(const QByteArray &path) {
    const int queryPos = path.indexOf('?');
    if (queryPos >= 0) {
        return path.left(queryPos);
    }
    return path;
}

void parseCookies(const QByteArray &cookieHeader, QHash<QByteArray, QByteArray> &cookies) {
    const QList<QByteArray> parts = cookieHeader.split(';');
    for (const QByteArray &part : parts) {
        const int sep = part.indexOf('=');
        if (sep <= 0) {
            continue;
        }
        const QByteArray key = part.left(sep).trimmed();
        const QByteArray value = part.mid(sep + 1).trimmed();
        if (!key.isEmpty()) {
            cookies.insert(key, value);
        }
    }
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

bool isRequestComplete(const QByteArray &peek, int maxRequestBytes, StaticResponse *errorResponse) {
    if (errorResponse) {
        *errorResponse = StaticResponse{};
    }

    if (peek.size() > maxRequestBytes) {
        if (errorResponse) {
            errorResponse->statusCode = 413;
            errorResponse->statusText = QStringLiteral("Payload Too Large");
            errorResponse->body = QByteArrayLiteral("Payload Too Large");
            errorResponse->mime = QStringLiteral("text/plain");
        }
        return false;
    }

    const int headerEnd = headerEndIndex(peek);
    if (headerEnd < 0) {
        return false;
    }

    const int bodyLength = contentLength(peek, headerEnd);
    if (bodyLength < 0) {
        fillBadRequest(errorResponse, QByteArrayLiteral("Invalid Content-Length"));
        return false;
    }

    const int totalLength = headerEnd + 4 + bodyLength;
    if (totalLength > maxRequestBytes) {
        if (errorResponse) {
            errorResponse->statusCode = 413;
            errorResponse->statusText = QStringLiteral("Payload Too Large");
            errorResponse->body = QByteArrayLiteral("Payload Too Large");
            errorResponse->mime = QStringLiteral("text/plain");
        }
        return false;
    }

    return peek.size() >= totalLength;
}

bool isRequestValid(const QByteArray &peek,
                    ParsedRequest &request,
                    StaticResponse *errorResponse) {
    if (errorResponse) {
        *errorResponse = StaticResponse{};
    }
    request = {};
    const int headerEnd = headerEndIndex(peek);
    if (headerEnd < 0) {
        fillBadRequest(errorResponse, QByteArrayLiteral("Bad Request"));
        return false;
    }

    request.lines = peek.left(headerEnd).split('\n');
    if (request.lines.isEmpty()) {
        fillBadRequest(errorResponse, QByteArrayLiteral("Bad Request"));
        return false;
    }

    const QByteArray requestLine = request.lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        fillBadRequest(errorResponse, QByteArrayLiteral("Bad Request"));
        return false;
    }

    request.method = parts.at(0);
    request.path = parts.at(1);
    request.normalizedPath = normalizedPathFrom(request.path);
    if (request.method != "GET" && request.method != "POST") {
        if (errorResponse) {
            errorResponse->statusCode = 405;
            errorResponse->statusText = QStringLiteral("Method Not Allowed");
            errorResponse->body = QByteArrayLiteral("Method Not Allowed");
            errorResponse->mime = QStringLiteral("text/plain");
        }
        return false;
    }

    for (int i = 1; i < request.lines.size(); ++i) {
        const QByteArray line = request.lines.at(i).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const int sep = line.indexOf(':');
        if (sep <= 0) {
            continue;
        }

        const QByteArray key = line.left(sep).trimmed().toLower();
        const QByteArray value = line.mid(sep + 1).trimmed();
        request.headers.insert(key, value);
        if (key == "cookie") {
            parseCookies(value, request.cookies);
        }
    }

    const int bodyLength = contentLength(peek, headerEnd);
    if (bodyLength < 0) {
        fillBadRequest(errorResponse, QByteArrayLiteral("Invalid Content-Length"));
        return false;
    }
    request.body = peek.mid(headerEnd + 4, bodyLength);
    return true;
}

bool isWebSocketUpgradeRequest(const ParsedRequest &request,
                               const QByteArray &wsPath,
                               bool enforceOrigin,
                               const QUrl &allowOriginUrl) {
    if (request.normalizedPath != wsPath) {
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
