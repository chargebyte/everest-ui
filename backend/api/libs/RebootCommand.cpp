// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "RebootCommand.hpp"
#include "ConsoleConnector.hpp"

#include <QDebug>
#include <QTimer>

void runDeferredRebootCommand(QObject* context, const QString& command) {
    QTimer::singleShot(0, context, [command]() {
        ConsoleConnector console;
        ConsoleConnector::ExecOptions options;
        options.syncTimeoutMs = 5000;
        const auto result = console.executeTemplate(command, {}, options, ConsoleConnector::ExecMode::Sync);
        if (!result.normalExit || result.exitCode != 0) {
            qWarning() << "Reboot command failed:" << command << result.processError << "exit_code=" << result.exitCode
                       << "stderr=" << result.stderrData;
        }
    });
}
