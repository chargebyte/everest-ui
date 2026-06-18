// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "ResponseBuilder.hpp"

#include "ProtocolSchema.hpp"

#include <stdexcept>

QJsonObject ResponseBuilder::buildResponse(const ModuleResponse &response) {
    return QJsonObject{
        {QLatin1String(kKeyOk), response.success},
        {QLatin1String(kKeyFinal), response.final},
        {QLatin1String(kKeyRequestId), response.requestId},
        {QLatin1String(kKeyType), buildType(response)},
        {QLatin1String(kKeyParameters), response.parameters},
    };
}

QString ResponseBuilder::buildTypeSuffix(bool success, const QJsonObject &parameters) {
    if (!success) {
        return QLatin1String(kError);
    }

    if (parameters.isEmpty()) {
        return QLatin1String(kTypeAck);
    }

    return QLatin1String(kTypeResult);
}

QString ResponseBuilder::buildType(const ModuleResponse &response) {
    return response.group + QLatin1Char('.') + response.action + QLatin1Char('.') +
           buildTypeSuffix(response.success, response.parameters);
}
