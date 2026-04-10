// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "OCPPConfig.hpp"

namespace OCPPConfig {
ModuleResponse handleRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("ocpp"),
        .action = request.action,
        .parameters = request.parameters,
        .success = false,
    };
}
} // namespace OCPPConfig
