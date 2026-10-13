// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SSHConfiguration.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"
#include "SystemdService.hpp"

#include <QDebug>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

#include <stdexcept>
#include <utility>

namespace {
constexpr char kSshSocketUnit[] = "sshd.socket";
constexpr char kSshServiceUnit[] = "sshd.service";
constexpr char kParameterActive[] = "active";
constexpr char kParameterEnabled[] = "enabled";
constexpr char kAutostartPath[] = "/usr/sbin/ssh-autostart";
constexpr char kControlMethodKey[] = "ssh_control_method";
constexpr char kAutostartMethod[] = "ssh-autostart";
constexpr char kSystemdMethod[] = "systemd";
constexpr char kErrorAutostartFailed[] = "ssh_autostart_failed";
constexpr char kErrorControlMethodInvalid[] = "ssh_control_method_invalid";
constexpr int kStartTimeoutMs = 3000;
constexpr int kRunTimeoutMs = 30000;
constexpr int kKillTimeoutMs = 1000;
constexpr char kParameterPassword[] = "password";
constexpr char kErrorSystemdFailed[] = "ssh_systemd_failed";
constexpr char kErrorPasswordFailed[] = "ssh_password_failed";
constexpr char kErrorUnsupportedAction[] = "unsupported_action";

SSHConfiguration::SystemdOperations g_systemdOperationsOverride;
SSHConfiguration::PasswordWriter g_passwordWriterOverride;
SSHConfiguration::AutostartRunner g_autostartRunnerOverride;
SSHConfiguration::ControlMethodReader g_controlMethodReaderOverride;

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

SSHConfiguration::AutostartResult runAutostart(const QString& program, const QStringList& arguments) {
    QProcess process;
    SSHConfiguration::AutostartResult result;
    process.start(program, arguments);
    result.started = process.waitForStarted(kStartTimeoutMs);
    if (!result.started) {
        result.timedOut = process.error() == QProcess::Timedout;
        result.processError = process.errorString();
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(kKillTimeoutMs);
        }
    } else if (!process.waitForFinished(kRunTimeoutMs)) {
        result.timedOut = true;
        result.processError = process.errorString();
        process.kill();
        process.waitForFinished(kKillTimeoutMs);
    } else if (process.exitStatus() != QProcess::NormalExit) {
        result.processError = process.errorString();
    }
    result.normalExit = result.started && !result.timedOut && process.exitStatus() == QProcess::NormalExit;
    result.exitCode = result.started ? process.exitCode() : -1;
    result.stdoutData = process.readAllStandardOutput();
    result.stderrData = process.readAllStandardError();
    return result;
}

ModuleResponse handleAutostartRequest(const ModuleRequest& request, SSHConfigurationAction action) {
    ModuleResponse response = makeResponse(request);
    const QString argument = action == SSHConfigurationAction::Read     ? QStringLiteral("--status")
                             : action == SSHConfigurationAction::Enable ? QStringLiteral("--enable")
                                                                        : QStringLiteral("--disable");
    const auto result = g_autostartRunnerOverride ? g_autostartRunnerOverride(QLatin1String(kAutostartPath), {argument})
                                                  : runAutostart(QLatin1String(kAutostartPath), {argument});
    const bool completed = result.started && !result.timedOut && result.normalExit;
    if (action == SSHConfigurationAction::Read) {
        const QByteArray status = result.stdoutData.trimmed();
        if (completed &&
            ((result.exitCode == 0 && status == "enabled") || (result.exitCode == 1 && status == "disabled"))) {
            response.parameters = {{QLatin1String(kParameterEnabled), result.exitCode == 0},
                                   {QLatin1String(kParameterActive), QJsonValue::Null}};
            response.success = true;
            return response;
        }
    } else if (completed && result.exitCode == 0) {
        response.success = true;
        return response;
    }
    qWarning().noquote() << QStringLiteral(
                                "SSH autostart failed: command='%1 %2'; started=%3; timed_out=%4; normal_exit=%5; "
                                "exit_code=%6; process_error='%7'; stdout='%8'; stderr='%9'; "
                                "start_timeout_ms=%10; execution_timeout_ms=%11")
                                .arg(QLatin1String(kAutostartPath), argument)
                                .arg(result.started)
                                .arg(result.timedOut)
                                .arg(result.normalExit)
                                .arg(result.exitCode)
                                .arg(result.processError, QString::fromUtf8(result.stdoutData).trimmed(),
                                     QString::fromUtf8(result.stderrData).trimmed())
                                .arg(kStartTimeoutMs)
                                .arg(kRunTimeoutMs);
    response.parameters = {{QLatin1String(kError), QLatin1String(kErrorAutostartFailed)}};
    return response;
}

QStringList availableSshUnits(SystemdService& systemdService) {
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

QJsonObject readSystemdStatusParameters() {
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
        {QLatin1String(kParameterActive), active},
        {QLatin1String(kParameterEnabled), enabled},
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

void setAutostartRunnerForTest(AutostartRunner runner) {
    g_autostartRunnerOverride = std::move(runner);
}

void setControlMethodReaderForTest(ControlMethodReader reader) {
    g_controlMethodReaderOverride = std::move(reader);
}

void resetTestOverrides() {
    g_systemdOperationsOverride = {};
    g_passwordWriterOverride = {};
    g_autostartRunnerOverride = {};
    g_controlMethodReaderOverride = {};
}

ModuleResponse handleRequest(const ModuleRequest& request) {
    const SSHConfigurationAction action = toSSHConfigurationAction(request.action);
    if (action == SSHConfigurationAction::Read || action == SSHConfigurationAction::Enable ||
        action == SSHConfigurationAction::Disable) {
        QString method = (g_controlMethodReaderOverride ? g_controlMethodReaderOverride()
                                                        : readBackendConfigValue(QLatin1String(kControlMethodKey)))
                             .trimmed();
        if (method.isEmpty()) {
            method = QLatin1String(kAutostartMethod);
        }
        if (method == QLatin1String(kAutostartMethod)) {
            return handleAutostartRequest(request, action);
        }
        if (method != QLatin1String(kSystemdMethod)) {
            qWarning().noquote() << QStringLiteral("Invalid ssh_control_method: '%1'").arg(method);
            ModuleResponse response = makeResponse(request);
            response.parameters = {{QLatin1String(kError), QLatin1String(kErrorControlMethodInvalid)}};
            return response;
        }
    }
    switch (action) {
    case SSHConfigurationAction::Read: {
        ModuleResponse response = makeResponse(request);
        response.parameters = readSystemdStatusParameters();
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
