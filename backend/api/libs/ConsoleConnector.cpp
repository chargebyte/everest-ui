// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "ConsoleConnector.hpp"

#include <QProcess>
#include <QEventLoop>
#include <QTimer>
#include <QStringList>
#include <QString>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

ConsoleConnector::ConsoleConnector(QObject *parent)
    : QObject(parent) {}

void ConsoleConnector::ensureLongProcess() {
    if (m_longProcess) {
        return;
    }

    m_longProcess = new QProcess(this);

    connect(m_longProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_streamingActive) {
            return;
        }

        const QByteArray chunk = m_longProcess->readAllStandardOutput();
        if (chunk.isEmpty()) {
            return;
        }

        m_streamingStdoutBuffer.append(chunk);
        emit streamingStdoutReceived(chunk);
    });

    connect(m_longProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_streamingActive) {
            return;
        }

        const QByteArray chunk = m_longProcess->readAllStandardError();
        if (chunk.isEmpty()) {
            return;
        }

        m_streamingStderrBuffer.append(chunk);
        emit streamingStderrReceived(chunk);
    });

    connect(m_longProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) {
                if (!m_streamingActive) {
                    return;
                }

                m_streamingActive = false;

                const QByteArray stdoutTail = m_longProcess->readAllStandardOutput();
                if (!stdoutTail.isEmpty()) {
                    m_streamingStdoutBuffer.append(stdoutTail);
                }

                const QByteArray stderrTail = m_longProcess->readAllStandardError();
                if (!stderrTail.isEmpty()) {
                    m_streamingStderrBuffer.append(stderrTail);
                }

                emit streamingFinished(RunResult{
                    .exitCode = exitCode,
                    .stdoutData = m_streamingStdoutBuffer,
                    .stderrData = m_streamingStderrBuffer,
                });
            });
}

ConsoleConnector::RunResult ConsoleConnector::executeTemplate(
    const QString &templateStr, const QHash<QString, QString> &values,
    const ExecOptions &options, ExecMode mode) {
    const CommandSpec spec = renderTemplate(templateStr, values);
    if (spec.program.isEmpty()) {
        return {-1, {}, {}};
    }

    if (mode == ExecMode::StreamingAsync) {
        return startPromptStreaming(spec, options);
    }

    if (mode == ExecMode::Async) {
        return runPromptAsync(spec, options);
    }

    return runPrompt(spec, options);
}

ConsoleConnector::CommandSpec ConsoleConnector::renderTemplate(
    const QString &templateStr, const QHash<QString, QString> &values) const {
    QString command = templateStr;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        command.replace(it.key(), it.value());
    }
    const QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty()) {
        return {};
    }
    return {parts.first(), parts.mid(1)};
}

ConsoleConnector::RunResult ConsoleConnector::runPrompt(
    const CommandSpec &spec, const ExecOptions &options) const {
    QProcess process;
    process.start(spec.program, spec.args);
    if (!process.waitForFinished(options.syncTimeoutMs)) {
        process.kill();
        process.waitForFinished(options.killTimeoutMs);
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();
        return {1, out, err};
    }
    const int exitCode = process.exitCode();
    const QByteArray out = process.readAllStandardOutput();
    const QByteArray err = process.readAllStandardError();
    return {exitCode, out, err};
}

ConsoleConnector::RunResult ConsoleConnector::runPromptAsync(
    const CommandSpec &spec, const ExecOptions &options) {
    if (options.stop) {
        return stopPromptAsync(options);
    }

    return startPromptAsync(spec, options);
}

ConsoleConnector::RunResult ConsoleConnector::startPromptAsync(
    const CommandSpec &spec, const ExecOptions &options) {
    ensureLongProcess();
    m_streamingActive = false;
    if (m_longProcess->state() != QProcess::NotRunning) {
        return {-1, {}, {}};
    }

    m_longProcess->start(spec.program, spec.args);
    if (m_longProcess->waitForStarted(options.startTimeoutMs)) {
        return {0, {}, {}};
    }

    if (m_longProcess->state() != QProcess::NotRunning) {
        m_longProcess->terminate();
        m_longProcess->waitForFinished(options.stopTimeoutMs);
        if (m_longProcess->state() != QProcess::NotRunning) {
            m_longProcess->kill();
            m_longProcess->waitForFinished(options.killTimeoutMs);
        }
    }
    return {1, {}, {}};
}

ConsoleConnector::RunResult ConsoleConnector::startPromptStreaming(
    const CommandSpec &spec, const ExecOptions &options) {
    ensureLongProcess();

    if (m_longProcess->state() != QProcess::NotRunning) {
        return {-1, {}, {}};
    }

    m_streamingStdoutBuffer.clear();
    m_streamingStderrBuffer.clear();
    m_streamingActive = true;

    m_longProcess->start(spec.program, spec.args);
    if (m_longProcess->waitForStarted(options.startTimeoutMs)) {
        return {0, {}, {}};
    }

    if (m_longProcess->state() != QProcess::NotRunning) {
        m_longProcess->terminate();
        m_longProcess->waitForFinished(options.stopTimeoutMs);
        if (m_longProcess->state() != QProcess::NotRunning) {
            m_longProcess->kill();
            m_longProcess->waitForFinished(options.killTimeoutMs);
        }
    }

    m_streamingActive = false;
    m_streamingStdoutBuffer.clear();
    m_streamingStderrBuffer.clear();
    return {1, {}, {}};
}

ConsoleConnector::RunResult ConsoleConnector::stopPromptAsync(const ExecOptions &options) {
    if (!m_longProcess || m_longProcess->state() == QProcess::NotRunning) {
        return {-1, {}, {}};
    }

#ifdef Q_OS_UNIX
    const qint64 pid = m_longProcess->processId();
    if (pid > 0) {
        ::kill(static_cast<pid_t>(pid), SIGINT);
    } else {
        m_longProcess->terminate();
    }
#else
    m_longProcess->terminate();
#endif

    if (!m_longProcess->waitForFinished(options.stopTimeoutMs)) {
        m_longProcess->kill();
        m_longProcess->waitForFinished(options.killTimeoutMs);
    }
    const QByteArray out = m_longProcess->readAllStandardOutput();
    const QByteArray err = m_longProcess->readAllStandardError();
    const int exitCode = m_longProcess->exitCode();
    return {exitCode, out, err};
}
