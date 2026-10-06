// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/libs/BackendConfig.hpp"

#include <QTest>

class BackendConfigTest final : public QObject {
    Q_OBJECT

private slots:
    void platformCandidatesFollowDeviceTreeOrder() {
        const QMap<QString, QString> overrides{
            {QStringLiteral("vendor,board-specific"), QStringLiteral("25")},
            {QStringLiteral("vendor,soc"), QStringLiteral("40")},
        };
        const QStringList candidates = backendConfigValueCandidates(QStringLiteral("15"), overrides,
                                                                    QByteArrayLiteral("vendor,soc") + '\0' +
                                                                        QByteArrayLiteral("vendor,board-specific") +
                                                                        '\0' + QByteArrayLiteral("vendor,soc") + '\0');

        QCOMPARE(candidates, QStringList({QStringLiteral("40"), QStringLiteral("25"), QStringLiteral("15")}));
    }

    void unmatchedOrUnavailablePlatformUsesGlobalValue() {
        const QMap<QString, QString> overrides{{QStringLiteral("vendor,board"), QStringLiteral("30")}};
        QCOMPARE(backendConfigValueCandidates(QStringLiteral("15"), overrides, QByteArrayLiteral("vendor,other\0")),
                 QStringList{QStringLiteral("15")});
        QCOMPARE(backendConfigValueCandidates(QStringLiteral("15"), overrides, {}), QStringList{QStringLiteral("15")});
    }

    void invalidPlatformValuesFallThroughToNextCandidate() {
        const QStringList candidates{QStringLiteral("not-a-number"), QStringLiteral("0"), QStringLiteral("-5"),
                                     QStringLiteral("30")};

        QCOMPARE(resolvePositiveIntegerConfigValue(candidates, 15, 3600, QStringLiteral("timeout")), 30);
        QCOMPARE(resolvePositiveIntegerConfigValue({QStringLiteral("999999")}, 15, 3600, QStringLiteral("timeout")),
                 15);
        QCOMPARE(resolvePositiveIntegerConfigValue({}, 15, 3600, QStringLiteral("timeout")), 15);
    }
};

QTEST_GUILESS_MAIN(BackendConfigTest)
#include "BackendConfigTest.moc"
