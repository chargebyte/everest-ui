// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef NETWORK_CONFIGURATION_HPP
#define NETWORK_CONFIGURATION_HPP

#include "RequestResponseTypes.hpp"

namespace NetworkConfiguration {
ModuleResponse handleRequest(const ModuleRequest &request);
}

#endif // NETWORK_CONFIGURATION_HPP
