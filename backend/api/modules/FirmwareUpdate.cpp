// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "FirmwareUpdate.hpp"

namespace FirmwareUpdate {
ModuleResponse handleRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = request.parameters,
        .success = false,
    };
}
} // namespace FirmwareUpdate
