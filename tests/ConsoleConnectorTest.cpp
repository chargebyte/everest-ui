// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/libs/ConsoleConnector.hpp"

#include <QTest>

class ConsoleConnectorTest final : public QObject {
    Q_OBJECT

private slots:
    void reportsNonzeroExitAndCapturedOutput() {
        ConsoleConnector connector;
        const ConsoleConnector::RunResult result = connector.executeTemplate(
            QStringLiteral("sh -c \"printf output; printf failure >&2; exit 7\""), {},
            {}, ConsoleConnector::ExecMode::Sync);

        QVERIFY(result.started);
        QVERIFY(!result.timedOut);
        QVERIFY(result.normalExit);
        QCOMPARE(result.exitCode, 7);
        QCOMPARE(result.stdoutData, QByteArrayLiteral("output"));
        QCOMPARE(result.stderrData, QByteArrayLiteral("failure"));
    }

    void reportsProcessStartFailure() {
        ConsoleConnector connector;
        const ConsoleConnector::RunResult result = connector.executeTemplate(
            QStringLiteral("/nonexistent/everest-ui-test-command"), {},
            {}, ConsoleConnector::ExecMode::Sync);

        QVERIFY(!result.started);
        QVERIFY(!result.timedOut);
        QVERIFY(!result.processError.isEmpty());
    }

    void reportsTimeout() {
        ConsoleConnector connector;
        ConsoleConnector::ExecOptions options;
        options.syncTimeoutMs = 20;
        options.killTimeoutMs = 1000;
        const ConsoleConnector::RunResult result = connector.executeTemplate(
            QStringLiteral("sh -c \"exec sleep 2\""), {}, options, ConsoleConnector::ExecMode::Sync);

        QVERIFY(result.started);
        QVERIFY(result.timedOut);
        QVERIFY(!result.normalExit);
    }

    void reportsSuccessfulExit() {
        ConsoleConnector connector;
        const ConsoleConnector::RunResult result = connector.executeTemplate(
            QStringLiteral("true"), {}, {}, ConsoleConnector::ExecMode::Sync);

        QVERIFY(result.started);
        QVERIFY(!result.timedOut);
        QVERIFY(result.normalExit);
        QCOMPARE(result.exitCode, 0);
    }
};

QTEST_GUILESS_MAIN(ConsoleConnectorTest)
#include "ConsoleConnectorTest.moc"
