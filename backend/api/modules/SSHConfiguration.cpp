// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SSHConfiguration.hpp"

#include "ProtocolSchema.hpp"
#include "SystemdService.hpp"

#include <QJsonObject>
#include <QProcess>

#include <stdexcept>
#include <utility>

namespace {
constexpr char kSshSocketUnit[] = "sshd.socket";
constexpr char kParameterSocketActive[] = "socket_active";
constexpr char kParameterSocketEnabled[] = "socket_enabled";
constexpr char kParameterPassword[] = "password";
constexpr char kErrorSystemdFailed[] = "ssh_systemd_failed";
constexpr char kErrorPasswordFailed[] = "ssh_password_failed";

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

QJsonObject readStatusParameters() {
    SystemdService systemdService;
    return QJsonObject{
        {QLatin1String(kParameterSocketActive),
         g_systemdOperationsOverride.isUnitActive
             ? g_systemdOperationsOverride.isUnitActive(QLatin1String(kSshSocketUnit))
             : systemdService.isUnitActive(QLatin1String(kSshSocketUnit))},
        {QLatin1String(kParameterSocketEnabled),
         g_systemdOperationsOverride.isUnitEnabled
             ? g_systemdOperationsOverride.isUnitEnabled(QLatin1String(kSshSocketUnit))
             : systemdService.isUnitEnabled(QLatin1String(kSshSocketUnit))},
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
                                            QStringLiteral("^sshd: ")});
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
        if (!enableUnit(QLatin1String(kSshSocketUnit)) ||
            !startUnit(QLatin1String(kSshSocketUnit)) ||
            !(g_systemdOperationsOverride.waitForUnitActive
                  ? g_systemdOperationsOverride.waitForUnitActive(
                        QLatin1String(kSshSocketUnit), true, 5000)
                  : systemdService.waitForUnitActive(QLatin1String(kSshSocketUnit), true, 5000))) {
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
        if (!stopUnit(QLatin1String(kSshSocketUnit)) ||
            !(g_systemdOperationsOverride.waitForUnitActive
                  ? g_systemdOperationsOverride.waitForUnitActive(
                        QLatin1String(kSshSocketUnit), false, 5000)
                  : systemdService.waitForUnitActive(QLatin1String(kSshSocketUnit), false, 5000)) ||
            !disableUnit(QLatin1String(kSshSocketUnit))) {
            response.parameters = QJsonObject{
                {QLatin1String(kError), QLatin1String(kErrorSystemdFailed)},
            };
            return response;
        }
        if (!terminateSshSessions()) {
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
        throw std::runtime_error("SSHConfiguration::handleRequest got unsupported action");
    }

    throw std::runtime_error("SSHConfiguration::handleRequest reached unreachable code");
}
} // namespace SSHConfiguration
