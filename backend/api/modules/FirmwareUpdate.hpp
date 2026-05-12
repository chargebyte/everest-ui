// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef FIRMWARE_UPDATE_HPP
#define FIRMWARE_UPDATE_HPP

#include "RequestResponseTypes.hpp"

enum class FirmwareUpdateAction {
    ReadVersion,
    UpdateImage,
    Unknown
};

struct FirmwareVersionReadResult {
    bool success = false;
    QString version;
    QString error;
};

namespace FirmwareUpdate {
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleUpdateRequest(const ModuleRequest &request);
FirmwareVersionReadResult readFirmwareVersion();
}

#endif // FIRMWARE_UPDATE_HPP
