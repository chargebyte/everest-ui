// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "FactoryResetRuntime.hpp"

#include "ConsoleConnector.hpp"
#include "LoginState.hpp"
#include "ProtocolSchema.hpp"

#include <QDebug>
#include <QScopedValueRollback>

#include <cmath>
#include <limits>
#include <utility>

FactoryResetRuntime::FactoryResetRuntime(Actions actions, QObject* parent) :
    QObject(parent), m_actions(std::move(actions)) {
    m_timeout.setSingleShot(true);
    m_timeout.setTimerType(Qt::PreciseTimer);
    m_killTimer.setSingleShot(true);
    m_keepalive.setInterval(2000);
    connect(&m_keepalive, &QTimer::timeout, this,
            [this]() { emit responseReady(response(m_request, true, {}, false)); });
    connect(&m_timeout, &QTimer::timeout, this, &FactoryResetRuntime::stop);
    connect(&m_killTimer, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::started, this, [this]() {
        if (m_timedOut) {
            stop();
        } else {
            m_timeout.start(m_executionTimeoutMs);
        }
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &FactoryResetRuntime::drainOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &FactoryResetRuntime::drainOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(false, true);
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                finish(!m_timedOut && status == QProcess::NormalExit && code == 0, true);
            });
}

FactoryResetRuntime::~FactoryResetRuntime() {
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

ModuleResponse FactoryResetRuntime::response(const ModuleRequest& request, bool success, const QJsonObject& parameters,
                                             bool final) const {
    return {request.requestId, QLatin1String(kGroupFactoryReset), request.action, parameters, success, final};
}

QList<int> FactoryResetRuntime::resolveEvseIndices(const QJsonObject& configuration) {
    const auto modules = configuration.value(QStringLiteral("active_modules")).toObject();
    QList<QJsonObject> managers;
    for (const auto& value : modules) {
        const auto module = value.toObject();
        if (module.value(QStringLiteral("module")).toString() == QLatin1String("EvseManager")) {
            managers.append(module);
        }
    }
    QList<int> result;
    for (const auto& manager : managers) {
        const auto mapping = manager.value(QStringLiteral("mapping"));
        const auto index = mapping.toObject().value(QStringLiteral("module")).toObject().value(QStringLiteral("evse"));
        int number = 0;
        if (mapping.isUndefined() && managers.size() == 1) {
            number = 1;
        } else if (index.isDouble() && index.toDouble() > 0 && index.toDouble() <= std::numeric_limits<int>::max() &&
                   std::floor(index.toDouble()) == index.toDouble()) {
            number = index.toInt();
        } else {
            return {};
        }
        if (!result.contains(number)) {
            result.append(number);
        }
    }
    return result;
}

bool FactoryResetRuntime::eligible() {
    if (m_checking || m_rebootExpected) {
        return false;
    }
    QScopedValueRollback<bool> checking(m_checking, true);
    const auto value = m_actions.config(QStringLiteral("factory_reset_require_unplugged")).trimmed().toLower();
    if (value == QLatin1String("false")) {
        return true;
    }
    if (!value.isEmpty() && value != QLatin1String("true")) {
        qWarning() << "Invalid factory_reset_require_unplugged; enforcing Unplugged guard";
    }
    const auto indices = resolveEvseIndices(m_actions.effectiveConfig());
    return !indices.isEmpty() && m_actions.unplugged(indices);
}

ModuleResponse FactoryResetRuntime::handleRequest(const ModuleRequest& request, bool maintenanceBusy) {
    if (request.action == QLatin1String(kActionReadStatus)) {
        const bool canReset = !m_running && !maintenanceBusy && eligible();
        return response(request, true,
                        {{"state", m_state}, {"can_reset", canReset}, {"reboot_expected", m_rebootExpected}});
    }
    if (request.action != QLatin1String(kActionReset)) {
        return response(request, false, {{"error", "unsupported_action"}});
    }
    if (m_running || maintenanceBusy || m_rebootExpected) {
        return response(request, false, {{"error", "factory_reset_busy"}});
    }
    if (request.parameters.value(QStringLiteral("confirm")) != QJsonValue(true)) {
        return response(request, false, {{"error", "factory_reset_confirmation_required"}});
    }
    m_request = request;
    m_running = true;
    m_state = QStringLiteral("running");
    m_keepalive.start();
    // Defer safety checks so the caller has registered the pending request before callbacks or nested RPC reads.
    QTimer::singleShot(0, this, &FactoryResetRuntime::start);
    return response(request, true, {}, false);
}

void FactoryResetRuntime::start() {
    if (!eligible()) {
        finish(false, false);
        return;
    }
    m_command = m_actions.config(QStringLiteral("factory_reset_command")).trimmed();
    if (m_command.isEmpty()) {
        m_command = QStringLiteral("/usr/sbin/factory-reset --force --reboot");
    }
    const auto timeout = m_actions.config(QStringLiteral("factory_reset_timeout_seconds")).trimmed();
    bool valid = false;
    const int seconds = timeout.toInt(&valid);
    m_executionTimeoutMs = 20000;
    if (valid && seconds > 0 && seconds <= std::numeric_limits<int>::max() / 1000) {
        m_executionTimeoutMs = seconds * 1000;
    } else if (!timeout.isEmpty()) {
        qWarning() << "Invalid factory_reset_timeout_seconds; using 20 seconds";
    }
    m_timedOut = false;
    m_stdout.clear();
    m_stderr.clear();
    m_rebootExpected = false;
    const auto command = ConsoleConnector::parseCommand(m_command);
    qInfo() << "Factory reset starting: command=" << m_command << "timeout_ms=" << m_executionTimeoutMs;
    if (command.program.isEmpty()) {
        finish(false, true);
        return;
    }
    m_timeout.start(2000);
    m_process.start(command.program, command.args);
}

void FactoryResetRuntime::stop() {
    m_timedOut = true;
    if (m_process.state() == QProcess::NotRunning) {
        finish(false, true);
        return;
    }
    m_process.terminate();
    m_killTimer.start(1000);
}

void FactoryResetRuntime::drainOutput() {
    // Bound retained diagnostics while continuing to drain both process channels.
    m_stdout = (m_stdout + m_process.readAllStandardOutput()).right(65536);
    m_stderr = (m_stderr + m_process.readAllStandardError()).right(65536);
}

void FactoryResetRuntime::finish(bool success, bool attempted) {
    if (!m_running) {
        return;
    }
    m_timeout.stop();
    m_killTimer.stop();
    m_keepalive.stop();
    m_running = false;
    if (attempted) {
        drainOutput();
        m_state = success ? QStringLiteral("rebooting") : QStringLiteral("failed");
        m_rebootExpected = success;
        qInfo() << "Factory reset finished: command=" << m_command << "success=" << success
                << "timed_out=" << m_timedOut << "exit_code=" << m_process.exitCode()
                << "process_error=" << m_process.errorString() << "stdout_tail=" << m_stdout
                << "stderr_tail=" << m_stderr;
    } else {
        m_state = QStringLiteral("idle");
        qWarning() << "Factory reset rejected by Unplugged safety policy";
    }
    QJsonObject parameters;
    if (success) {
        parameters.insert(QStringLiteral("reboot_expected"), true);
        QString error;
        if (!UiRuntime::blockLoginForReboot(QStringLiteral("factory_reset"), error)) {
            parameters.insert(QStringLiteral("error"), QStringLiteral("login_state_unavailable"));
            success = false;
        }
    }
    if (!success && !parameters.contains(QStringLiteral("error"))) {
        parameters.insert(QStringLiteral("error"), attempted ? QStringLiteral("factory_reset_failed")
                                                             : QStringLiteral("factory_reset_not_allowed"));
    }
    emit responseReady(response(m_request, success, parameters));
}
