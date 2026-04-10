// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "Logs.hpp"

namespace Logs {
ModuleResponse handleRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("logs"),
        .action = request.action,
        .parameters = request.parameters,
        .success = false,
    };
}
} // namespace Logs
