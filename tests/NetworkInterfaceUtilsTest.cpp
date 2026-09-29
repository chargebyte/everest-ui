// SPDX-License-Identifier: Apache-2.0

#include "NetworkInterfaceUtils.hpp"

#include <QtTest>

class NetworkInterfaceUtilsTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesDriverList() {
        QCOMPARE(NetworkInterfaceUtils::parseIsoHighLevelCommsDriverList(
                     QStringLiteral(" mse102x, , qcaspi ,, ")),
                 QStringList({QStringLiteral("mse102x"), QStringLiteral("qcaspi")}));
    }

    void matchesDriverCaseInsensitively() {
        const QStringList drivers{QStringLiteral("mse102x")};
        QVERIFY(NetworkInterfaceUtils::isIsoHighLevelCommsDriver(
            QStringLiteral("MSE102X"), drivers));
        QVERIFY(!NetworkInterfaceUtils::isIsoHighLevelCommsDriver(
            QStringLiteral("other"), drivers));
        QVERIFY(!NetworkInterfaceUtils::isIsoHighLevelCommsDriver(
            QStringLiteral("mse102x"), {}));
    }

    void likelyClassificationRequiresEveryCondition() {
        using NetworkInterfaceUtils::isLikelyIsoHighLevelCommsInterface;
        QVERIFY(isLikelyIsoHighLevelCommsInterface(false, false, true, true, true, true));
        QVERIFY(!isLikelyIsoHighLevelCommsInterface(true, false, true, true, true, true));
        QVERIFY(!isLikelyIsoHighLevelCommsInterface(false, true, true, true, true, true));
        QVERIFY(!isLikelyIsoHighLevelCommsInterface(false, false, false, true, true, true));
        QVERIFY(!isLikelyIsoHighLevelCommsInterface(false, false, true, false, true, true));
        QVERIFY(!isLikelyIsoHighLevelCommsInterface(false, false, true, true, false, true));
        QVERIFY(!isLikelyIsoHighLevelCommsInterface(false, false, true, true, true, false));
    }
};

QTEST_GUILESS_MAIN(NetworkInterfaceUtilsTest)
#include "NetworkInterfaceUtilsTest.moc"
