// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "RequestSecurity.hpp"

#include "RequestParsing.hpp"

#include <QDebug>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSysInfo>

namespace RequestSecurity {
QString normalizedHost(QString host) {
    QHostAddress address;
    if (address.setAddress(host)) {
        return address.toString().toLower();
    }
    host = host.toLower();
    if (host.endsWith('.')) {
        host.chop(1);
    }
    return host;
}

bool validHostName(const QString &host) {
    QHostAddress address;
    if (address.setAddress(host)) {
        return true;
    }
    if (host.isEmpty() || host.size() > 253) {
        return false;
    }
    const auto name = normalizedHost(host);
    static const QRegularExpression label(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    for (const auto &part : name.split('.')) {
        if (!label.match(part).hasMatch()) {
            return false;
        }
    }
    return true;
}

bool parseAuthority(const QByteArray &authority, QUrl &url) {
    // Validate syntax before QUrl can normalize userinfo, paths or empty ports.
    static const QRegularExpression syntax(QStringLiteral(
        "^(\\[[0-9A-Fa-f:.]+\\]|[A-Za-z0-9.-]+)(?::([0-9]{1,5}))?$"));
    const auto match = syntax.match(QString::fromLatin1(authority));
    if (!match.hasMatch()) {
        return false;
    }
    if (!match.captured(2).isEmpty() &&
        (match.captured(2).toInt() < 1 || match.captured(2).toInt() > 65535)) {
        return false;
    }
    url = QUrl::fromEncoded(QByteArrayLiteral("http://") + authority, QUrl::StrictMode);
    if (!url.isValid() || !validHostName(url.host())) {
        return false;
    }
    if (authority.startsWith('[')) {
        QHostAddress address(url.host());
        if (address.protocol() != QAbstractSocket::IPv6Protocol) {
            return false;
        }
    }
    return true;
}

QSet<QString> allowedHosts(const QStringList &configured, const QString &allowOriginHost) {
    QSet<QString> hosts;
    hosts.insert(QStringLiteral("localhost"));
    const QString local = QSysInfo::machineHostName();
    if (validHostName(local)) {
        hosts.insert(normalizedHost(local));
        hosts.insert(normalizedHost(local.section('.', 0, 0)) + QStringLiteral(".local"));
    }
    if (validHostName(allowOriginHost)) {
        hosts.insert(normalizedHost(allowOriginHost));
    }
    for (const auto &name : configured) {
        hosts.insert(normalizedHost(name));
    }
    return hosts;
}

int validateHost(const ParsedRequest &request, const QSet<QString> &allowed) {
    QUrl url;
    if (!parseAuthority(request.headers.value(QByteArrayLiteral("host")), url)) {
        return 400;
    }
    QHostAddress address;
    if (address.setAddress(url.host()) || allowed.contains(normalizedHost(url.host()))) {
        return 200;
    }
    return 421;
}

int validateAuthMutation(const ParsedRequest &request) {
    static const QRegularExpression jsonType(
        QStringLiteral("^application/json(?:[ \\t]*;[ \\t]*"
                       "[!#$%&'*+.^_`|~0-9a-z-]+[ \\t]*=[ \\t]*"
                       "(?:[!#$%&'*+.^_`|~0-9a-z-]+|\"[^\"\\r\\n]*\"))*[ \\t]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (!jsonType.isValid()) {
        qCritical() << "Invalid Content-Type validation expression:" << jsonType.errorString();
        return 500;
    }
    if (!jsonType
             .match(QString::fromLatin1(request.headers.value(QByteArrayLiteral("content-type"))))
             .hasMatch()) {
        return 415;
    }
    if (!request.headers.contains(QByteArrayLiteral("origin"))) {
        return 200;
    }
    const auto origin = request.headers.value(QByteArrayLiteral("origin"));
    // This server terminates HTTP, not TLS; forwarded headers are not trusted.
    if (!origin.startsWith(QByteArrayLiteral("http://"))) {
        return 403;
    }
    QUrl source, target;
    if (!parseAuthority(origin.mid(7), source) ||
        !parseAuthority(request.headers.value(QByteArrayLiteral("host")), target)) {
        return 403;
    }
    if (normalizedHost(source.host()) != normalizedHost(target.host()) ||
        source.port(80) != target.port(80)) {
        return 403;
    }
    return 200;
}
} // namespace RequestSecurity
