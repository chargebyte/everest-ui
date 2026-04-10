// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef STATIC_CONTENT_HPP
#define STATIC_CONTENT_HPP

#include "StaticResponse.hpp"

#include <QByteArray>
#include <QString>

class StaticContent {
public:
    static QString normalizeRequestPath(const QByteArray &path);
    static StaticResponse loadFile(const QString &rootDir, const QString &requestPath);
};

#endif // STATIC_CONTENT_HPP
