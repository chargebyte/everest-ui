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
