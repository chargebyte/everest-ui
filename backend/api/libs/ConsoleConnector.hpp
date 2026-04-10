// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef CONSOLE_CONNECTOR_HPP
#define CONSOLE_CONNECTOR_HPP

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <cstddef>

class QProcess;

class ConsoleConnector final : public QObject {
    Q_OBJECT

public:
    struct CommandSpec {
        QString program;
        QStringList args;
    };

    struct RunResult {
        int exitCode = -1;
        QByteArray stdoutData;
        QByteArray stderrData;
    };

    struct EvalResult {
        bool ok = false;
        QString error;
        QByteArray payload;
    };

    struct ExecOptions {
        bool stop = false;
        int syncTimeoutMs = 30000;
        int startTimeoutMs = 2000;
        int stopTimeoutMs = 3000;
        int killTimeoutMs = 1000;
    };

    enum class ExecMode {
        Sync,
        Async
    };

    inline static constexpr const char kErrorUnknownCommand[] = "unknown_command";

    explicit ConsoleConnector(QObject *parent = nullptr);

    // Generic execution: caller provides template and parameters.
    RunResult executeTemplate(const QString &templateStr,
                              const QHash<QString, QString> &values,
                              const ExecOptions &options,
                              ExecMode mode);

private:
    CommandSpec renderTemplate(const QString &templateStr,
                               const QHash<QString, QString> &values) const;
    RunResult runPrompt(const CommandSpec &spec, const ExecOptions &options) const;
    RunResult runPromptAsync(const CommandSpec &spec, const ExecOptions &options);
    RunResult startPromptAsync(const CommandSpec &spec, const ExecOptions &options);
    RunResult stopPromptAsync(const ExecOptions &options);

    QProcess *m_longProcess = nullptr;
};

#endif // CONSOLE_CONNECTOR_HPP
