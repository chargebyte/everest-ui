// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SSHConfiguration.hpp"

#include "ProtocolSchema.hpp"

#include <QJsonObject>
#include <QStringList>
#include <QTest>

class SSHConfigurationTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() {
        SSHConfiguration::resetTestOverrides();
    }

    void readMapsStatus() {
        SSHConfiguration::SystemdOperations operations;
        operations.isUnitActive = [](const QString &unit) {
            return unit == QStringLiteral("sshd.socket");
        };
        operations.isUnitEnabled = [](const QString &unit) {
            return unit == QStringLiteral("sshd.socket");
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 1,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionRead),
            .parameters = {},
        });

        QVERIFY(response.success);
        QCOMPARE(response.parameters.value(QStringLiteral("socket_active")).toBool(), true);
        QCOMPARE(response.parameters.value(QStringLiteral("socket_enabled")).toBool(), true);
    }

    void enableStartsAndEnablesSocket() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        operations.startUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("start:") + unit;
            return true;
        };
        operations.waitForUnitActive = [&calls](const QString &unit, bool active, int) {
            calls << QStringLiteral("wait:") + unit + QLatin1Char(':') +
                    (active ? QStringLiteral("active") : QStringLiteral("inactive"));
            return true;
        };
        operations.enableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("enable:") + unit;
            return true;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 2,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionEnable),
            .parameters = {},
        });

        QVERIFY(response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("enable:sshd.socket"),
                                     QStringLiteral("start:sshd.socket"),
                                     QStringLiteral("wait:sshd.socket:active")}));
    }

    void disableStopsAndDisablesSocket() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        operations.stopUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("stop:") + unit;
            return true;
        };
        operations.disableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("disable:") + unit;
            return true;
        };
        operations.waitForUnitActive = [&calls](const QString &unit, bool active, int) {
            calls << QStringLiteral("wait:") + unit + QLatin1Char(':') +
                    (active ? QStringLiteral("active") : QStringLiteral("inactive"));
            return true;
        };
        operations.terminateSessions = [&calls]() {
            calls << QStringLiteral("terminate_sessions");
            return true;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 3,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionDisable),
            .parameters = {},
        });

        QVERIFY(response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("stop:sshd.socket"),
                                     QStringLiteral("wait:sshd.socket:inactive"),
                                     QStringLiteral("disable:sshd.socket"),
                                     QStringLiteral("terminate_sessions")}));
    }

    void setPasswordValidatesAndWritesRootPassword() {
        QString writtenPassword;
        SSHConfiguration::setPasswordWriterForTest([&writtenPassword](const QString &password) {
            writtenPassword = password;
            return true;
        });

        ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 4,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionSetPassword),
            .parameters = QJsonObject{{QStringLiteral("password"), QStringLiteral("secret123")}},
        });

        QVERIFY(response.success);
        QCOMPARE(writtenPassword, QStringLiteral("secret123"));

        response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 5,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionSetPassword),
            .parameters = QJsonObject{{QStringLiteral("password"), QString()}},
        });

        QVERIFY(!response.success);
        QCOMPARE(response.parameters.value(QLatin1String(kError)).toString(),
                 QLatin1String(kErrorInvalidParams));
    }

    void setPasswordRejectsChpasswdRecordSeparators() {
        bool writerCalled = false;
        SSHConfiguration::setPasswordWriterForTest([&writerCalled](const QString &) {
            writerCalled = true;
            return true;
        });

        const QStringList invalidPasswords{
            QStringLiteral("secret\n123"),
            QStringLiteral("secret\r123"),
            QStringLiteral("secret") + QChar::Null + QStringLiteral("123"),
        };

        for (const QString &password : invalidPasswords) {
            writerCalled = false;

            const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
                .requestId = 6,
                .group = ModuleGroup::SSHConfiguration,
                .action = QLatin1String(kActionSetPassword),
                .parameters = QJsonObject{{QStringLiteral("password"), password}},
            });

            QVERIFY(!response.success);
            QCOMPARE(response.parameters.value(QLatin1String(kError)).toString(),
                     QLatin1String(kErrorInvalidParams));
            QVERIFY(!writerCalled);
        }
    }

    void unknownActionReturnsUnsupportedActionError() {
        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 7,
            .group = ModuleGroup::SSHConfiguration,
            .action = QStringLiteral("unknown_action"),
            .parameters = {},
        });

        QVERIFY(!response.success);
        QCOMPARE(response.requestId, qint64(7));
        QCOMPARE(response.group, QLatin1String(kGroupSSH));
        QCOMPARE(response.action, QStringLiteral("unknown_action"));
        QCOMPARE(response.parameters.value(QLatin1String(kError)).toString(),
                 QStringLiteral("unsupported_action"));
        QVERIFY(response.final);
    }
};

QTEST_MAIN(SSHConfigurationTest)
#include "SSHConfigurationTest.moc"
