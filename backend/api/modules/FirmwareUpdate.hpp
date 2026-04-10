// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef FIRMWARE_UPDATE_HPP
#define FIRMWARE_UPDATE_HPP

#include "RequestResponseTypes.hpp"

namespace FirmwareUpdate {
ModuleResponse handleRequest(const ModuleRequest &request);
}

#endif // FIRMWARE_UPDATE_HPP
