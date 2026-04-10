// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "StaticResponse.hpp"

#include <QDateTime>

namespace {
QString httpDate() {
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
}
} // namespace

QByteArray StaticResponse::toHttpWire() const {
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText.toUtf8() + "\r\n";
    resp += "Date: " + httpDate().toUtf8() + "\r\n";
    resp += "Connection: close\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Content-Type: " + mime.toUtf8() + "\r\n\r\n";
    resp += body;
    return resp;
}
