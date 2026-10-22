// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "RebootSettings.hpp"
#include "BootIdentity.hpp"

#include <QRegularExpression>
#include <QTest>

class RebootSettingsTest final : public QObject {
    Q_OBJECT
private slots:
    void defaultsAndBaseValues() {
        QCOMPARE(resolveRebootWaitSeconds({}, {}, {}), 30);
        QCOMPARE(resolveRebootWaitSeconds(QStringLiteral("120"), {}, {}), 120);
        for (const auto& value :
             {QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("bad"), QStringLiteral("2147484")}) {
            QCOMPARE(resolveRebootWaitSeconds(value, {}, {}), 30);
        }
    }
    void platformOrderAndFallback() {
        const QMap<QString, QString> values{{QStringLiteral("chargebyte,board"), QStringLiteral("90")},
                                            {QStringLiteral("fsl,soc"), QStringLiteral("180")}};
        const QByteArray compatible("chargebyte,board\0fsl,soc\0", 24);
        QCOMPARE(resolveRebootWaitSeconds(QStringLiteral("60"), values, compatible), 90);
        auto invalid = values;
        invalid.insert(QStringLiteral("chargebyte,board"), QStringLiteral("bad"));
        QCOMPARE(resolveRebootWaitSeconds(QStringLiteral("60"), invalid, compatible), 180);
        QCOMPARE(resolveRebootWaitSeconds(QStringLiteral("60"), values, "unmatched"), 60);
        QCOMPARE(resolveRebootWaitSeconds(QStringLiteral("60"), values, {}), 60);
    }
    void bootIdIsStableAndValidWhenAvailable() {
        const auto id = UiRuntime::deviceBootId();
        QCOMPARE(UiRuntime::deviceBootId(), id);
        QVERIFY(id.isEmpty() ||
                QRegularExpression(QStringLiteral("^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$")).match(id).hasMatch());
    }
};

QTEST_GUILESS_MAIN(RebootSettingsTest)
#include "RebootSettingsTest.moc"
