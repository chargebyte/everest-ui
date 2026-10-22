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

protected:
    bool readUnitState(const QString&, QString* result) override {
        ++queries;
        *result = state;
        return readable;
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
};

QTEST_GUILESS_MAIN(SystemdServiceTest)
#include "SystemdServiceTest.moc"
