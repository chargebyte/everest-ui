// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "../backend/api/modules/SafetyController.cpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace SafetyController;

class SafetyControllerAvailabilityTest final : public QObject {
    Q_OBJECT

    static bool writeFile(const QString& path, const QByteArray& bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    }

private slots:
    void parsesSelection_data() {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("all");
        QTest::addColumn<QList<int>>("indices");
        QTest::newRow("all") << QString("all") << true << QList<int>{};
        QTest::newRow("uppercase") << QString(" ALL ") << true << QList<int>{};
        QTest::newRow("none") << QString("none") << false << QList<int>{};
        QTest::newRow("empty") << QString() << false << QList<int>{};
        QTest::newRow("whitespace") << QString("  ") << false << QList<int>{};
        QTest::newRow("subset") << QString("1,3") << false << (QList<int>{0, 2});
        QTest::newRow("duplicates") << QString(" 3, 1, 3 ") << false << (QList<int>{0, 2});
        QTest::newRow("beyond-catalog") << QString("5") << false << QList<int>{4};
        for (const auto& value :
             {QString("0"), QString("-1"), QString("estop1"), QString("1,invalid"), QString("1,"), QString("1,,3"),
              QString("all,1"), QString("none,1"), QString("1.5"), QString("2147483648")}) {
            QTest::newRow(qPrintable(value)) << value << false << QList<int>{};
        }
    }
    void parsesSelection() {
        QFETCH(QString, value);
        QFETCH(bool, all);
        QFETCH(QList<int>, indices);
        const auto result = parseAvailableEstops(value);
        QCOMPARE(result.all, all);
        QCOMPARE(result.indices, QSet<int>(indices.begin(), indices.end()));
        QVERIFY(!result.allows(-1));
    }

    void platformPrecedence() {
        UiConfig::Snapshot config;
        const QString key = QStringLiteral("safety_controller_available_estops");
        const QByteArray compatible("board\0som\0soc\0", 14);
        const auto select = [&](const QByteArray& data = {}) { return availableEstopsForConfiguration(config, data); };
        QVERIFY(select(compatible).all);
        config.entries.insert(key, {QStringLiteral("3"), {}, 0});
        QVERIFY(select(compatible).allows(2));
        QVERIFY(select().allows(2)); // Missing device tree uses the global value.
        config.entries.insert(key + ".som", {QStringLiteral("2"), {}, 0});
        config.entries.insert(key + ".board", {QStringLiteral("1"), {}, 0});
        auto result = select(compatible);
        QVERIFY(result.allows(0));
        QVERIFY(!result.allows(1)); // First match replaces, rather than unions, lists.
        config.entries[key + ".board"].value.clear();
        QVERIFY(select(compatible).indices.isEmpty());
        QVERIFY(!select(compatible).all); // Explicit empty is not skipped.
        config.entries[key + ".board"].value = QStringLiteral("broken");
        QVERIFY(select(compatible).indices.isEmpty());
        config.entries.remove(key + ".board");
        QVERIFY(select(compatible).allows(1));
        config.entries.remove(key + ".som");
        QVERIFY(select(compatible).allows(2));
        config.entries.remove(key);
        QVERIFY(select(compatible).all);
    }

