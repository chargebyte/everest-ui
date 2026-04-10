// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SAFETY_CONTROLLER_HPP
#define SAFETY_CONTROLLER_HPP

#include "RequestResponseTypes.hpp"

namespace SafetyController {
ModuleResponse handleRequest(const ModuleRequest &request);
}

#endif // SAFETY_CONTROLLER_HPP
