// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "PCAP.hpp"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

Q_DECLARE_METATYPE(ConsoleConnector::RunResult)

class PCAPTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<ConsoleConnector::RunResult>("RunResult");
    }

    void interfaceDiscoveryReturnsSyntheticAny() {
        PCAP pcap;
        const ModuleResponse response = pcap.handleRequest(ModuleRequest{
            .requestId = 1,
            .group = ModuleGroup::PCAP,
            .action = QStringLiteral("read_interfaces"),
            .parameters = {},
        });

        QVERIFY(response.success);
        const QJsonArray interfaces = response.parameters.value(QStringLiteral("interfaces")).toArray();
        QVERIFY(!interfaces.isEmpty());
        QCOMPARE(interfaces.at(0).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("any"));
    }

    void readWithoutCaptureDoesNotChangeState() {
        PCAP pcap;
        const ModuleResponse response = pcap.handleRequest(ModuleRequest{
            .requestId = 2,
            .group = ModuleGroup::PCAP,
            .action = QStringLiteral("read"),
            .parameters = {},
        });

        QVERIFY(!response.success);
        QCOMPARE(response.parameters.value(QStringLiteral("error")).toString(),
                 QStringLiteral("not_recording"));
    }

    void immediateProcessExitIsReported() {
        ConsoleConnector connector;
        QSignalSpy finishedSpy(&connector, &ConsoleConnector::streamingFinished);
        QVERIFY(finishedSpy.isValid());

        const ConsoleConnector::RunResult startResult = connector.executeTemplate(
            QStringLiteral("sh -c \"printf failure >&2; exit 7\""),
            {},
            {},
            ConsoleConnector::ExecMode::StreamingAsync);

        QCOMPARE(startResult.exitCode, 0);
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(1000));
        }
        QCOMPARE(finishedSpy.count(), 1);
        const QList<QVariant> arguments = finishedSpy.takeFirst();
        const ConsoleConnector::RunResult result = qvariant_cast<ConsoleConnector::RunResult>(arguments.constFirst());
        QCOMPARE(result.exitCode, 7);
        QCOMPARE(QString::fromLocal8Bit(result.stderrData), QStringLiteral("failure"));
    }
};

QTEST_MAIN(PCAPTest)
#include "PCAPTest.moc"