    void layeredOverridesAndExactMasks() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString key = QStringLiteral("safety_controller_available_estops");
        const UiConfig::Paths paths{dir.filePath("defaults.conf"), dir.filePath("backend.conf")};
        QVERIFY(writeFile(paths.defaults, (key + "=1,3\n" + key + ".board=1\n" + key + ".soc=2\n").toUtf8()));
        const QByteArray compatible("board\0soc\0", 10);
        const auto select = [&]() { return availableEstopsForConfiguration(UiConfig::load(paths), compatible); };
        QVERIFY(writeFile(paths.overlay, (key + "=3\n").toUtf8()));
        QVERIFY(select().allows(0)); // A local global assignment does not override a distribution platform key.
        QVERIFY(writeFile(paths.overlay, ("!" + key + "\n").toUtf8()));
        QVERIFY(select().allows(0)); // Global masks are not wildcard masks.
        QVERIFY(writeFile(paths.overlay, (key + "=3\n!" + key + ".board\n").toUtf8()));
        QVERIFY(select().allows(1));
        QVERIFY(writeFile(paths.overlay, (key + "=3\n!" + key + ".board\n!" + key + ".soc\n").toUtf8()));
        QVERIFY(select().allows(2));
        QVERIFY(writeFile(paths.overlay, ("!" + key + "\n!" + key + ".board\n!" + key + ".soc\n").toUtf8()));
        QVERIFY(select().all);
        QVERIFY(writeFile(paths.overlay, (key + ".board=\n").toUtf8()));
        QVERIFY(!select().all);
        QVERIFY(select().indices.isEmpty());
        QVERIFY(writeFile(paths.overlay, "# inherit again\n"));
        QVERIFY(select().allows(0));
        QVERIFY(UiConfig::load(paths).valid());
    }

    void filtersReadsWithoutRenumbering() {
        const QJsonObject yaml{{"pt1000s", QJsonArray{"disabled"}},
                               {"contactors", QJsonArray{}},
                               {"estops", QJsonArray{"active-low", "active-high", "disabled"}}};
        QJsonObject requested{{"pt1000_0", QJsonObject{}},  {"estops_0", QJsonObject{}}, {"estops_1", QJsonObject{}},
                              {"estops_2", QJsonObject{}},  {"estops_6", QJsonObject{}}, {"estops_bad", QJsonObject{}},
                              {"estops_00", QJsonObject{}}, {"estops_-1", QJsonObject{}}};
        const auto filtered = readRequestedParametersFromYaml(requested, yaml, {"all"}, parseAvailableEstops("1,3"));
        QCOMPARE(filtered.keys(), (QStringList{"estops_0", "estops_2", "pt1000_0"}));
        QCOMPARE(filtered.value("estops_2").toObject().value("enabled").toString(), QString("disabled"));
        const auto none = readRequestedParametersFromYaml(requested, yaml, {"all"}, parseAvailableEstops(""));
        QCOMPARE(none.keys(), QStringList{"pt1000_0"});
        const auto disabledGroup = readRequestedParametersFromYaml(requested, yaml, {"pt1000"}, AvailableEstops{});
        QCOMPARE(disabledGroup.keys(), QStringList{"pt1000_0"});
        auto longer = yaml;
        longer.insert("estops", QJsonArray{"disabled", "disabled", "disabled", "disabled", "active-high"});
        requested.insert("estops_4", QJsonObject{});
        const auto fifth = readRequestedParametersFromYaml(requested, longer, {"estops"}, parseAvailableEstops("5"));
        QCOMPARE(fifth.keys(), QStringList{"estops_4"});
    }

    void forgedWritesPreserveExcludedNativeEntries() {
        QTemporaryDir dir;
        const QString path = dir.filePath("controller.yaml");
        const QByteArray original = "version: '0003'\npt1000s: [disabled]\ncontactors: [disabled]\n"
                                    "estops: [active-low, '000123', active-high]\nopaque: {serial: '0012'}\n";
        QVERIFY(writeFile(path, original));
        const auto cached = loadYamlFile(path);
        QVERIFY(cached.success);
        const QJsonObject request{{"estops_0", QJsonObject{{"enabled", "disabled"}}},
                                  {"estops_1", QJsonObject{{"enabled", "disabled"}}},
                                  {"estops_2", QJsonObject{{"enabled", "active-low"}}}};
        const auto before = YAML::Load(original.constData());
        for (const auto& group : {QStringList{"estops"}, QStringList{"pt1000"}}) {
            for (const auto& value : {QString("1,3"), QString("none")}) {
                const auto modified =
                    prepareSafetyControllerYaml(path, request, cached.yamlRoot, group, parseAvailableEstops(value));
                QVERIFY(!modified.isEmpty());
                const auto after = YAML::Load(modified.constData());
                QCOMPARE(QString::fromStdString(after["estops"][1].Scalar()), QString("000123"));
                const bool edit = group.contains("estops") && value == "1,3";
                QCOMPARE(QString::fromStdString(after["estops"][0].Scalar()),
                         edit ? QString("disabled") : QString("active-low"));
                QCOMPARE(QString::fromStdString(after["estops"][2].Scalar()),
                         edit ? QString("active-low") : QString("active-high"));
                for (const auto* key : {"version", "pt1000s", "contactors", "opaque"}) {
                    QCOMPARE(QString::fromStdString(YAML::Dump(after[key])),
                             QString::fromStdString(YAML::Dump(before[key])));
                }
                const auto json =
                    updateRequestParametersInYaml(request, cached.yamlRoot, group, parseAvailableEstops(value));
                QCOMPARE(json.value("estops").toArray().at(1), cached.yamlRoot.value("estops").toArray().at(1));
            }
        }
    }
};

QTEST_GUILESS_MAIN(SafetyControllerAvailabilityTest)
#include "SafetyControllerAvailabilityTest.moc"
