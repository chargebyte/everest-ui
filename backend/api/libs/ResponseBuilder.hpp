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

private:
    static QString buildTypeSuffix(bool success, const QJsonObject &parameters);
    static QString buildType(const ModuleResponse &response);
};

#endif // RESPONSE_BUILDER_HPP
