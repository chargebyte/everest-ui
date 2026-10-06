// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/SafetyController.cpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace SafetyController;

namespace {
bool writeTextFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(contents) == contents.size();
}

QJsonObject bspModule(const QString& driver, const QString& serialPort = QString(),
                      const QString& resetGpioLineName = QString()) {
    QJsonObject config;
    if (!serialPort.isNull()) {
        config.insert(QStringLiteral("serial_port"), serialPort);
    }
    if (!resetGpioLineName.isNull()) {
        config.insert(QStringLiteral("reset_gpio_line_name"), resetGpioLineName);
    }
    return {{QStringLiteral("module"), driver}, {QStringLiteral("config_module"), config}};
}

QJsonObject evseModule(const QStringList& bspInstances) {
    QJsonArray connections;
    for (const QString& instance : bspInstances) {
        connections.append(QJsonObject{{QStringLiteral("module_id"), instance}});
    }
    return {{QStringLiteral("module"), QStringLiteral("EvseManager")},
            {QStringLiteral("connections"), QJsonObject{{QStringLiteral("bsp"), connections}}}};
}

QJsonObject activeConfig(const QJsonObject& modules) {
    return {{QStringLiteral("active_modules"), modules}};
}
} // namespace

class SafetyControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void resolvesConfiguredDevicesAndDeduplicatesByDeviceName() {
        const QJsonObject config = activeConfig({
            {QStringLiteral("evse_a"), evseModule({QStringLiteral("board_a"), QStringLiteral("board_shared")})},
            {QStringLiteral("evse_b"), evseModule({QStringLiteral("board_b")})},
            {QStringLiteral("board_a"), bspModule(QStringLiteral("BoardDriverA"), QStringLiteral("/dev/ttyUSB0"),
                                                  QStringLiteral("nSAFETY_RESET_INT"))},
            {QStringLiteral("board_b"), bspModule(QStringLiteral("BoardDriverB"), QStringLiteral("ttyUSB1"),
                                                  QStringLiteral("nSAFETY2_RESET_INT"))},
            {QStringLiteral("board_shared"),
             bspModule(QStringLiteral("BoardDriverShared"), QStringLiteral("/dev/ttyUSB0"),
                       QStringLiteral("nSAFETY3_RESET_INT"))},
        });

        const SafetyControllerDeviceResolution result =
            resolveSafetyControllerDevices(config, QStringLiteral("/no-manifests"));

