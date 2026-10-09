// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SSHConfiguration.hpp"

#include "ProtocolSchema.hpp"
#include "SystemdService.hpp"

#include <QJsonObject>
#include <QProcess>
#include <QStringList>

#include <stdexcept>
#include <utility>

namespace {
constexpr char kSshSocketUnit[] = "sshd.socket";
constexpr char kSshServiceUnit[] = "sshd.service";
constexpr char kParameterSocketActive[] = "socket_active";
constexpr char kParameterSocketEnabled[] = "socket_enabled";
constexpr char kParameterPassword[] = "password";
constexpr char kErrorSystemdFailed[] = "ssh_systemd_failed";
constexpr char kErrorPasswordFailed[] = "ssh_password_failed";
constexpr char kErrorUnsupportedAction[] = "unsupported_action";

SSHConfiguration::SystemdOperations g_systemdOperationsOverride;
SSHConfiguration::PasswordWriter g_passwordWriterOverride;

SSHConfigurationAction toSSHConfigurationAction(const QString &action) {
    if (action == QLatin1String(kActionRead)) {
        return SSHConfigurationAction::Read;
    }
    if (action == QLatin1String(kActionEnable)) {
        return SSHConfigurationAction::Enable;
    }
    if (action == QLatin1String(kActionDisable)) {
        return SSHConfigurationAction::Disable;
    }
    if (action == QLatin1String(kActionSetPassword)) {
        return SSHConfigurationAction::SetPassword;
    }
    return SSHConfigurationAction::Unknown;
}

ModuleResponse makeResponse(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSSH),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };
}

QStringList availableSshUnits(SystemdService &systemdService) {
    const auto isUnitAvailable = g_systemdOperationsOverride.isUnitAvailable
                                     ? g_systemdOperationsOverride.isUnitAvailable
                                     : [&systemdService](const QString &unit) {
                                           return systemdService.isUnitAvailable(unit);
                                       };
    QStringList units;
    if (isUnitAvailable(QLatin1String(kSshSocketUnit))) {
        units.append(QLatin1String(kSshSocketUnit));
    }
    if (isUnitAvailable(QLatin1String(kSshServiceUnit))) {
        units.append(QLatin1String(kSshServiceUnit));
    }
    return units;
}

QJsonObject readStatusParameters() {
    SystemdService systemdService;
    const auto isUnitActive = g_systemdOperationsOverride.isUnitActive
                                  ? g_systemdOperationsOverride.isUnitActive
                                  : [&systemdService](const QString &unit) {
                                        return systemdService.isUnitActive(unit);
                                    };
    const auto isUnitEnabled = g_systemdOperationsOverride.isUnitEnabled
                                   ? g_systemdOperationsOverride.isUnitEnabled
                                   : [&systemdService](const QString &unit) {
                                         return systemdService.isUnitEnabled(unit);
                                     };
    bool active = false;
    bool enabled = false;
    for (const QString &unit : availableSshUnits(systemdService)) {
        active = active || isUnitActive(unit);
        enabled = enabled || isUnitEnabled(unit);
    }
    return QJsonObject{
        {QLatin1String(kParameterSocketActive), active},
        {QLatin1String(kParameterSocketEnabled), enabled},
    };
}

bool isPasswordValid(const QString &password) {
    return password.size() > 0 && password.size() <= 256 &&
           !password.contains(QLatin1Char('\n')) && !password.contains(QLatin1Char('\r')) &&
           !password.contains(QChar::Null);
}

