// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "FactoryResetRuntime.hpp"

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
    int m_reboots = 0;

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
                    m_checked = indices;
                    return m_unplugged;
                },
                [this]() { ++m_reboots; }};
    }

    ModuleRequest request(const QString& action, QJsonObject parameters = {}) {
        return {42, ModuleGroup::FactoryReset, action, parameters};
    }

    ModuleResponse last(const QSignalSpy& spy) {
        return qvariant_cast<ModuleResponse>(spy.last().at(0));
    }

private slots:
    void initTestCase() {
        qRegisterMetaType<ModuleResponse>();
    }

    void init() {
        QVERIFY(m_dir.isValid());
        m_config = {{"factory_reset_command", script("exit 0\n")}};
        m_effective = QJsonDocument::fromJson(R"({"active_modules":{"evse":{"module":"EvseManager"}}})").object();
        m_unplugged = true;
        m_checked.clear();
        m_loads = m_reboots = 0;
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
        QVERIFY(!last(responses).parameters.contains("restart_required"));
        QCOMPARE(m_checked, QList<int>{1});
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
    }

    void confirmationAndBusyRejectionsDoNotRun() {
        FactoryResetRuntime runtime(actions());
        QVERIFY(!runtime.handleRequest(request("reset")).success);
        QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", "true"}})).success);
        QVERIFY(!runtime.handleRequest(request("reset", {{"confirm", true}}), true).success);
        QVERIFY(!runtime.handleRequest(request("reboot")).success);
        QCOMPARE(m_loads, 0);
        QCOMPARE(m_reboots, 0);
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
        QVERIFY(final.parameters.value("restart_required").toBool());
        QCOMPARE(final.parameters.size(), success ? 1 : 2);
        if (!success)
            QCOMPARE(final.parameters.value("error").toString(), QString("factory_reset_failed"));
        QVERIFY(runtime.handleRequest(request("read_status")).parameters.value("reboot_available").toBool());
        const int count = responses.count();
        QTest::qWait(30);
        QCOMPARE(responses.count(), count);
        QVERIFY(!runtime.handleRequest(request("reboot"), true).success);
        QVERIFY(runtime.handleRequest(request("reboot")).success);
        QVERIFY(runtime.isRebootPending());
        QCOMPARE(m_reboots, 1);
        QVERIFY(!runtime.handleRequest(request("reboot")).success);
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
        QCOMPARE(status.parameters.value("state").toString(), QString("succeeded"));
        QVERIFY(status.parameters.value("reboot_available").toBool());
    }
};

QTEST_GUILESS_MAIN(FactoryResetTest)
#include "FactoryResetTest.moc"
