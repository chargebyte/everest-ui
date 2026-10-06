// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SSH_CONFIGURATION_HPP
#define SSH_CONFIGURATION_HPP

#include "RequestResponseTypes.hpp"

#include <functional>

enum class SSHConfigurationAction {
    Read,
    Enable,
    Disable,
    SetPassword,
    Unknown
};

namespace SSHConfiguration {
struct SystemdOperations {
    std::function<bool(const QString &)> startUnit;
    std::function<bool(const QString &)> stopUnit;
    std::function<bool(const QString &)> enableUnit;
    std::function<bool(const QString &)> disableUnit;
    std::function<bool(const QString &)> isUnitActive;
    std::function<bool(const QString &, bool, int)> waitForUnitActive;
    std::function<bool(const QString &)> isUnitEnabled;
};

using PasswordWriter = std::function<bool(const QString &)>;

ModuleResponse handleRequest(const ModuleRequest &request);
void setSystemdOperationsForTest(SystemdOperations operations);
void setPasswordWriterForTest(PasswordWriter writer);
void resetTestOverrides();
}

#endif // SSH_CONFIGURATION_HPP
