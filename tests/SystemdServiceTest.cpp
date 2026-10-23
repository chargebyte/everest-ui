// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "SystemdService.hpp"

#include <QTest>
#include <QTimer>

class FakeSystemdService final : public SystemdService {
public:
    QString state = QStringLiteral("deactivating");
    bool readable = true;
    int queries = 0;
    int failOnQuery = 0;
    quint32 mainPid = 0;
    quint32 controlPid = 0;
    bool processIdsReadable = true;
    QString stateAfterProcessQuery;

protected:
    bool readUnitState(const QString&, QString* result) override {
        ++queries;
        *result = state;
        return readable && queries != failOnQuery;
    }
    bool readServiceProcessIds(const QString&, quint32* main, quint32* control) override {
        *main = mainPid;
        *control = controlPid;
        if (!stateAfterProcessQuery.isEmpty())
            state = stateAfterProcessQuery;
        return processIdsReadable;
    }
};

class SystemdServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void onlyConfirmedInactiveMeansStopped_data() {
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("readable");
        QTest::addColumn<bool>("stopped");
        QTest::newRow("inactive") << QString("inactive") << true << true;
        QTest::newRow("active") << QString("active") << true << false;
        QTest::newRow("activating") << QString("activating") << true << false;
        QTest::newRow("deactivating") << QString("deactivating") << true << false;
        QTest::newRow("failed") << QString("failed") << true << false;
        QTest::newRow("unknown") << QString("unknown") << true << false;
        QTest::newRow("dbus-error") << QString("inactive") << false << false;
    }
    void onlyConfirmedInactiveMeansStopped() {
        QFETCH(QString, state);
        QFETCH(bool, readable);
        QFETCH(bool, stopped);
        FakeSystemdService service;
        service.state = state;
        service.readable = readable;
        QCOMPARE(service.waitForUnitStopped(QStringLiteral("everest.service"), 30), stopped);
    }

    void waitsThroughShutdownAndQueryFailure() {
        FakeSystemdService service;
        service.readable = false;
        QTimer::singleShot(120, &service, [&]() { service.readable = true; });
        QTimer::singleShot(240, &service, [&]() { service.state = QStringLiteral("inactive"); });
        QVERIFY(service.waitForUnitStopped(QStringLiteral("everest.service"), 1000));
        QVERIFY(service.queries >= 3);
    }

    void recoveryRequiresTerminalStateAndNoProcesses_data() {
        QTest::addColumn<QString>("state");
        QTest::addColumn<quint32>("mainPid");
        QTest::addColumn<quint32>("controlPid");
        QTest::addColumn<bool>("readable");
        QTest::addColumn<bool>("processIdsReadable");
        QTest::addColumn<QString>("nextState");
        QTest::addColumn<bool>("expected");
        for (const auto& state : {QString("inactive"), QString("failed"), QString("active"), QString("activating"),
                                  QString("deactivating"), QString("reloading"), QString("unknown")}) {
            QTest::newRow(qPrintable(state)) << state << quint32(0) << quint32(0) << true << true << QString()
                                             << (state == "inactive" || state == "failed");
        }
        QTest::newRow("main-process") << QString("inactive") << quint32(123) << quint32(0) << true << true << QString()
                                      << false;
        QTest::newRow("control-process") << QString("failed") << quint32(0) << quint32(123) << true << true << QString()
                                         << false;
        QTest::newRow("missing-or-unreadable-unit")
            << QString("inactive") << quint32(0) << quint32(0) << false << true << QString() << false;
        QTest::newRow("unreadable-or-missing-pids")
            << QString("inactive") << quint32(0) << quint32(0) << true << false << QString() << false;
        QTest::newRow("starts-during-query")
            << QString("inactive") << quint32(0) << quint32(0) << true << true << QString("activating") << false;
    }
    void recoveryRequiresTerminalStateAndNoProcesses() {
        QFETCH(QString, state);
        QFETCH(quint32, mainPid);
        QFETCH(quint32, controlPid);
        QFETCH(bool, readable);
        QFETCH(bool, processIdsReadable);
        QFETCH(QString, nextState);
        QFETCH(bool, expected);
        FakeSystemdService service;
        service.state = state;
        service.mainPid = mainPid;
        service.controlPid = controlPid;
        service.readable = readable;
        service.processIdsReadable = processIdsReadable;
        service.stateAfterProcessQuery = nextState;
        QCOMPARE(service.isServiceStoppedForRecovery(QStringLiteral("everest.service")), expected);
    }

    void recoveryFailsIfFinalStateCannotBeRead() {
        FakeSystemdService service;
        service.state = QStringLiteral("inactive");
        service.failOnQuery = 2;
        QVERIFY(!service.isServiceStoppedForRecovery(QStringLiteral("everest.service")));
        QCOMPARE(service.queries, 2);
    }
};

QTEST_GUILESS_MAIN(SystemdServiceTest)
#include "SystemdServiceTest.moc"
