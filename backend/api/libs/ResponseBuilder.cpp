// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "ResponseBuilder.hpp"

#include "ProtocolSchema.hpp"

#include <stdexcept>

QJsonObject ResponseBuilder::buildResponse(const ModuleResponse &response) {
    return QJsonObject{
        {QStringLiteral("ok"), response.success},
        {QStringLiteral("requestId"), response.requestId},
        {QStringLiteral("type"), buildType(response)},
        {QStringLiteral("parameters"), response.parameters},
    };
}

QString ResponseBuilder::buildTypeSuffix(bool success, const QJsonObject &parameters) {
    if (!success) {
        return QStringLiteral("error");
    }

    if (parameters.isEmpty()) {
        return QStringLiteral("ack");
    }

    return QStringLiteral("result");
}

QString ResponseBuilder::buildType(const ModuleResponse &response) {
    return response.group + QLatin1Char('.') + response.action + QLatin1Char('.') +
           buildTypeSuffix(response.success, response.parameters);
}

QJsonObject ResponseBuilder::error(const QString &requestId, const QString &error) {
    // Temporary compatibility path for the legacy PCAP module. Remove once
    // PCAP emits ModuleResponse objects and uses buildResponse() directly.
    return QJsonObject{
        {QLatin1String(kKeyOk), false},
        {QLatin1String(FieldNameKey(FieldName::RequestId)), requestId},
        {QLatin1String(kKeyError), error},
    };
}

QJsonObject ResponseBuilder::pcapWriteAck(const QString &requestId) {
    // Temporary compatibility path for the legacy PCAP module. Remove once
    // PCAP emits ModuleResponse objects and uses buildResponse() directly.
    return QJsonObject{
        {QLatin1String(kKeyOk), true},
        {QLatin1String(FieldNameKey(FieldName::RequestId)), requestId},
        {QLatin1String(kKeyType), QLatin1String(kTypePcapWriteAck)},
        {QLatin1String(FieldNameKey(FieldName::Parameters)), QJsonObject{}},
    };
}

QJsonObject ResponseBuilder::pcapReadResult(const QString &requestId,
                                            const QString &filePath,
                                            const QByteArray &encodedData) {
    // Temporary compatibility path for the legacy PCAP module. Remove once
    // PCAP emits ModuleResponse objects and uses buildResponse() directly.
    return QJsonObject{
        {QLatin1String(kKeyOk), true},
        {QLatin1String(FieldNameKey(FieldName::RequestId)), requestId},
        {QLatin1String(kKeyType), QLatin1String(kTypePcapReadResult)},
        {QLatin1String(FieldNameKey(FieldName::Parameters)),
         QJsonObject{
             {QLatin1String(kKeyFile), filePath},
             {QLatin1String(kKeyDataB64), QString::fromLatin1(encodedData)},
         }},
    };
}
