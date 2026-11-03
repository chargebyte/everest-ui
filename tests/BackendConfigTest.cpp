// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/libs/BackendConfig.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class BackendConfigTest final : public QObject {
    Q_OBJECT

private slots:
    void featureListsAndDefaults() {
        UiConfig::Snapshot config;
        const auto resolve = [&]() { return backendFeatures(config, {}); };
        QVERIFY(resolve().value("network").toBool());
        QVERIFY(resolve().value("safety").toBool());
        for (const auto& value : {QString(), QStringLiteral("none"), QStringLiteral("unknown")}) {
            config.entries.insert("available_features", {value, {}, 0});
            QVERIFY(!resolve().value("network").toBool());
            QVERIFY(!resolve().value("safety").toBool());
        }
        config.entries["available_features"].value = " safety , network,unknown,network ";
        QVERIFY(resolve().value("network").toBool());
        QVERIFY(resolve().value("safety").toBool());
        config.error = "configuration unreadable";
        QVERIFY(!resolve().value("network").toBool());
        QVERIFY(!resolve().value("safety").toBool());
    }

    void platformFeaturesReplaceGlobalInDeviceTreeOrder() {
        UiConfig::Snapshot config;
        config.entries.insert("available_features", {"network", {}, 0});
        config.entries.insert("available_features.board", {"safety", {}, 0});
        config.entries.insert("available_features.soc", {"network", {}, 0});
        const QByteArray compatible = QByteArrayLiteral("board\0soc\0");
        auto features = backendFeatures(config, compatible);
        QVERIFY(features.value("safety").toBool());
        QVERIFY(!features.value("network").toBool());
        for (const auto& value : {QString(), QStringLiteral("none"), QStringLiteral("unknown")}) {
            config.entries["available_features.board"].value = value;
            features = backendFeatures(config, compatible);
            QVERIFY(!features.value("safety").toBool());
            QVERIFY(!features.value("network").toBool());
        }
        config.entries.remove("available_features.board");
        QVERIFY(backendFeatures(config, compatible).value("network").toBool());
        QVERIFY(backendFeatures(config, {}).value("network").toBool());
        QVERIFY(backendFeatures(config, QByteArrayLiteral("other\0")).value("network").toBool());
        config.entries.remove("available_features");
        QVERIFY(backendFeatures(config, {}).value("safety").toBool());
    }

    void layeredFeatureMasksAreExact() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const UiConfig::Paths paths{directory.filePath("defaults.conf"), directory.filePath("local.conf")};
        const auto write = [](const QString& path, const QByteArray& contents) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
        };
        QVERIFY(write(paths.defaults, "available_features=network\navailable_features.board=safety\n"
                                      "available_features.soc=none\n"));
        const auto resolve = [&]() {
            return backendFeatures(UiConfig::load(paths), QByteArrayLiteral("board\0soc\0"));
        };
        QVERIFY(write(paths.overlay, "available_features=network\n"));
        QVERIFY(resolve().value("safety").toBool());
        QVERIFY(write(paths.overlay, "!available_features\n"));
        QVERIFY(resolve().value("safety").toBool());
        QVERIFY(write(paths.overlay, "!available_features.board\n"));
        QVERIFY(!resolve().value("safety").toBool());
        QVERIFY(!resolve().value("network").toBool());
        QVERIFY(write(paths.overlay, "!available_features.board\n!available_features.soc\n"));
        QVERIFY(resolve().value("network").toBool());
        QVERIFY(!resolve().value("safety").toBool());
        QVERIFY(write(paths.overlay, "!available_features\n!available_features.board\n!available_features.soc\n"));
        QVERIFY(resolve().value("network").toBool());
        QVERIFY(resolve().value("safety").toBool());
        QVERIFY(write(paths.overlay, "available_features.board=\n"));
        QVERIFY(!resolve().value("network").toBool());
        QVERIFY(!resolve().value("safety").toBool());
    }

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
