// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_REQUEST_SECURITY_HPP
#define EVEREST_UI_REQUEST_SECURITY_HPP

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

struct ParsedRequest;
namespace RequestSecurity {
QString normalizedHost(QString host);
bool validHostName(const QString &host);
bool parseAuthority(const QByteArray &authority, QUrl &url);
QSet<QString> allowedHosts(const QStringList &configured, const QString &allowOriginHost = {});
int validateHost(const ParsedRequest &request, const QSet<QString> &allowed);
int validateAuthMutation(const ParsedRequest &request);
} // namespace RequestSecurity

#endif // EVEREST_UI_REQUEST_SECURITY_HPP
