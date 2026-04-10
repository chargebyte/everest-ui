// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "StaticContent.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
QString guessMime(const QString &path) {
    if (path.endsWith(QStringLiteral(".html"))) return QStringLiteral("text/html; charset=utf-8");
    if (path.endsWith(QStringLiteral(".js"))) return QStringLiteral("application/javascript; charset=utf-8");
    if (path.endsWith(QStringLiteral(".css"))) return QStringLiteral("text/css; charset=utf-8");
    if (path.endsWith(QStringLiteral(".json"))) return QStringLiteral("application/json; charset=utf-8");
    if (path.endsWith(QStringLiteral(".png"))) return QStringLiteral("image/png");
    if (path.endsWith(QStringLiteral(".svg"))) return QStringLiteral("image/svg+xml");
    return QStringLiteral("application/octet-stream");
}
} // namespace

QString StaticContent::normalizeRequestPath(const QByteArray &path) {
    QByteArray normalized = path;
    const int queryPos = normalized.indexOf('?');
    if (queryPos >= 0) {
        normalized = normalized.left(queryPos);
    }
    if (normalized == "/" || normalized.isEmpty()) {
        normalized = "/index.html";
    }
    return QString::fromUtf8(normalized);
}

StaticResponse StaticContent::loadFile(const QString &rootDir, const QString &requestPath) {
    StaticResponse response;

    QString relativePath = requestPath;
    if (relativePath.startsWith('/')) {
        relativePath.remove(0, 1);
    }
    relativePath = QDir::cleanPath(relativePath);
    if (relativePath.startsWith(QStringLiteral(".."))) {
        response.statusCode = 404;
        response.statusText = QStringLiteral("Not Found");
        response.body = QByteArrayLiteral("Not Found");
        response.mime = QStringLiteral("text/plain");
        return response;
    }

    const QString absoluteCandidatePath = QDir(rootDir).absoluteFilePath(relativePath);
    const QString canonicalCandidatePath = QFileInfo(absoluteCandidatePath).canonicalFilePath();
    if (canonicalCandidatePath.isEmpty() || !canonicalCandidatePath.startsWith(rootDir)) {
        response.statusCode = 404;
        response.statusText = QStringLiteral("Not Found");
        response.body = QByteArrayLiteral("Not Found");
        response.mime = QStringLiteral("text/plain");
        return response;
    }

    QFile file(canonicalCandidatePath);
    if (!file.open(QIODevice::ReadOnly)) {
        response.statusCode = 404;
        response.statusText = QStringLiteral("Not Found");
        response.body = QByteArrayLiteral("Not Found");
        response.mime = QStringLiteral("text/plain");
        return response;
    }

    response.statusCode = 200;
    response.statusText = QStringLiteral("OK");
    response.body = file.readAll();
    response.mime = guessMime(canonicalCandidatePath);
    return response;
}