        QCOMPARE(result.devices.size(), 2);
        QCOMPARE(result.devices.at(0).name, QStringLiteral("ttyUSB0"));
        QCOMPARE(result.devices.at(0).resetGpioLineName, QStringLiteral("nSAFETY_RESET_INT"));
        QCOMPARE(result.devices.at(0).bootModeGpioLineName, QStringLiteral("SAFETY_BOOTMODE_SET"));
        QCOMPARE(result.devices.at(0).bspInstance, QStringLiteral("board_a"));
        QCOMPARE(result.devices.at(0).driverModule, QStringLiteral("BoardDriverA"));
        QCOMPARE(result.devices.at(0).yamlPath, QStringLiteral("/run/ra-utils/ttyUSB0.yaml"));
        QCOMPARE(result.devices.at(1).name, QStringLiteral("ttyUSB1"));
        QCOMPARE(result.devices.at(1).resetGpioLineName, QStringLiteral("nSAFETY2_RESET_INT"));
        QCOMPARE(result.devices.at(1).bootModeGpioLineName, QStringLiteral("SAFETY2_BOOTMODE_SET"));
        QVERIFY(result.errors.isEmpty());
    }

    void resolvesMissingSerialPortFromDriverManifestDefault() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString moduleDirectory = directory.filePath(QStringLiteral("BoardDriver"));
        QVERIFY(QDir().mkpath(moduleDirectory));
        QVERIFY(writeTextFile(QDir(moduleDirectory).filePath(QStringLiteral("manifest.yaml")),
                              "config:\n  serial_port:\n    type: string\n    default: "
                              "/dev/ttyRA0\n  reset_gpio_line_name:\n    type: string\n    default: "
                              "nSAFETY4_RESET_INT\n"));
        const QJsonObject config = activeConfig({
            {QStringLiteral("evse"), evseModule({QStringLiteral("board")})},
            {QStringLiteral("board"), bspModule(QStringLiteral("BoardDriver"))},
        });

        const SafetyControllerDeviceResolution result = resolveSafetyControllerDevices(config, directory.path());

        QCOMPARE(result.devices.size(), 1);
        QCOMPARE(result.devices.first().name, QStringLiteral("ttyRA0"));
        QCOMPARE(result.devices.first().resetGpioLineName, QStringLiteral("nSAFETY4_RESET_INT"));
        QCOMPARE(result.devices.first().bootModeGpioLineName, QStringLiteral("SAFETY4_BOOTMODE_SET"));
        QVERIFY(result.errors.isEmpty());
    }

    void configuredResetGpioLineOverridesManifestDefault() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString moduleDirectory = directory.filePath(QStringLiteral("BoardDriver"));
        QVERIFY(QDir().mkpath(moduleDirectory));
        QVERIFY(writeTextFile(QDir(moduleDirectory).filePath(QStringLiteral("manifest.yaml")),
                              "config:\n  serial_port:\n    default: /dev/ttyRA0\n"
                              "  reset_gpio_line_name:\n    default: nSAFETY7_RESET_INT\n"));
        const QJsonObject config = activeConfig({
            {QStringLiteral("evse"), evseModule({QStringLiteral("board")})},
            {QStringLiteral("board"),
             bspModule(QStringLiteral("BoardDriver"), QString(), QStringLiteral(" nSAFETY9_RESET_INT "))},
        });

        const SafetyControllerDeviceResolution result = resolveSafetyControllerDevices(config, directory.path());

        QCOMPARE(result.devices.size(), 1);
        QCOMPARE(result.devices.first().resetGpioLineName, QStringLiteral("nSAFETY9_RESET_INT"));
        QCOMPARE(result.devices.first().bootModeGpioLineName, QStringLiteral("SAFETY9_BOOTMODE_SET"));
        QVERIFY(result.errors.isEmpty());
    }

    void reportsMissingResetGpioLineDefault() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString moduleDirectory = directory.filePath(QStringLiteral("BoardDriver"));
        QVERIFY(QDir().mkpath(moduleDirectory));
        QVERIFY(writeTextFile(QDir(moduleDirectory).filePath(QStringLiteral("manifest.yaml")),
                              "config:\n  serial_port:\n    default: /dev/ttyRA0\n"
                              "  reset_gpio_line_name:\n    default: \"\"\n"));
        const QJsonObject config = activeConfig({
            {QStringLiteral("evse"), evseModule({QStringLiteral("board")})},
            {QStringLiteral("board"), bspModule(QStringLiteral("BoardDriver"))},
        });

        const SafetyControllerDeviceResolution result = resolveSafetyControllerDevices(config, directory.path());

        QVERIFY(result.devices.isEmpty());
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(QStringLiteral("reset_gpio_line_name has no default")));
    }

    void reportsUnresolvedManifestOrUnsafeDeviceName() {
        const QJsonObject missingManifestConfig = activeConfig({
            {QStringLiteral("evse"), evseModule({QStringLiteral("board")})},
            {QStringLiteral("board"), bspModule(QStringLiteral("MissingDriver"))},
        });
        const SafetyControllerDeviceResolution missingManifest =
            resolveSafetyControllerDevices(missingManifestConfig, QStringLiteral("/no-manifests"));
        QVERIFY(missingManifest.devices.isEmpty());
        QCOMPARE(missingManifest.errors.size(), 1);

        const QJsonObject unsafeNameConfig = activeConfig({
            {QStringLiteral("evse"), evseModule({QStringLiteral("board")})},
            {QStringLiteral("board"), bspModule(QStringLiteral("BoardDriver"), QStringLiteral("/dev/../secret"),
                                                QStringLiteral("nSAFETY_RESET_INT"))},
        });
        const SafetyControllerDeviceResolution unsafeName =
            resolveSafetyControllerDevices(unsafeNameConfig, QStringLiteral("/no-manifests"));
        QVERIFY(unsafeName.devices.isEmpty());
        QVERIFY(unsafeName.errors.first().contains(QStringLiteral("invalid serial_port")));
    }

    void raUpdateFlashCommandSelectsResolvedDevice() {
        QCOMPARE(raDataFlashCommand(QStringLiteral("ttyUSB0"), QStringLiteral("nSAFETY_RESET_INT"),
                                    QStringLiteral("SAFETY_BOOTMODE_SET"), QStringLiteral("/tmp/safety.bin")),
                 QStringLiteral("ra-update -a data -d /dev/ttyUSB0 -r \"nSAFETY_RESET_INT\" "
                                "-m \"SAFETY_BOOTMODE_SET\" "
                                "flash /tmp/safety.bin"));
    }

    void raUpdateFlashCommandKeepsResetLineAsOneArgument() {
        QCOMPARE(raDataFlashCommand(QStringLiteral("ttyUSB0"), QStringLiteral("reset line \"A\""),
                                    QStringLiteral("SAFETY_BOOTMODE_SET"), QStringLiteral("/tmp/safety.bin")),
                 QStringLiteral("ra-update -a data -d /dev/ttyUSB0 -r \"reset line \\\"A\\\"\" "
                                "-m \"SAFETY_BOOTMODE_SET\" "
                                "flash /tmp/safety.bin"));
    }

    void derivesBootModeLineAcrossExpectedSuffixShapes() {
        const QList<QPair<QString, QString>> examples{
            {QStringLiteral("nSAFETY_RESET_INT"), QStringLiteral("SAFETY_BOOTMODE_SET")},
            {QStringLiteral("nSAFETY2_RESET_INT"), QStringLiteral("SAFETY2_BOOTMODE_SET")},
            {QStringLiteral("prefix_nSAFETY123_RESET_INT"), QStringLiteral("SAFETY123_BOOTMODE_SET")},
            {QStringLiteral("prefix_SAFETYaBc_RESET_INT"), QStringLiteral("SAFETYaBc_BOOTMODE_SET")},
            {QStringLiteral("prefix_SAFETY_A_RESET_INT"), QStringLiteral("SAFETY_A_BOOTMODE_SET")},
        };

        for (const auto& example : examples) {
            QString bootModeLine;
            QVERIFY(deriveBootModeGpioLineName(example.first, bootModeLine));
            QCOMPARE(bootModeLine, example.second);
        }
    }

    void rejectsResetLineWithoutExpectedSafetyPattern() {
        QString bootModeLine;
        QVERIFY(!deriveBootModeGpioLineName(QStringLiteral("nRESET_INT"), bootModeLine));

        const QJsonObject config = activeConfig({
            {QStringLiteral("evse"), evseModule({QStringLiteral("board")})},
            {QStringLiteral("board"),
             bspModule(QStringLiteral("BoardDriver"), QStringLiteral("/dev/ttyRA0"), QStringLiteral("nRESET_INT"))},
        });
        const SafetyControllerDeviceResolution result =
            resolveSafetyControllerDevices(config, QStringLiteral("/no-manifests"));

        QVERIFY(result.devices.isEmpty());
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(QStringLiteral("unsupported reset_gpio_line_name")));
    }

    void readsCachedYamlWithoutRpcAndReportsReadOnly() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString yamlPath = directory.filePath(QStringLiteral("ttyUSB0.yaml"));
        QVERIFY(writeTextFile(yamlPath, "version: 1\npt1000s:\n  - abort-temperature: 90 °C\n"
                                        "    resistance-offset: 0 Ω\ncontactors: []\nestops: [enabled]\n"));
        const SafetyControllerDevice device{
            QStringLiteral("ttyUSB0"), QStringLiteral("nSAFETY_RESET_INT"), QStringLiteral("SAFETY_BOOTMODE_SET"),
            QStringLiteral("board"),   QStringLiteral("BoardDriver"),       yamlPath};
        const QJsonObject request{
            {QStringLiteral("pt1000_0"), QJsonObject{{QStringLiteral("abort-temperature"), QString()},
                                                     {QStringLiteral("resistance-offset"), QString()},
                                                     {QStringLiteral("overtemperature-protection"), false}}}};

        const QJsonObject result = safetyControllerDeviceToJson(device, request, false);

        QVERIFY(result.value(QStringLiteral("available")).toBool());
        QVERIFY(!result.value(QStringLiteral("writable")).toBool());
        QVERIFY(result.value(QStringLiteral("message")).toString().contains(QStringLiteral("read-only")));
        QCOMPARE(result.value(QStringLiteral("settings"))
                     .toObject()
                     .value(QStringLiteral("pt1000_0"))
                     .toObject()
                     .value(QStringLiteral("abort-temperature"))
                     .toString(),
                 QStringLiteral("90"));
    }

    void missingCachedYamlDisablesControllerPane() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const SafetyControllerDevice device{QStringLiteral("ttyUSB9"),
                                            QStringLiteral("nSAFETY_RESET_INT"),
                                            QStringLiteral("SAFETY_BOOTMODE_SET"),
                                            QStringLiteral("board"),
                                            QStringLiteral("BoardDriver"),
                                            directory.filePath(QStringLiteral("missing-cache.yaml"))};

        const QJsonObject result = safetyControllerDeviceToJson(device, {}, true);

        QVERIFY(!result.value(QStringLiteral("available")).toBool());
        QVERIFY(!result.value(QStringLiteral("writable")).toBool());
        QVERIFY(result.value(QStringLiteral("message")).toString().contains(device.yamlPath));
    }

    void writesYamlAtomicallyInRaUtilsFormat() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString yamlPath = directory.filePath(QStringLiteral("controller.yaml"));
        const QJsonObject yamlRoot{
            {QStringLiteral("version"), 1},
            {QStringLiteral("pt1000s"),
             QJsonArray{QJsonObject{{QStringLiteral("abort-temperature"), QStringLiteral("90 \u00b0C")},
                                    {QStringLiteral("resistance-offset"), QStringLiteral("0 \u03a9")}}}},
            {QStringLiteral("contactors"), QJsonArray{QStringLiteral("disabled")}},
            {QStringLiteral("estops"), QJsonArray{QStringLiteral("enabled")}},
        };

        QVERIFY(writeSafetyControllerYamlFile(yamlPath, yamlRoot));
        const YamlLoadResult result = loadYamlFile(yamlPath);
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.yamlRoot.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(result.yamlRoot.value(QStringLiteral("estops")).toArray().at(0).toString(), QStringLiteral("enabled"));
    }
};

QTEST_GUILESS_MAIN(SafetyControllerTest)
#include "SafetyControllerTest.moc"
