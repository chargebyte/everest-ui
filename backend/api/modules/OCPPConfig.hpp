// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef OCPP_CONFIG_HPP
#define OCPP_CONFIG_HPP

#include "RequestResponseTypes.hpp"

enum class OCPPAction {
    ReadSettings,
    WriteSettings,
    Unknown
};

namespace OCPPConfig {
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleWriteRequest(const ModuleRequest &request);
}

#endif // OCPP_CONFIG_HPP