bool setRootPassword(const QString &password) {
    QProcess process;
    process.start(QStringLiteral("chpasswd"), QStringList());
    if (!process.waitForStarted(3000)) {
        return false;
    }

    process.write(QStringLiteral("root:%1\n").arg(password).toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool terminateSshSessions() {
    QProcess process;
    process.start(QStringLiteral("pkill"), {QStringLiteral("-TERM"), QStringLiteral("-f"),
                                            QStringLiteral("^sshd(-session)?: ")});
    if (!process.waitForStarted(3000)) {
        return false;
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit &&
           (process.exitCode() == 0 || process.exitCode() == 1);
}
} // namespace

namespace SSHConfiguration {
void setSystemdOperationsForTest(SystemdOperations operations) {
    g_systemdOperationsOverride = std::move(operations);
}

void setPasswordWriterForTest(PasswordWriter writer) {
    g_passwordWriterOverride = std::move(writer);
}

void resetTestOverrides() {
    g_systemdOperationsOverride = {};
    g_passwordWriterOverride = {};
}

ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toSSHConfigurationAction(request.action)) {
    case SSHConfigurationAction::Read: {
        ModuleResponse response = makeResponse(request);
        response.parameters = readStatusParameters();
        response.success = true;
        return response;
    }
    case SSHConfigurationAction::Enable: {
        ModuleResponse response = makeResponse(request);
        SystemdService systemdService;
        const QStringList sshUnits = availableSshUnits(systemdService);
        if (sshUnits.isEmpty()) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        const QString unit = sshUnits.contains(QLatin1String(kSshSocketUnit))
                                 ? QLatin1String(kSshSocketUnit)
                                 : QLatin1String(kSshServiceUnit);
        const auto enableUnit = g_systemdOperationsOverride.enableUnit
                                    ? g_systemdOperationsOverride.enableUnit
                                    : [&systemdService](const QString &unit) {
                                          return systemdService.enableUnit(unit);
                                      };
        const auto startUnit = g_systemdOperationsOverride.startUnit
                                   ? g_systemdOperationsOverride.startUnit
                                   : [&systemdService](const QString &unit) {
                                         return systemdService.startUnit(unit);
                                     };
        const auto stopUnit = g_systemdOperationsOverride.stopUnit
                                  ? g_systemdOperationsOverride.stopUnit
                                  : [&systemdService](const QString &unit) {
                                        return systemdService.stopUnit(unit);
                                    };
        const auto disableUnit = g_systemdOperationsOverride.disableUnit
                                     ? g_systemdOperationsOverride.disableUnit
                                     : [&systemdService](const QString &unit) {
                                           return systemdService.disableUnit(unit);
                                       };
        const auto reloadManager = g_systemdOperationsOverride.reloadManager
                                       ? g_systemdOperationsOverride.reloadManager
                                       : [&systemdService]() { return systemdService.reloadManager(); };
        const auto waitForActive = g_systemdOperationsOverride.waitForUnitActive
                                       ? g_systemdOperationsOverride.waitForUnitActive
                                       : [&systemdService](const QString &unit, bool active, int timeoutMs) {
                                             return systemdService.waitForUnitActive(unit, active, timeoutMs);
                                         };
        const auto rollbackEnablement = [&]() {
            disableUnit(unit);
            reloadManager();
        };
        if (!enableUnit(unit)) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        if (!reloadManager()) {
            rollbackEnablement();
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        const bool started = startUnit(unit);
        if (!started || !waitForActive(unit, true, 5000)) {
            if (started) {
                stopUnit(unit);
            }
            rollbackEnablement();
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        response.success = true;
        return response;
    }
    case SSHConfigurationAction::Disable: {
        ModuleResponse response = makeResponse(request);
        SystemdService systemdService;
        const QStringList sshUnits = availableSshUnits(systemdService);
        if (sshUnits.isEmpty()) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        const auto stopUnit = g_systemdOperationsOverride.stopUnit
                                  ? g_systemdOperationsOverride.stopUnit
                                  : [&systemdService](const QString &unit) {
                                        return systemdService.stopUnit(unit);
                                    };
        const auto disableUnit = g_systemdOperationsOverride.disableUnit
                                     ? g_systemdOperationsOverride.disableUnit
                                     : [&systemdService](const QString &unit) {
                                           return systemdService.disableUnit(unit);
                                       };
        const auto reloadManager = g_systemdOperationsOverride.reloadManager
                                       ? g_systemdOperationsOverride.reloadManager
                                       : [&systemdService]() { return systemdService.reloadManager(); };
        const auto waitForInactive = g_systemdOperationsOverride.waitForUnitActive
                                         ? g_systemdOperationsOverride.waitForUnitActive
                                         : [&systemdService](const QString &unit, bool active, int timeoutMs) {
                                               return systemdService.waitForUnitActive(unit, active, timeoutMs);
                                           };
        bool systemdSucceeded = true;
        for (const QString &unit : sshUnits) {
            if (!stopUnit(unit) || !waitForInactive(unit, false, 5000)) {
                systemdSucceeded = false;
            }
        }
        for (const QString &unit : sshUnits) {
            if (!disableUnit(unit)) {
                systemdSucceeded = false;
            }
        }
        if (!sshUnits.isEmpty() && !reloadManager()) {
            systemdSucceeded = false;
        }
        if (!systemdSucceeded) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        const bool sessionsTerminated = g_systemdOperationsOverride.terminateSessions
                                            ? g_systemdOperationsOverride.terminateSessions()
                                            : terminateSshSessions();
        if (!sessionsTerminated) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        response.success = true;
        return response;
    }
    case SSHConfigurationAction::SetPassword: {
        ModuleResponse response = makeResponse(request);
        const QString password =
            request.parameters.value(QLatin1String(kParameterPassword)).toString();
        if (!isPasswordValid(password)) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorInvalidParams)},
            };
            return response;
        }
        const bool passwordSet = g_passwordWriterOverride
                                     ? g_passwordWriterOverride(password)
                                     : setRootPassword(password);
        if (!passwordSet) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorPasswordFailed)},
            };
            return response;
        }
        response.success = true;
        return response;
    }
    case SSHConfigurationAction::Unknown:
        ModuleResponse response = makeResponse(request);
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorUnsupportedAction)},
        };
        return response;
    }

    throw std::runtime_error("SSHConfiguration::handleRequest reached unreachable code");
}
} // namespace SSHConfiguration
