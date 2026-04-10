// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef STATIC_RESPONSE_HPP
#define STATIC_RESPONSE_HPP

#include <QByteArray>
#include <QString>

struct StaticResponse {
    int statusCode = 500;
    QString statusText = QStringLiteral("Internal Server Error");
    QByteArray body;
    QString mime = QStringLiteral("text/plain");

    QByteArray toHttpWire() const;
};

#endif // STATIC_RESPONSE_HPP
