// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef RESPONSE_BUILDER_HPP
#define RESPONSE_BUILDER_HPP

#include "RequestResponseTypes.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class ResponseBuilder {
public:
    static QJsonObject buildResponse(const ModuleResponse &response);

    // Temporary compatibility API for the legacy PCAP module. Remove these
    // once PCAP is adapted to the new ModuleResponse-based response pipeline.
    static QJsonObject error(const QString &requestId, const QString &error);
    static QJsonObject pcapWriteAck(const QString &requestId);
    static QJsonObject pcapReadResult(const QString &requestId,
                                      const QString &filePath,
                                      const QByteArray &encodedData);

private:
    static QString buildTypeSuffix(bool success, const QJsonObject &parameters);
    static QString buildType(const ModuleResponse &response);
};

#endif // RESPONSE_BUILDER_HPP
