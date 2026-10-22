// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "FactoryResetRuntime.hpp"
#include "LoginState.hpp"

#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class FactoryResetTest final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QMap<QString, QString> m_config;
    QJsonObject m_effective;
    QList<int> m_checked;
    bool m_unplugged = true;
    int m_loads = 0;
    int m_stops = 0;
    QString m_stopError;
    QStringList m_events;
    QByteArray m_previousRuntime;

    QString script(const QByteArray& body) {
        const auto path = m_dir.filePath(QStringLiteral("fake-reset"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("#!/bin/sh\n" + body) < 0 ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            qFatal("Cannot create fake reset script");
        }
        return path;
    }

    FactoryResetRuntime::Actions actions() {
        return {[this](const QString& key) { return m_config.value(key); },
                [this]() {
                    ++m_loads;
                    return m_effective;
                },
                [this](const QList<int>& indices) {
                    m_events.append(QStringLiteral("safety_check"));
                    m_checked = indices;
                    return m_unplugged;
                },
                [this]() {
                    ++m_stops;
                    m_events.append(QStringLiteral("stop_everest"));
                    if (m_stopError.isEmpty()) {
                        m_unplugged = false; // RPC becomes unavailable when EVerest stops.
                    }
                    return EverestServiceControlResult{m_stopError.isEmpty(), m_stopError};
                }};
    }

    ModuleRequest request(const QString& action, QJsonObject parameters = {}) {
        return {42, ModuleGroup::FactoryReset, action, parameters};
    }

    ModuleResponse last(const QSignalSpy& spy) {
        return qvariant_cast<ModuleResponse>(spy.last().at(0));
    }

private slots:
    void initTestCase() {
        m_previousRuntime = qgetenv("RUNTIME_DIRECTORY");
        qputenv("RUNTIME_DIRECTORY", m_dir.path().toUtf8());
        qRegisterMetaType<ModuleResponse>();
    }

    void cleanupTestCase() {
        if (m_previousRuntime.isNull())
            qunsetenv("RUNTIME_DIRECTORY");
        else
            qputenv("RUNTIME_DIRECTORY", m_previousRuntime);
    }

    void init() {
        QVERIFY(m_dir.isValid());
        QString error;
        QVERIFY2(UiRuntime::initializeLoginState(error), qPrintable(error));
        m_config = {{"factory_reset_command", script("exit 0\n")}};
        m_effective = QJsonDocument::fromJson(R"({"active_modules":{"evse":{"module":"EvseManager"}}})").object();
        m_unplugged = true;
        m_checked.clear();
        m_loads = 0;
        m_stops = 0;
        m_stopError.clear();
        m_events.clear();
    }

    void resolvesEveryEvseAndRejectsIncompleteMappings() {
        QCOMPARE(FactoryResetRuntime::resolveEvseIndices(m_effective), QList<int>{1});
        auto config = QJsonDocument::fromJson(R"({"active_modules":{
            "a":{"module":"EvseManager","mapping":{"module":{"evse":2}}},
            "b":{"module":"EvseManager","mapping":{"module":{"evse":3}}}}})")
                          .object();
        QCOMPARE(FactoryResetRuntime::resolveEvseIndices(config), (QList<int>{2, 3}));
        auto modules = config.value("active_modules").toObject();
        modules.insert("c", QJsonObject{{"module", "EvseManager"}});
        config.insert("active_modules", modules);
        QVERIFY(FactoryResetRuntime::resolveEvseIndices(config).isEmpty());
        QVERIFY(FactoryResetRuntime::resolveEvseIndices({}).isEmpty());
        for (const auto& invalid : {QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue("2")}) {
            config = {{"active_modules",
                       QJsonObject{
                           {"a", QJsonObject{{"module", "EvseManager"},
                                             {"mapping", QJsonObject{{"module", QJsonObject{{"evse", invalid}}}}}}}}}};
            QVERIFY(FactoryResetRuntime::resolveEvseIndices(config).isEmpty());
        }
    }

    void safetyPolicyDefaultsToEnforced_data() {
        QTest::addColumn<QString>("value");
        QTest::newRow("absent") << QString();
        QTest::newRow("true") << QString("true");
        QTest::newRow("uppercase") << QString("TRUE");
        QTest::newRow("invalid") << QString("no");
    }
    void safetyPolicyDefaultsToEnforced() {
        QFETCH(QString, value);
        m_config.insert("factory_reset_require_unplugged", value);
        m_unplugged = false;
        FactoryResetRuntime runtime(actions());
        QVERIFY(!runtime.handleRequest(request("read_status")).parameters.value("can_reset").toBool());
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", true}})).final);
        QTRY_VERIFY(!responses.isEmpty());
        QVERIFY(last(responses).final);
        QCOMPARE(last(responses).parameters.value("error").toString(), QString("factory_reset_not_allowed"));
        QVERIFY(!last(responses).parameters.contains("reboot_expected"));
        QCOMPARE(m_checked, QList<int>{1});
        QCOMPARE(m_stops, 0);
    }

    void bypassNeedsNeitherConfigNorRpcAndRechecksPolicy() {
        m_config.insert("factory_reset_require_unplugged", "FALSE");
        m_effective = {};
        m_unplugged = false;
        FactoryResetRuntime runtime(actions());
        QVERIFY(runtime.handleRequest(request("read_status")).parameters.value("can_reset").toBool());
        QCOMPARE(m_loads, 0);
        m_config.insert("factory_reset_require_unplugged", "true");
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY(!responses.isEmpty());
        QVERIFY(!last(responses).success);
        QCOMPARE(m_loads, 1);
        QCOMPARE(m_stops, 0);
    }

    void confirmationAndBusyRejectionsDoNotRun() {
        FactoryResetRuntime runtime(actions());
        QVERIFY(!runtime.handleRequest(request("reset")).success);
        QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", "true"}})).success);
        QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", true}}), true).success);
        QVERIFY(!runtime.handleRequest(request("reboot")).success);
        QCOMPARE(m_loads, 0);
        QTest::qWait(10);
        QCOMPARE(m_stops, 0);
    }

    void stopsAfterSafetyCheckBeforeRunningHelper() {
        const auto marker = m_dir.filePath(QStringLiteral("helper-started"));
        m_config.insert("factory_reset_command", script("touch '" + marker.toUtf8() + "'\nexit 0\n"));
        auto callbacks = actions();
        const auto stop = callbacks.stopEverest;
        callbacks.stopEverest = [&]() {
            // The process must not have started, even though status is already running.
            if (QFile::exists(marker)) {
                return EverestServiceControlResult{false, QStringLiteral("helper_started_before_stop")};
            }
            return stop();
        };
        FactoryResetRuntime runtime(callbacks);
        QVERIFY(runtime.handleRequest(request("read_status")).parameters.value("can_reset").toBool());
        QCOMPARE(m_stops, 0);
        m_events.clear();
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY(!runtime.isRunning());
        QVERIFY(runtime.isRebootExpected());
        QCOMPARE(m_events, (QStringList{"safety_check", "stop_everest"}));
        QCOMPARE(m_stops, 1);
        QVERIFY(QFile::exists(marker));
        QVERIFY(QFile::remove(marker));
    }

    void stopFailureDoesNotRunHelper_data() {
        QTest::addColumn<QString>("error");
        QTest::newRow("stop-failed") << QString("everest_stop_failed");
        QTest::newRow("stop-timeout") << QString("everest_stop_timeout");
    }
    void stopFailureDoesNotRunHelper() {
        QFETCH(QString, error);
        m_stopError = error;
        const auto marker = m_dir.filePath(QStringLiteral("unexpected-helper"));
        m_config.insert("factory_reset_command", script("touch '" + marker.toUtf8() + "'\nexit 0\n"));
        FactoryResetRuntime runtime(actions());
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY(!runtime.isRunning());
        QVERIFY(!last(responses).success);
        QCOMPARE(last(responses).parameters, (QJsonObject{{"error", "factory_reset_failed"}}));
        QCOMPARE(runtime.handleRequest(request("read_status")).parameters.value("state").toString(), QString("failed"));
        QCOMPARE(m_stops, 1);
        QVERIFY(!QFile::exists(marker));
        QVERIFY(UiRuntime::readLoginState().allowed);
    }

    void keepaliveAndExclusivityDuringStop() {
        m_config.insert("factory_reset_require_unplugged", "false");
        m_config.insert("factory_reset_timeout_seconds", "1");
        auto callbacks = actions();
        bool keepaliveWhileStopping = false;
        callbacks.stopEverest = [&]() {
            ++m_stops;
            QEventLoop loop;
            QTimer::singleShot(2200, &loop, &QEventLoop::quit);
            loop.exec();
            return EverestServiceControlResult{true, {}};
        };
        FactoryResetRuntime runtime(callbacks);
        connect(&runtime, &FactoryResetRuntime::responseReady, &runtime, [&](const ModuleResponse& reply) {
            if (!reply.final) {
                keepaliveWhileStopping = runtime.isRunning();
                QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", true}})).success);
                const auto status = runtime.handleRequest(request("read_status"));
                QCOMPARE(status.parameters.value("state").toString(), QString("running"));
                QVERIFY(!status.parameters.value("can_reset").toBool());
            }
        });
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isRunning(), 5000);
        QVERIFY(keepaliveWhileStopping);
        QVERIFY(runtime.isRebootExpected()); // Stop time did not consume the helper's one-second budget.
        QCOMPARE(m_stops, 1);
        QCOMPARE(m_loads, 0);
    }

    void failedResetRequiresRecoveryBeforeRpcGuardedRetry() {
        m_config.insert("factory_reset_command", script("exit 9\n"));
        FactoryResetRuntime runtime(actions());
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY(!runtime.isRunning());
        QCOMPARE(last(responses).parameters.value("error").toString(), QString("factory_reset_failed"));
        const auto status = runtime.handleRequest(request("read_status"));
        QCOMPARE(status.parameters.value("state").toString(), QString("failed"));
        QVERIFY(!status.parameters.value("can_reset").toBool());
        QVERIFY(!m_unplugged);
        QCOMPARE(m_stops, 1);

        const int count = responses.count();
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY(responses.count() > count);
        QCOMPARE(last(responses).parameters.value("error").toString(), QString("factory_reset_not_allowed"));
        QCOMPARE(m_stops, 1);

        m_unplugged = true; // Administrator has recovered EVerest and RPC reports Unplugged again.
        QVERIFY(runtime.handleRequest(request("read_status")).parameters.value("can_reset").toBool());
    }

    void invalidCommandDoesNotStopEverest() {
        m_config.insert("factory_reset_command", "\"\"");
        FactoryResetRuntime runtime(actions());
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY(!runtime.isRunning());
        QCOMPARE(last(responses).parameters.value("error").toString(), QString("factory_reset_failed"));
        QCOMPARE(m_stops, 0);
        QVERIFY(m_checked.isEmpty());
    }

    void commandOutcomes_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<bool>("success");
        QTest::newRow("success") << QByteArray("exit 0\n") << true;
        QTest::newRow("nonzero") << QByteArray("echo private-detail >&2\nexit 9\n") << false;
        QTest::newRow("crash") << QByteArray("kill -KILL $$\n") << false;
        QTest::newRow("timeout") << QByteArray("trap '' TERM\nexec sleep 30\n") << false;
        QTest::newRow("failed-to-start") << QByteArray() << false;
    }
    void commandOutcomes() {
        QFETCH(QByteArray, body);
        QFETCH(bool, success);
        m_config.insert("factory_reset_require_unplugged", "false");
        m_config.insert("factory_reset_timeout_seconds", "1");
        m_config.insert("factory_reset_command", body.isEmpty() ? "/no-such-test-command" : script(body));
        FactoryResetRuntime runtime(actions());
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        const auto accepted = runtime.handleRequest(request("reset", {{"confirm", true}}));
        QVERIFY(accepted.success && !accepted.final);
        QVERIFY(runtime.isRunning());
        QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", true}})).success);
        const auto running = runtime.handleRequest(request("read_status"));
        QCOMPARE(running.parameters.value("state").toString(), QString("running"));
        QVERIFY(!running.parameters.value("can_reset").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isRunning(), 6000);
        const auto final = last(responses);
        QVERIFY(final.final);
        QCOMPARE(final.success, success);
        QCOMPARE(m_stops, 1); // Bypassing RPC never bypasses stopping EVerest.
        const auto login = UiRuntime::readLoginState();
        QCOMPARE(login.allowed, !success);
        QCOMPARE(login.reason, success ? QStringLiteral("waiting_for_reboot") : QString());
        QCOMPARE(login.operation, success ? QStringLiteral("factory_reset") : QString());
        QCOMPARE(final.parameters.value("reboot_expected").toBool(), success);
        QCOMPARE(final.parameters.size(), 1);
        if (!success)
            QCOMPARE(final.parameters.value("error").toString(), QString("factory_reset_failed"));
        const auto outcome = runtime.handleRequest(request("read_status"));
        QCOMPARE(outcome.parameters.value("reboot_expected").toBool(), success);
        QCOMPARE(outcome.parameters.value("state").toString(), success ? QString("rebooting") : QString("failed"));
        const int count = responses.count();
        QTest::qWait(30);
        QCOMPARE(responses.count(), count);
        QVERIFY(!runtime.handleRequest(request("reboot")).success);
        if (success) {
            QVERIFY(runtime.isRebootExpected());
            QVERIFY(!outcome.parameters.value("can_reset").toBool());
            QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", true}})).success);
        } else {
            QVERIFY(!runtime.isRebootExpected());
            QVERIFY(outcome.parameters.value("can_reset").toBool());
        }
    }

    void defaultBudgetAllowsTwoSecondKeepalivesAndStatusRecovery() {
        m_config.insert("factory_reset_require_unplugged", "false");
        m_config.insert("factory_reset_command", script("sleep 3\nexit 0\n"));
        FactoryResetRuntime runtime(actions());
        QSignalSpy responses(&runtime, &FactoryResetRuntime::responseReady);
        runtime.handleRequest(request("reset", {{"confirm", true}}));
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty(), 2800);
        QVERIFY(!last(responses).final);
        QVERIFY(last(responses).success);
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isRunning(), 5000);
        auto reconnect = request("read_status");
        reconnect.requestId = 100;
        const auto status = runtime.handleRequest(reconnect);
        QCOMPARE(status.requestId, qint64(100));
        QCOMPARE(status.parameters.value("state").toString(), QString("rebooting"));
        QVERIFY(status.parameters.value("reboot_expected").toBool());
    }
};

QTEST_GUILESS_MAIN(FactoryResetTest)
#include "FactoryResetTest.moc"
