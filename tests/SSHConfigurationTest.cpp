// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SSHConfiguration.hpp"

#include "ProtocolSchema.hpp"

#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

namespace {
void setAvailableSshUnits(SSHConfiguration::SystemdOperations& operations, const QStringList& units) {
    SSHConfiguration::setControlMethodReaderForTest([] { return QStringLiteral("systemd"); });
    operations.isUnitAvailable = [units](const QString& unit) { return units.contains(unit); };
}
} // namespace

class SSHConfigurationTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() {
        SSHConfiguration::resetTestOverrides();
    }

    void autostartReadMapsStatus_data() {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QByteArray>("output");
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<bool>("success");
        QTest::addColumn<bool>("enabled");
        QTest::newRow("missing-setting-default") << QString() << QByteArray("enabled\n") << 0 << true << true;
        QTest::newRow("empty-setting-default") << QStringLiteral("") << QByteArray("disabled\n") << 1 << true << false;
        QTest::newRow("whitespace-setting-default")
            << QStringLiteral(" ") << QByteArray("enabled") << 0 << true << true;
        QTest::newRow("explicit-helper") << QStringLiteral("ssh-autostart") << QByteArray(" enabled\n") << 0 << true
                                         << true;
        QTest::newRow("disabled-is-not-failure")
            << QStringLiteral("ssh-autostart") << QByteArray("disabled") << 1 << true << false;
        QTest::newRow("enabled-with-wrong-exit") << QString() << QByteArray("enabled") << 1 << false << false;
        QTest::newRow("disabled-with-wrong-exit") << QString() << QByteArray("disabled") << 0 << false << false;
        QTest::newRow("unexpected-output") << QString() << QByteArray("unknown") << 0 << false << false;
        QTest::newRow("empty-output") << QString() << QByteArray() << 1 << false << false;
        QTest::newRow("command-failure") << QString() << QByteArray("disabled") << 2 << false << false;
    }

    void autostartReadMapsStatus() {
        QFETCH(QString, method);
        QFETCH(QByteArray, output);
        QFETCH(int, exitCode);
        QFETCH(bool, success);
        QFETCH(bool, enabled);
        SSHConfiguration::setControlMethodReaderForTest([method] { return method; });
        QString program;
        QStringList arguments;
        SSHConfiguration::setAutostartRunnerForTest([&](const QString& path, const QStringList& args) {
            program = path;
            arguments = args;
            return SSHConfiguration::AutostartResult{
                .started = true, .normalExit = true, .exitCode = exitCode, .stdoutData = output};
        });
        bool systemdCalled = false;
        SSHConfiguration::SystemdOperations operations;
        operations.isUnitAvailable = [&](const QString&) {
            systemdCalled = true;
            return true;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);
        if (!success) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("SSH autostart failed: .*")));
        }
        const ModuleResponse response = SSHConfiguration::handleRequest({.requestId = 12,
                                                                         .group = ModuleGroup::SSHConfiguration,
                                                                         .action = QStringLiteral("read"),
                                                                         .parameters = {}});
        QCOMPARE(response.success, success);
        QCOMPARE(program, QStringLiteral("/usr/sbin/ssh-autostart"));
        QCOMPARE(arguments, QStringList{QStringLiteral("--status")});
        QVERIFY(!systemdCalled);
        QVERIFY(response.final);
        if (success) {
            QCOMPARE(response.parameters.value(QStringLiteral("enabled")).toBool(), enabled);
            QVERIFY(response.parameters.value(QStringLiteral("active")).isNull());
            QVERIFY(!response.parameters.contains(QStringLiteral("socket_active")));
            QVERIFY(!response.parameters.contains(QStringLiteral("socket_enabled")));
        } else {
            QCOMPARE(response.parameters.value(QStringLiteral("error")).toString(),
                     QStringLiteral("ssh_autostart_failed"));
        }
    }

    void autostartCommandsHandleProcessOutcomes_data() {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("outcome");
        for (const QString& action : {QStringLiteral("read"), QStringLiteral("enable"), QStringLiteral("disable")}) {
            for (const QString& outcome :
                 {QStringLiteral("success"), QStringLiteral("nonzero"), QStringLiteral("missing"),
                  QStringLiteral("timeout"), QStringLiteral("crash")}) {
                QTest::newRow(qPrintable(action + QLatin1Char('-') + outcome)) << action << outcome;
            }
        }
    }

    void autostartCommandsHandleProcessOutcomes() {
        QFETCH(QString, action);
        QFETCH(QString, outcome);
        SSHConfiguration::setControlMethodReaderForTest([] { return QStringLiteral("ssh-autostart"); });
        QStringList calls;
        SSHConfiguration::setAutostartRunnerForTest([&](const QString& program, const QStringList& arguments) {
            calls.append(program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
            return SSHConfiguration::AutostartResult{
                .started = outcome != QStringLiteral("missing"),
                .timedOut = outcome == QStringLiteral("timeout"),
                .normalExit = outcome != QStringLiteral("crash"),
                .exitCode = outcome == QStringLiteral("success") ? 0 : 1,
                .stdoutData = QByteArray("enabled\n"),
            };
        });
        bool systemdCalled = false;
        SSHConfiguration::SystemdOperations operations;
        operations.isUnitAvailable = [&](const QString&) {
            systemdCalled = true;
            return true;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);
        const bool success = outcome == QStringLiteral("success");
        if (!success) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("SSH autostart failed: .*")));
        }
        const ModuleResponse response = SSHConfiguration::handleRequest(
            {.requestId = 13, .group = ModuleGroup::SSHConfiguration, .action = action, .parameters = {}});
        QCOMPARE(response.success, success);
        const QString flag =
            action == QStringLiteral("read") ? QStringLiteral("--status") : QStringLiteral("--") + action;
        QCOMPARE(calls, QStringList{QStringLiteral("/usr/sbin/ssh-autostart ") + flag});
        QVERIFY(!systemdCalled);
        if (!success) {
            QCOMPARE(response.parameters.value(QStringLiteral("error")).toString(),
                     QStringLiteral("ssh_autostart_failed"));
        } else if (action != QStringLiteral("read")) {
            QVERIFY(response.parameters.isEmpty());
        }
    }

    void invalidControlMethodRejectsServiceActions() {
        SSHConfiguration::setControlMethodReaderForTest([] { return QStringLiteral("invalid"); });
        bool runnerCalled = false;
        SSHConfiguration::setAutostartRunnerForTest([&](const QString&, const QStringList&) {
            runnerCalled = true;
            return SSHConfiguration::AutostartResult{};
        });
        for (const QString& action : {QStringLiteral("read"), QStringLiteral("enable"), QStringLiteral("disable")}) {
            QTest::ignoreMessage(QtWarningMsg, "Invalid ssh_control_method: 'invalid'");
            const ModuleResponse response = SSHConfiguration::handleRequest(
                {.requestId = 14, .group = ModuleGroup::SSHConfiguration, .action = action, .parameters = {}});
            QVERIFY(!response.success);
            QCOMPARE(response.parameters.value(QStringLiteral("error")).toString(),
                     QStringLiteral("ssh_control_method_invalid"));
        }
        QVERIFY(!runnerCalled);
    }

    void invalidControlMethodDoesNotBlockPasswordChanges() {
        SSHConfiguration::setControlMethodReaderForTest([] { return QStringLiteral("invalid"); });
        QString password;
        SSHConfiguration::setPasswordWriterForTest([&](const QString& value) {
            password = value;
            return true;
        });
        const ModuleResponse response = SSHConfiguration::handleRequest(
            {.requestId = 15,
             .group = ModuleGroup::SSHConfiguration,
             .action = QStringLiteral("set_password"),
             .parameters = {{QStringLiteral("password"), QStringLiteral("example-password")}}});
        QVERIFY(response.success);
        QCOMPARE(password, QStringLiteral("example-password"));
    }

    void readMapsStatus() {
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket")});
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
        QCOMPARE(response.parameters.value(QStringLiteral("active")).toBool(), true);
        QCOMPARE(response.parameters.value(QStringLiteral("enabled")).toBool(), true);
    }

    void readMapsServiceStatus() {
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.service")});
        operations.isUnitActive = [](const QString &unit) {
            return unit == QStringLiteral("sshd.service");
        };
        operations.isUnitEnabled = [](const QString &) { return false; };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 2,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionRead),
            .parameters = {},
        });

        QVERIFY(response.success);
        QCOMPARE(response.parameters.value(QStringLiteral("active")).toBool(), true);
        QCOMPARE(response.parameters.value(QStringLiteral("enabled")).toBool(), false);
    }

    void enableStartsAndEnablesSocket() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket"), QStringLiteral("sshd.service")});
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
        operations.reloadManager = [&calls]() {
            calls << QStringLiteral("reload");
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
        QCOMPARE(calls, QStringList({QStringLiteral("enable:sshd.socket"), QStringLiteral("reload"),
                                     QStringLiteral("start:sshd.socket"),
                                     QStringLiteral("wait:sshd.socket:active")}));
    }

    void enableFallsBackToService() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.service")});
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
        operations.reloadManager = [&calls]() {
            calls << QStringLiteral("reload");
            return true;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 3,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionEnable),
            .parameters = {},
        });

        QVERIFY(response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("enable:sshd.service"), QStringLiteral("reload"),
                                     QStringLiteral("start:sshd.service"),
                                     QStringLiteral("wait:sshd.service:active")}));
    }

    void disableStopsAndDisablesSocket() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket")});
        operations.stopUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("stop:") + unit;
            return true;
        };
        operations.disableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("disable:") + unit;
            return true;
        };
        operations.reloadManager = [&calls]() {
            calls << QStringLiteral("reload");
            return true;
        };
        operations.waitForUnitActive = [&calls](const QString &unit, bool active, int) {
            calls << QStringLiteral("wait:") + unit + QLatin1Char(':') +
                    (active ? QStringLiteral("active") : QStringLiteral("inactive"));
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
                                     QStringLiteral("reload")}));
    }

    void disableStopsAndDisablesAllAvailableUnits() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket"), QStringLiteral("sshd.service")});
        operations.stopUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("stop:") + unit;
            return true;
        };
        operations.disableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("disable:") + unit;
            return true;
        };
        operations.reloadManager = [&calls]() {
            calls << QStringLiteral("reload");
            return true;
        };
        operations.waitForUnitActive = [&calls](const QString &unit, bool active, int) {
            calls << QStringLiteral("wait:") + unit + QLatin1Char(':') +
                    (active ? QStringLiteral("active") : QStringLiteral("inactive"));
            return true;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 4,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionDisable),
            .parameters = {},
        });

        QVERIFY(response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("stop:sshd.socket"),
                                     QStringLiteral("wait:sshd.socket:inactive"),
                                     QStringLiteral("stop:sshd.service"),
                                     QStringLiteral("wait:sshd.service:inactive"),
                                     QStringLiteral("disable:sshd.socket"),
                                     QStringLiteral("disable:sshd.service"), QStringLiteral("reload")}));
    }

    void disableFailsWhenNoSshUnitIsAvailable() {
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {});
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 5,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionDisable),
            .parameters = {},
        });

        QVERIFY(!response.success);
        QCOMPARE(response.parameters.value(QLatin1String(kError)).toString(),
                 QStringLiteral("ssh_systemd_failed"));
    }

    void enableFailureRollsBackBootEnablement() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket")});
        operations.enableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("enable:") + unit;
            return true;
        };
        operations.disableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("disable:") + unit;
            return true;
        };
        operations.reloadManager = [&calls]() {
            calls << QStringLiteral("reload");
            return true;
        };
        operations.startUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("start:") + unit;
            return false;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 4,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionEnable),
            .parameters = {},
        });

        QVERIFY(!response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("enable:sshd.socket"), QStringLiteral("reload"),
                                     QStringLiteral("start:sshd.socket"),
                                     QStringLiteral("disable:sshd.socket"), QStringLiteral("reload")}));
    }

    void enableWaitFailureStopsSocketAndRollsBackBootEnablement() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket")});
        operations.enableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("enable:") + unit;
            return true;
        };
        operations.disableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("disable:") + unit;
            return true;
        };
        operations.reloadManager = [&calls]() {
            calls << QStringLiteral("reload");
            return true;
        };
        operations.startUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("start:") + unit;
            return true;
        };
        operations.stopUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("stop:") + unit;
            return true;
        };
        operations.waitForUnitActive = [&calls](const QString &unit, bool active, int) {
            calls << QStringLiteral("wait:") + unit + QLatin1Char(':') +
                    (active ? QStringLiteral("active") : QStringLiteral("inactive"));
            return false;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 5,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionEnable),
            .parameters = {},
        });

        QVERIFY(!response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("enable:sshd.socket"), QStringLiteral("reload"),
                                     QStringLiteral("start:sshd.socket"),
                                     QStringLiteral("wait:sshd.socket:active"),
                                     QStringLiteral("stop:sshd.socket"),
                                     QStringLiteral("disable:sshd.socket"), QStringLiteral("reload")}));
    }

    void disableReloadFailureKeepsSshDisabled() {
        QStringList calls;
        SSHConfiguration::SystemdOperations operations;
        setAvailableSshUnits(operations, {QStringLiteral("sshd.socket")});
        operations.stopUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("stop:") + unit;
            return true;
        };
        operations.waitForUnitActive = [&calls](const QString &unit, bool active, int) {
            calls << QStringLiteral("wait:") + unit + QLatin1Char(':') +
                    (active ? QStringLiteral("active") : QStringLiteral("inactive"));
            return true;
        };
        operations.disableUnit = [&calls](const QString &unit) {
            calls << QStringLiteral("disable:") + unit;
            return true;
        };
        int reloadCalls = 0;
        operations.reloadManager = [&calls, &reloadCalls]() {
            calls << QStringLiteral("reload");
            return ++reloadCalls != 1;
        };
        SSHConfiguration::setSystemdOperationsForTest(operations);

        const ModuleResponse response = SSHConfiguration::handleRequest(ModuleRequest{
            .requestId = 6,
            .group = ModuleGroup::SSHConfiguration,
            .action = QLatin1String(kActionDisable),
            .parameters = {},
        });

        QVERIFY(!response.success);
        QCOMPARE(calls, QStringList({QStringLiteral("stop:sshd.socket"),
                                     QStringLiteral("wait:sshd.socket:inactive"),
                                     QStringLiteral("disable:sshd.socket"), QStringLiteral("reload")}));
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
