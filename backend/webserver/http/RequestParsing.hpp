// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef REQUEST_PARSING_HPP
#define REQUEST_PARSING_HPP

#include "StaticResponse.hpp"

#include <QByteArray>
#include <QList>
#include <QUrl>

struct ParsedRequest {
    QList<QByteArray> lines;
    QByteArray method;
    QByteArray path;
};

bool isHeaderComplete(const QByteArray &peek, int maxRequestBytes, StaticResponse *errorResponse);
bool isRequestValid(const QByteArray &peek,
                    ParsedRequest &request,
                    StaticResponse *errorResponse);
bool isWebSocketUpgradeRequest(const ParsedRequest &request,
                               const QByteArray &wsPath,
                               bool enforceOrigin,
                               const QUrl &allowOriginUrl);

#endif // REQUEST_PARSING_HPP
