// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SafetyController.hpp"

namespace SafetyController {
ModuleResponse handleRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("safety"),
        .action = request.action,
        .parameters = request.parameters,
        .success = false,
    };
}
} // namespace SafetyController
