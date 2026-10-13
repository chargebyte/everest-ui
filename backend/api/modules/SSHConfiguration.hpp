// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SSH_CONFIGURATION_HPP
#define SSH_CONFIGURATION_HPP

#include "RequestResponseTypes.hpp"

#include <QByteArray>
#include <QStringList>

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
    std::function<bool()> reloadManager;
    std::function<bool(const QString &)> isUnitActive;
    std::function<bool(const QString &, bool, int)> waitForUnitActive;
    std::function<bool(const QString &)> isUnitAvailable;
    std::function<bool(const QString &)> isUnitEnabled;
    std::function<bool()> terminateSessions;
};

using PasswordWriter = std::function<bool(const QString&)>;

struct AutostartResult {
    bool started = false;
    bool timedOut = false;
    bool normalExit = false;
    int exitCode = -1;
    QByteArray stdoutData;
    QByteArray stderrData;
    QString processError;
};

using AutostartRunner = std::function<AutostartResult(const QString&, const QStringList&)>;
using ControlMethodReader = std::function<QString()>;

ModuleResponse handleRequest(const ModuleRequest& request);
void setSystemdOperationsForTest(SystemdOperations operations);
void setPasswordWriterForTest(PasswordWriter writer);
void setAutostartRunnerForTest(AutostartRunner runner);
void setControlMethodReaderForTest(ControlMethodReader reader);
void resetTestOverrides();
} // namespace SSHConfiguration

#endif // SSH_CONFIGURATION_HPP
