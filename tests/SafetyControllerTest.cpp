// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/SafetyController.cpp"

#include <QElapsedTimer>
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

QJsonObject evseModule(const QStringList& bspInstances, int evseIndex = 0) {
    QJsonArray connections;
    for (const QString& instance : bspInstances) {
        connections.append(QJsonObject{{QStringLiteral("module_id"), instance}});
    }
    QJsonObject module{{QStringLiteral("module"), QStringLiteral("EvseManager")},
                       {QStringLiteral("connections"), QJsonObject{{QStringLiteral("bsp"), connections}}}};
    if (evseIndex > 0) {
        module.insert(QStringLiteral("mapping"),
                      QJsonObject{{QStringLiteral("module"), QJsonObject{{QStringLiteral("evse"), evseIndex}}}});
    }
    return module;
}

QJsonObject activeConfig(const QJsonObject& modules) {
    return {{QStringLiteral("active_modules"), modules}};
}

class FakeRpcApiClient final : public RpcApiClient {
public:
    QHash<int, QString> states;
    QHash<int, bool> errorsPresent;
    QList<int> stateQueries;
    QList<int> errorQueries;
    QHash<int, int> transientErrorFailures;
    int failingStateIndex = -1;
    int failingErrorIndex = -1;
    bool ready = true;
    quint64 generation = 2;
    quint64 disconnects = 2;
    QString transientError = QStringLiteral("rpc_api_not_connected");
    QHash<int, int> statusDelayMs;
    QHash<int, QString> permanentErrors;
    QList<int> statusBudgets;

    bool isReady() const override {
        return ready;
    }
    quint64 handshakeGeneration() const override {
        return generation;
    }
    quint64 disconnectionGeneration() const override {
        return disconnects;
    }

    RpcApiEvseStateResult getEvseState(int evseIndex) override {
        stateQueries.append(evseIndex);
        if (evseIndex == failingStateIndex || !states.contains(evseIndex)) {
            return {.success = false, .state = QString(), .error = QStringLiteral("rpc_status_unavailable")};
        }
        return {.success = true, .state = states.value(evseIndex), .error = QString()};
    }

    RpcApiEvseErrorPresentResult getEvseErrorPresent(int evseIndex, int timeoutMs = 4000) override {
        errorQueries.append(evseIndex);
        statusBudgets.append(timeoutMs);
        const int delay = statusDelayMs.value(evseIndex);
        if (delay > 0) {
            QEventLoop loop;
            QTimer::singleShot(qMin(delay, timeoutMs), &loop, &QEventLoop::quit);
            loop.exec();
            if (delay >= timeoutMs) {
                return {.success = false, .errorPresent = false, .error = QStringLiteral("rpc_api_no_response")};
            }
        }
        if (permanentErrors.contains(evseIndex)) {
            return {.success = false, .errorPresent = false, .error = permanentErrors.value(evseIndex)};
        }
        if (transientErrorFailures.value(evseIndex) > 0) {
            transientErrorFailures[evseIndex] -= 1;
            return {.success = false, .errorPresent = false, .error = transientError};
        }
        if (evseIndex == failingErrorIndex || !errorsPresent.contains(evseIndex)) {
            return {.success = false, .errorPresent = false, .error = QStringLiteral("rpc_status_unavailable")};
        }
        return {.success = true, .errorPresent = errorsPresent.value(evseIndex), .error = QString()};
    }
};

class ScopedProcessEnvironment final {
public:
    ScopedProcessEnvironment(const QString& path, RpcApiClient* rpc) :
        m_hadPath(qEnvironmentVariableIsSet("PATH")), m_previousPath(qgetenv("PATH")), m_previousRpc(g_rpcApiClient) {
        qputenv("PATH", path.toLocal8Bit());
        setRpcApiClient(rpc);
    }

    ~ScopedProcessEnvironment() {
        if (m_hadPath) {
            qputenv("PATH", m_previousPath);
        } else {
            qunsetenv("PATH");
        }
        setRpcApiClient(m_previousRpc);
    }

private:
    bool m_hadPath;
    QByteArray m_previousPath;
    RpcApiClient* m_previousRpc;
};
} // namespace

class SafetyControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void readsDisabledPt1000WithEmptyUnknownValuesAndKeepsSubmittedValues() {
        const QJsonObject request{
            {QStringLiteral("overtemperature-protection"), false},
            {QStringLiteral("abort-temperature"), 86.5},
            {QStringLiteral("resistance-offset"), -1.25},
        };
        const auto result = readPt1000ParametersFromYaml({}, QStringLiteral("disabled"));
        QVERIFY(!result.value(QStringLiteral("overtemperature-protection")).toBool());
        QVERIFY(result.contains(QStringLiteral("abort-temperature")));
        QVERIFY(result.contains(QStringLiteral("resistance-offset")));
        QCOMPARE(result.value(QStringLiteral("abort-temperature")).toString(), QString());
        QCOMPARE(result.value(QStringLiteral("resistance-offset")).toString(), QString());
        QCOMPARE(updatePt1000ParametersInYaml(request).toString(), QStringLiteral("disabled"));
        QCOMPARE(request.value(QStringLiteral("abort-temperature")).toDouble(), 86.5);
        QCOMPARE(request.value(QStringLiteral("resistance-offset")).toDouble(), -1.25);

        QJsonObject enabled = request;
        enabled.insert(QStringLiteral("overtemperature-protection"), true);
        const QJsonObject yaml = updatePt1000ParametersInYaml(enabled).toObject();
        const auto roundTrip = readPt1000ParametersFromYaml({}, yaml);
        QVERIFY(roundTrip.value(QStringLiteral("overtemperature-protection")).toBool());
        bool temperatureOk = false;
        const double temperature =
            roundTrip.value(QStringLiteral("abort-temperature")).toString().toDouble(&temperatureOk);
        QVERIFY(temperatureOk);
        QCOMPARE(temperature, 86.5);
        bool offsetOk = false;
        const double offset = roundTrip.value(QStringLiteral("resistance-offset")).toString().toDouble(&offsetOk);
        QVERIFY(offsetOk);
        QCOMPARE(offset, -1.25);

        enabled.insert(QStringLiteral("abort-temperature"), 0);
        enabled.insert(QStringLiteral("resistance-offset"), 0);
        const auto zeroRoundTrip = readPt1000ParametersFromYaml({}, updatePt1000ParametersInYaml(enabled));
        for (const QString& field : {QStringLiteral("abort-temperature"), QStringLiteral("resistance-offset")}) {
            const QString text = zeroRoundTrip.value(field).toString();
            QVERIFY(!text.isEmpty());
            bool validNumber = false;
            const double number = text.toDouble(&validNumber);
            QVERIFY(validNumber);
            QCOMPARE(number, 0.0);
        }
    }

    void validatesEnabledPt1000NumericValues_data() {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined) << false;
        QTest::newRow("null") << QJsonValue(QJsonValue::Null) << false;
        QTest::newRow("empty") << QJsonValue(QString()) << false;
        QTest::newRow("whitespace") << QJsonValue(QStringLiteral("  ")) << false;
        QTest::newRow("boolean") << QJsonValue(true) << false;
        QTest::newRow("array") << QJsonValue(QJsonArray{}) << false;
        QTest::newRow("object") << QJsonValue(QJsonObject{}) << false;
        QTest::newRow("trailing-garbage") << QJsonValue(QStringLiteral("86.5abc")) << false;
        QTest::newRow("nan") << QJsonValue(QStringLiteral("NaN")) << false;
        QTest::newRow("infinity") << QJsonValue(QStringLiteral("Infinity")) << false;
        QTest::newRow("negative-infinity") << QJsonValue(QStringLiteral("-inf")) << false;
        QTest::newRow("overflow") << QJsonValue(QStringLiteral("1e309")) << false;
        QTest::newRow("numeric-zero") << QJsonValue(0) << true;
        QTest::newRow("string-zero") << QJsonValue(QStringLiteral("0")) << true;
        QTest::newRow("numeric-value") << QJsonValue(86.5) << true;
        QTest::newRow("negative-value") << QJsonValue(-1.25) << true;
        QTest::newRow("string-value") << QJsonValue(QStringLiteral("86.500000")) << true;
        QTest::newRow("exponent") << QJsonValue(QStringLiteral("8.65e1")) << true;
    }

    void validatesEnabledPt1000NumericValues() {
        QFETCH(QJsonValue, value);
        QFETCH(bool, accepted);
        const QStringList available{QStringLiteral("pt1000")};
        for (const QString& field : {QStringLiteral("abort-temperature"), QStringLiteral("resistance-offset")}) {
            QJsonObject block{{QStringLiteral("overtemperature-protection"), true},
                              {QStringLiteral("abort-temperature"), 0},
                              {QStringLiteral("resistance-offset"), 0}};
            if (value.isUndefined()) {
                block.remove(field);
            } else {
                block.insert(field, value);
            }
            QJsonObject parameters{{QStringLiteral("pt1000_0"), block}};
            QCOMPARE(validatePt1000Settings(parameters, available), accepted);
            QVERIFY(validatePt1000Settings(parameters, {QStringLiteral("estops")}));
            block.insert(QStringLiteral("overtemperature-protection"), false);
            parameters.insert(QStringLiteral("pt1000_0"), block);
            QVERIFY(validatePt1000Settings(parameters, available));
            QCOMPARE(updatePt1000ParametersInYaml(block).toString(), QStringLiteral("disabled"));
        }
    }

    void rejectsInvalidPt1000WriteBeforeStartingAnOperation() {
        if (!isSafetyControllerSettingAvailable(loadAvailableSafetyControllerSettings(), QStringLiteral("pt1000"))) {
            QSKIP("The installed backend configuration does not permit PT1000 editing");
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        FakeRpcApiClient rpc;
        ScopedProcessEnvironment environment(directory.path(), &rpc);
        ModuleRequest request{};
        request.parameters = {
            {QStringLiteral("device_name"), QStringLiteral("ttyUSB0")},
            {QStringLiteral("pt1000_0"), QJsonObject{{QStringLiteral("overtemperature-protection"), true}}}};
        QObject owner;
        int completions = 0;
        const auto response = startWriteRequest(request, &owner, [&](const ModuleResponse&) { ++completions; });
        QVERIFY(response.final);
        QVERIFY(!response.success);
        QCOMPARE(response.parameters.value(QStringLiteral("error")).toString(),
                 QStringLiteral("safety_controller_pt1000_invalid_settings"));
        QCOMPARE(completions, 0);
        QVERIFY(owner.children().isEmpty());
        QVERIFY(rpc.stateQueries.isEmpty());
        QVERIFY(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }

    void roundTripsAllEstopPolarities() {
        for (const auto& value :
             {QStringLiteral("active-low"), QStringLiteral("active-high"), QStringLiteral("disabled")}) {
            const auto result = readEstopParametersFromYaml({}, value);
            QCOMPARE(result.value(QStringLiteral("enabled")).toString(), value);
            QCOMPARE(updateEstopParametersInYaml(result).toString(), value);
        }
    }

    void roundTripsAllContactorTypes() {
        for (const auto& type :
             {QStringLiteral("disabled"), QStringLiteral("without-feedback"),
              QStringLiteral("with-feedback-normally-open"), QStringLiteral("with-feedback-normally-closed")}) {
            const QJsonObject request{{QStringLiteral("type"), type},
                                      {QStringLiteral("close-time"), 100},
                                      {QStringLiteral("open-time"), 200}};
            const auto yaml = updateContactorParametersInYaml(request);
            const auto result = readContactorParametersFromYaml({}, yaml);
            QCOMPARE(result.value(QStringLiteral("type")).toString(), type);
            if (type != QStringLiteral("disabled")) {
                bool closeTimeOk = false;
                const double closeTime = result.value(QStringLiteral("close-time")).toString().toDouble(&closeTimeOk);
                QVERIFY(closeTimeOk);
                QCOMPARE(closeTime, 100.0);
                bool openTimeOk = false;
                const double openTime = result.value(QStringLiteral("open-time")).toString().toDouble(&openTimeOk);
                QVERIFY(openTimeOk);
                QCOMPARE(openTime, 200.0);
            }
        }
    }

    void preservesCompleteYamlWhenEditingAllowedEntries_data() {
        QTest::addColumn<QByteArray>("version");
        QTest::newRow("version-1") << QByteArray("1");
        QTest::newRow("version-3") << QByteArray("3");
        QTest::newRow("quoted-scalar") << QByteArray("'0003'");
    }

    void preservesCompleteYamlWhenEditingAllowedEntries() {
        QFETCH(QByteArray, version);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("controller.yaml"));
        const QByteArray original =
            "version: " + version +
            "\npt1000s:\n  - abort-temperature: 90 C\n    resistance-offset: 0 Ohm\n"
            "    extension: {keep: '0012'}\ncontactors: [{type: normally-open, close-time: 100 ms, open-time: 200 "
            "ms}]\n"
            "estops: [enabled]\nimd: active-low\nrcm: {test: {polarity: active-high, delay: 50 ms}}\n"
            "inlet: {type: with-feedback, extension: [one, two]}\nopaque: {serial: '000123', large: "
            "'9007199254740993'}\n";
        QVERIFY(writeTextFile(path, original));
        const auto cached = loadYamlFile(path);
        QVERIFY(cached.success);
        const QJsonObject settings{
            {QStringLiteral("pt1000_0"), QJsonObject{{QStringLiteral("overtemperature-protection"), true},
                                                     {QStringLiteral("abort-temperature"), 95},
                                                     {QStringLiteral("resistance-offset"), 2}}},
            {QStringLiteral("pt1000_invalid"), QJsonObject{{QStringLiteral("overtemperature-protection"), false}}},
            {QStringLiteral("contactors_0"), QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}}},
        };
        const QByteArray changed = prepareSafetyControllerYaml(path, settings, cached.yamlRoot,
                                                               {QStringLiteral("pt1000"), QStringLiteral("estops")});
        QVERIFY(!changed.isEmpty());
        const YAML::Node before = YAML::Load(original.constData());
        const YAML::Node after = YAML::Load(changed.constData());
        for (const char* key : {"version", "contactors", "estops", "imd", "rcm", "inlet", "opaque"}) {
            QCOMPARE(QString::fromStdString(YAML::Dump(after[key])), QString::fromStdString(YAML::Dump(before[key])));
        }
        QCOMPARE(QString::fromStdString(after["pt1000s"][0]["extension"]["keep"].as<std::string>()),
                 QStringLiteral("0012"));
        QVERIFY(QString::fromStdString(after["pt1000s"][0]["abort-temperature"].as<std::string>())
                    .startsWith(QStringLiteral("95")));
    }

    void binaryCreationUsesCachedVersion_data() {
        QTest::addColumn<int>("version");
        QTest::addColumn<int>("createExitCode");
        QTest::newRow("version-1") << 1 << 0;
        QTest::newRow("version-3") << 3 << 0;
        QTest::newRow("conversion-failed") << 3 << 2;
    }

    void binaryCreationUsesCachedVersion() {
        QFETCH(int, version);
        QFETCH(int, createExitCode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SafetyControllerDevice device;
        device.name = QStringLiteral("ttyUSB9");
        device.yamlPath = directory.filePath(QStringLiteral("controller.yaml"));
        device.resetGpioLineName = QStringLiteral("nSAFETY2_RESET_INT");
        device.bootModeGpioLineName = QStringLiteral("SAFETY2_BOOTMODE_SET");
        device.evseIndices = {2};
        const QByteArray original = "version: " + QByteArray::number(version) +
                                    "\npt1000s: [disabled]\ncontactors: [disabled]\nestops: [active-low]\n";
        QVERIFY(writeTextFile(device.yamlPath, original));
        const auto cached = loadYamlFile(device.yamlPath);
        QVERIFY(cached.success);
        const QJsonObject settings{
            {QStringLiteral("estops_0"), QJsonObject{{QStringLiteral("enabled"), QStringLiteral("disabled")}}}};
        const auto modified =
            prepareSafetyControllerYaml(device.yamlPath, settings, cached.yamlRoot, {QStringLiteral("estops")});
        QVERIFY(!modified.isEmpty());
        const auto yamlPath = directory.filePath(QStringLiteral("settings.yaml"));
        const auto binPath = directory.filePath(QStringLiteral("settings.bin"));
        const auto argumentsPath = directory.filePath(QStringLiteral("create-arguments"));
        const auto inputPath = directory.filePath(QStringLiteral("create-input.yaml"));
        const auto flashPath = directory.filePath(QStringLiteral("flash-arguments"));
        QVERIFY(writeSafetyControllerYamlFile(yamlPath, modified));
        const auto executable = [&](const QString& name, const QByteArray& body) {
            const auto path = directory.filePath(name);
            return writeTextFile(path, "#!/bin/sh\n" + body) &&
                   QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        };
        QByteArray createScript = "printf '%s\\n' \"$@\" >> " + quoteShellArgument(argumentsPath).toUtf8() + "\n";
        createScript += "[ \"$#\" -eq 5 ] && [ \"$1\" = --version-from-yaml ] && "
                        "[ \"$2\" = -i ] && [ \"$4\" = -o ] || exit 98\n";
        createScript += "/bin/cp \"$3\" " + quoteShellArgument(inputPath).toUtf8() + " || exit 99\n";
        if (createExitCode == 0) {
            createScript += "printf binary > \"$5\" || exit 99\n";
        }
        createScript += "exit " + QByteArray::number(createExitCode) + '\n';
        QVERIFY(executable(QStringLiteral("ra-pb-create"), createScript));
        QVERIFY(executable(QStringLiteral("ra-update"),
                           "printf '%s\\n' \"$@\" > " + quoteShellArgument(flashPath).toUtf8() + "\nexit 0\n"));
        ScopedProcessEnvironment environment(directory.path(), nullptr);
        SafetyControllerServiceActions actions;
        QStringList serviceCalls;
        actions.check = [&](const QList<int>&) {
            serviceCalls.append(QStringLiteral("check"));
            return EverestStateAllowedResult{true, {}, {}};
        };
        actions.stop = [&]() {
            serviceCalls.append(QStringLiteral("stop"));
            return EverestServiceControlResult{true, {}};
        };
        actions.restart = [&](const QList<int>&) {
            serviceCalls.append(QStringLiteral("restart"));
            return EverestServiceControlResult{true, {}};
        };
        actions.monitor = [&](const QList<int>&, bool) {
            serviceCalls.append(QStringLiteral("monitor"));
            return EverestErrorPresentResult{false, false, QStringLiteral("everest_error_present_not_detected")};
        };
        QObject owner;
        ModuleResponse result;
        int completions = 0;
        auto* operation = new SafetyControllerWriteOperation(
            {}, device, yamlPath, binPath, modified, &owner,
            [&](const ModuleResponse& response) {
                result = response;
                ++completions;
            },
            {}, actions);
        operation->start();
        QTRY_COMPARE(completions, 1);
        QFile arguments(argumentsPath);
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        QCOMPARE(arguments.readAll(),
                 "--version-from-yaml\n-i\n" + yamlPath.toUtf8() + "\n-o\n" + binPath.toUtf8() + '\n');
        const auto converted = YAML::LoadFile(inputPath.toStdString());
        QCOMPARE(QString::fromStdString(converted["version"].Scalar()), QString::number(version));
        QCOMPARE(QString::fromStdString(converted["estops"][0].as<std::string>()), QStringLiteral("disabled"));
        QFile cache(device.yamlPath);
        QVERIFY(cache.open(QIODevice::ReadOnly));
        if (createExitCode == 0) {
            QVERIFY(result.success);
            QCOMPARE(serviceCalls, (QStringList{"check", "stop", "restart", "monitor"}));
            QVERIFY(QFile::exists(flashPath));
            QCOMPARE(cache.readAll(), modified);
            const auto published = YAML::LoadFile(device.yamlPath.toStdString());
            QCOMPARE(QString::fromStdString(published["version"].Scalar()), QString::number(version));
        } else {
            QVERIFY(!result.success);
            QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(),
                     QStringLiteral("safety_controller_pb_create_failed"));
            QVERIFY(serviceCalls.isEmpty());
            QVERIFY(!QFile::exists(flashPath));
            QVERIFY(!QFile::exists(binPath));
            QCOMPARE(cache.readAll(), original);
        }
        QVERIFY(!QFile::exists(device.yamlPath + QStringLiteral(".invalid")));
    }

    void invalidatedCacheIsNotReadAfterInterruptedOperation() {
        QTemporaryDir directory;
        SafetyControllerDevice device;
        device.name = QStringLiteral("ttyUSB0");
        device.yamlPath = directory.filePath(QStringLiteral("controller.yaml"));
        QVERIFY(writeTextFile(device.yamlPath, "version: 3\npt1000s: []\ncontactors: []\nestops: []\n"));
        QVERIFY(invalidateSafetyControllerCache(device.yamlPath));
        QVERIFY(!QFile::exists(device.yamlPath));
        QVERIFY(QFile::exists(device.yamlPath + QStringLiteral(".invalid")));
        const auto result = safetyControllerDeviceToJson(device, {}, true);
        QVERIFY(!result.value(QStringLiteral("available")).toBool());
        QVERIFY(!result.value(QStringLiteral("writable")).toBool());
        QVERIFY(result.value(QStringLiteral("message")).toString().contains(QStringLiteral("readback")));
        // A later external producer can restore availability by publishing a fresh canonical file.
        QVERIFY(writeTextFile(device.yamlPath, "version: 3\npt1000s: []\ncontactors: []\nestops: []\n"));
        QVERIFY(safetyControllerDeviceToJson(device, {}, false).value(QStringLiteral("available")).toBool());
    }

    void flashCacheLifecycle_data() {
        QTest::addColumn<QString>("flashMode");
        QTest::addColumn<QString>("recoveryMode");
        QTest::addColumn<QString>("cacheState");
        QTest::addColumn<bool>("restartFails");
        QTest::newRow("success") << QStringLiteral("success") << QStringLiteral("valid") << QStringLiteral("") << false;
        QTest::newRow("invalidation-failed")
            << QStringLiteral("invalidation-failed") << QStringLiteral("valid") << QStringLiteral("unchanged") << false;
        QTest::newRow("failed-to-start") << QStringLiteral("missing") << QStringLiteral("valid")
                                         << QStringLiteral("unchanged") << false;
        QTest::newRow("nonzero-recovered")
            << QStringLiteral("nonzero") << QStringLiteral("valid") << QStringLiteral("refreshed") << false;
        QTest::newRow("timeout-recovered")
            << QStringLiteral("timeout") << QStringLiteral("valid") << QStringLiteral("refreshed") << false;
        QTest::newRow("crash-recovered") << QStringLiteral("crash") << QStringLiteral("valid")
                                         << QStringLiteral("refreshed") << false;
        QTest::newRow("dump-failed") << QStringLiteral("nonzero") << QStringLiteral("dump-failed")
                                     << QStringLiteral("unavailable") << false;
        QTest::newRow("dump-timeout") << QStringLiteral("nonzero") << QStringLiteral("dump-timeout")
                                      << QStringLiteral("unavailable") << false;
        QTest::newRow("decoder-crc-failure")
            << QStringLiteral("nonzero") << QStringLiteral("crc-failed") << QStringLiteral("unavailable") << false;
        QTest::newRow("malformed-dump") << QStringLiteral("nonzero") << QStringLiteral("malformed")
                                        << QStringLiteral("unavailable") << false;
        QTest::newRow("wrong-structure") << QStringLiteral("nonzero") << QStringLiteral("structure")
                                         << QStringLiteral("unavailable") << false;
        QTest::newRow("recovery-timeout")
            << QStringLiteral("nonzero") << QStringLiteral("timeout") << QStringLiteral("unavailable") << false;
        QTest::newRow("publication-failed")
            << QStringLiteral("publish-failed") << QStringLiteral("valid") << QStringLiteral("unavailable") << false;
        QTest::newRow("recovery-publication-failed")
            << QStringLiteral("nonzero") << QStringLiteral("publish-failed") << QStringLiteral("unavailable") << false;
        QTest::newRow("restart-failed-after-flash-failure")
            << QStringLiteral("nonzero") << QStringLiteral("valid") << QStringLiteral("refreshed") << true;
        QTest::newRow("restart-failed-after-success")
            << QStringLiteral("success") << QStringLiteral("valid") << QStringLiteral("") << true;
    }

    void flashCacheLifecycle() {
        QFETCH(QString, flashMode);
        QFETCH(QString, recoveryMode);
        QFETCH(QString, cacheState);
        QFETCH(bool, restartFails);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SafetyControllerDevice device;
        device.name = QStringLiteral("ttyUSB9");
        device.yamlPath = directory.filePath(QStringLiteral("controller.yaml"));
        device.resetGpioLineName = QStringLiteral("nSAFETY2_RESET_INT");
        device.bootModeGpioLineName = QStringLiteral("SAFETY2_BOOTMODE_SET");
        device.evseIndices = {2};
        const QByteArray original =
            "version: 3\npt1000s: [disabled]\ncontactors: [disabled]\nestops: [enabled]\nimd: active-low\n";
        const QByteArray modified =
            "version: 3\npt1000s: [disabled]\ncontactors: [disabled]\nestops: [disabled]\nimd: active-low\n";
        const QByteArray recovered =
            "version: 3\npt1000s: [disabled]\ncontactors: [disabled]\nestops: [enabled]\nimd: active-high\n";
        QVERIFY(writeTextFile(device.yamlPath, original));
        if (flashMode == QStringLiteral("invalidation-failed")) {
            QVERIFY(QDir().mkdir(device.yamlPath + QStringLiteral(".invalid")));
        }
        const QString otherCache = directory.filePath(QStringLiteral("other.yaml"));
        QVERIFY(writeTextFile(otherCache, original));
        const QString dumpPath = directory.filePath(QStringLiteral("dump.yaml"));
        QVERIFY(writeTextFile(dumpPath, recoveryMode == QStringLiteral("malformed")   ? QByteArray("[broken")
                                        : recoveryMode == QStringLiteral("structure") ? QByteArray("version: 3\n")
                                                                                      : recovered));
        const QString callsPath = directory.filePath(QStringLiteral("calls"));
        const auto executable = [&](const QString& name, const QByteArray& body) {
            const QString path = directory.filePath(name);
            return writeTextFile(path, "#!/bin/sh\n" + body) &&
                   QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        };
        QVERIFY(executable(QStringLiteral("ra-pb-create"), "exit 0\n"));
        const QByteArray cacheArg = quoteShellArgument(device.yamlPath).toUtf8();
        if (flashMode != QStringLiteral("missing")) {
            QByteArray script = "printf '%s\\n' \"$@\" >> " + quoteShellArgument(callsPath).toUtf8() + "\n";
            script += "[ ! -e " + cacheArg + " ] || exit 99\ncase \"$9\" in\nflash)\n";
            if (flashMode == QStringLiteral("timeout")) {
                script += "exec /bin/sleep 5\n";
            } else if (flashMode == QStringLiteral("crash")) {
                script += "kill -KILL $$\n";
            } else if (flashMode == QStringLiteral("publish-failed")) {
                script += "/bin/mkdir " + cacheArg + "\nexit 0\n";
            } else {
                script += flashMode == QStringLiteral("success") ? "exit 0\n" : "exit 7\n";
            }
            script += ";;\ndump-pb)\n";
            if (recoveryMode == QStringLiteral("dump-failed")) {
                script += "exit 8\n";
            } else if (recoveryMode == QStringLiteral("dump-timeout")) {
                script += "exec /bin/sleep 5\n";
            } else {
                script += "printf binary > \"${10}\"\nexit 0\n";
            }
            script += ";;\n*) exit 98;;\nesac\n";
            QVERIFY(executable(QStringLiteral("ra-update"), script));
        }
        QByteArray decoder;
        if (recoveryMode == QStringLiteral("timeout")) {
            decoder = "exec /bin/sleep 5\n";
        } else {
            decoder = "/bin/cat " + quoteShellArgument(dumpPath).toUtf8() + "\n";
            if (recoveryMode == QStringLiteral("publish-failed")) {
                decoder += "/bin/mkdir " + cacheArg + "\n";
            }
            decoder += recoveryMode == QStringLiteral("crc-failed") ? "exit 1\n" : "exit 0\n";
        }
        QVERIFY(executable(QStringLiteral("ra-pb-dump"), decoder));
        ScopedProcessEnvironment environment(directory.path(), nullptr);
        SafetyControllerServiceActions actions;
        bool stopped = false;
        int restarts = 0;
        bool orderCorrect = true;
        actions.check = [](const QList<int>&) { return EverestStateAllowedResult{true, {}, {}}; };
        actions.stop = [&]() {
            stopped = true;
            return EverestServiceControlResult{true, {}};
        };
        actions.restart = [&](const QList<int>& indices) {
            ++restarts;
            orderCorrect = stopped && indices == QList<int>{2};
            QFile cache(device.yamlPath);
            if (cacheState == QStringLiteral("unavailable")) {
                orderCorrect = orderCorrect && !QFileInfo(device.yamlPath).isFile();
            } else {
                orderCorrect = orderCorrect && cache.open(QIODevice::ReadOnly);
                const QByteArray expected = cacheState == QStringLiteral("unchanged")   ? original
                                            : cacheState == QStringLiteral("refreshed") ? recovered
                                                                                        : modified;
                orderCorrect = orderCorrect && cache.readAll() == expected;
            }
            return EverestServiceControlResult{!restartFails,
                                               restartFails ? QStringLiteral("rpc_api_no_response") : QString()};
        };
        actions.monitor = [](const QList<int>&, bool) {
            return EverestErrorPresentResult{
                .success = false, .errorPresent = false, .error = QStringLiteral("everest_error_present_not_detected")};
        };
        ConsoleConnector::ExecOptions options;
        options.syncTimeoutMs = 300;
        QObject owner;
        int completions = 0;
        ModuleResponse result;
        auto* operation = new SafetyControllerWriteOperation(
            {}, device, directory.filePath(QStringLiteral("settings.yaml")),
            directory.filePath(QStringLiteral("settings.bin")), modified, &owner,
            [&](const ModuleResponse& response) {
                result = response;
                ++completions;
            },
            options, actions);
        operation->start();
        QTRY_COMPARE(completions, 1);
        QCOMPARE(restarts, 1);
        QVERIFY(orderCorrect);
        QCOMPARE(result.parameters.value(QStringLiteral("cache_state")).toString(), cacheState);
        if (flashMode == QStringLiteral("success")) {
            QCOMPARE(result.success, !restartFails);
        } else {
            QVERIFY(!result.success);
            QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(),
                     flashMode == QStringLiteral("publish-failed")
                         ? QStringLiteral("safety_controller_yaml_publish_failed_after_flash")
                     : flashMode == QStringLiteral("invalidation-failed")
                         ? QStringLiteral("safety_controller_cache_invalidation_failed")
                         : QStringLiteral("safety_controller_flash_failed"));
            if (restartFails) {
                QCOMPARE(result.parameters.value(QStringLiteral("restart_error")).toString(),
                         QStringLiteral("rpc_api_no_response"));
            }
        }
        QFile other(otherCache);
        QVERIFY(other.open(QIODevice::ReadOnly));
        QCOMPARE(other.readAll(), original);
        QCOMPARE(QFile::exists(device.yamlPath + QStringLiteral(".invalid")),
                 cacheState == QStringLiteral("unavailable") || flashMode == QStringLiteral("invalidation-failed"));
        if (flashMode != QStringLiteral("missing") && flashMode != QStringLiteral("invalidation-failed")) {
            QFile calls(callsPath);
            QVERIFY(calls.open(QIODevice::ReadOnly));
            const QByteArray arguments = calls.readAll();
            QVERIFY(arguments.contains("-d\n/dev/ttyUSB9\n-r\nnSAFETY2_RESET_INT\n-m\nSAFETY2_BOOTMODE_SET\nflash\n"));
            const bool recoveryAttempted =
                flashMode != QStringLiteral("success") && flashMode != QStringLiteral("publish-failed");
            QCOMPARE(arguments.contains("dump-pb\n"), recoveryAttempted);
            if (recoveryAttempted) {
                QVERIFY(arguments.contains(
                    "-d\n/dev/ttyUSB9\n-r\nnSAFETY2_RESET_INT\n-m\nSAFETY2_BOOTMODE_SET\ndump-pb\n"));
            }
        } else {
            QVERIFY(!QFile::exists(callsPath));
        }
        QTest::qWait(350);
        QCOMPARE(completions, 1);
    }

    void delayedBinaryCreationReceivesExecutionBudget_data() {
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<int>("startTimeoutMs");
        QTest::newRow("success-advances-to-safety-check") << 0 << 2000;
        QTest::newRow("startup-budget-stops-after-launch") << 0 << 100;
        QTest::newRow("ordinary-process-failure") << 7 << 2000;
    }

    void delayedBinaryCreationReceivesExecutionBudget() {
        QFETCH(int, exitCode);
        QFETCH(int, startTimeoutMs);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString executable = directory.filePath(QStringLiteral("ra-pb-create"));
        QVERIFY(writeTextFile(executable,
                              QByteArray("#!/bin/sh\n/bin/sleep 0.15\nexit ") + QByteArray::number(exitCode) + '\n'));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        FakeRpcApiClient rpc;
        ScopedProcessEnvironment environment(directory.path(), &rpc);
        if (exitCode != 0) {
            QTest::ignoreMessage(
                QtWarningMsg, QRegularExpression(QStringLiteral("Safety Controller binary creation failed: .*"
                                                                "started=1; timed_out=0; normal_exit=1; exit_code=7; .*"
                                                                "timeout_ms=30000")));
        }
        int completions = 0;
        ModuleResponse result;
        QObject owner;
        ConsoleConnector::ExecOptions options;
        options.startTimeoutMs = startTimeoutMs;
        auto* operation = new SafetyControllerWriteOperation(
            {}, {}, directory.filePath(QStringLiteral("settings.yaml")),
            directory.filePath(QStringLiteral("settings.bin")), {}, &owner,
            [&](const ModuleResponse& response) {
                result = response;
                ++completions;
            },
            options);
        QElapsedTimer elapsed;
        elapsed.start();
        const ModuleResponse initial = operation->start();
        QVERIFY(!initial.final);
        QTRY_COMPARE(completions, 1);
        QVERIFY(elapsed.elapsed() >= 150);
        QVERIFY(result.final);
        QVERIFY(!result.success);
        QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(),
                 exitCode == 0 ? QStringLiteral("safety_controller_evse_mapping_unavailable")
                               : QStringLiteral("safety_controller_pb_create_failed"));
        QVERIFY(rpc.stateQueries.isEmpty());
        QTest::qWait(200);
        QCOMPARE(completions, 1);
    }

    void executionTimeoutKillsProcessAndCompletesOnce() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString executable = directory.filePath(QStringLiteral("ra-pb-create"));
        QVERIFY(writeTextFile(executable, "#!/bin/sh\nexec /bin/sleep 5\n"));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        ScopedProcessEnvironment environment(directory.path(), nullptr);
        ConsoleConnector::ExecOptions options;
        options.syncTimeoutMs = 150;
        QTest::ignoreMessage(
            QtWarningMsg, QRegularExpression(QStringLiteral("Safety Controller binary creation failed: .*"
                                                            "started=1; timed_out=1; normal_exit=0; .*"
                                                            "process_error='process_run_timeout'; .*timeout_ms=150")));
        int completions = 0;
        ModuleResponse result;
        QObject owner;
        auto* operation = new SafetyControllerWriteOperation(
            {}, {}, directory.filePath(QStringLiteral("settings.yaml")),
            directory.filePath(QStringLiteral("settings.bin")), {}, &owner,
            [&](const ModuleResponse& response) {
                result = response;
                ++completions;
            },
            options);
        QElapsedTimer elapsed;
        elapsed.start();
        operation->start();
        QTRY_COMPARE(completions, 1);
        QVERIFY(elapsed.elapsed() >= options.syncTimeoutMs);
        QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(),
                 QStringLiteral("safety_controller_pb_create_failed"));
        QTest::qWait(200);
        QCOMPARE(completions, 1);
    }

    void missingExecutableCompletesWithoutStaleTimeout() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ScopedProcessEnvironment environment(directory.path(), nullptr);
        ConsoleConnector::ExecOptions options;
        options.startTimeoutMs = 100;
        options.syncTimeoutMs = 150;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Safety Controller binary creation failed: .*"
                                                               "started=0; timed_out=0; .*timeout_ms=100")));
        int completions = 0;
        ModuleResponse result;
        QObject owner;
        auto* operation = new SafetyControllerWriteOperation(
            {}, {}, directory.filePath(QStringLiteral("settings.yaml")),
            directory.filePath(QStringLiteral("settings.bin")), {}, &owner,
            [&](const ModuleResponse& response) {
                result = response;
                ++completions;
            },
            options);
        operation->start();
        QTRY_COMPARE(completions, 1);
        QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(),
                 QStringLiteral("safety_controller_pb_create_failed"));
        QTest::qWait(250);
        QCOMPARE(completions, 1);
    }

    void resolvesConfiguredDevicesAndDeduplicatesByDeviceName() {
        const QJsonObject config = activeConfig({
            {QStringLiteral("evse_a"), evseModule({QStringLiteral("board_a"), QStringLiteral("board_shared")}, 1)},
            {QStringLiteral("evse_b"), evseModule({QStringLiteral("board_b"), QStringLiteral("board_shared")}, 2)},
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
        QCOMPARE(result.devices.at(0).evseManagerInstances,
                 QStringList({QStringLiteral("evse_a"), QStringLiteral("evse_b")}));
        QCOMPARE(result.devices.at(0).evseIndices, QList<int>({1, 2}));
        QVERIFY(result.devices.at(0).evseMappingResolved);
        QCOMPARE(result.devices.at(1).name, QStringLiteral("ttyUSB1"));
        QCOMPARE(result.devices.at(1).evseIndices, QList<int>({2}));
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
        QCOMPARE(result.devices.first().evseIndices, QList<int>({1}));
        QVERIFY(result.devices.first().evseMappingResolved);
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

    void refusesControllerWhenAnyAssociatedEvseIsNotAllowed() {
        FakeRpcApiClient rpc;
        rpc.states.insert(1, QStringLiteral("Unplugged"));
        rpc.states.insert(2, QStringLiteral("Charging"));

        const EverestStateAllowedResult result =
            EverestServiceControl::checkEverestStateAllowed(&rpc, QList<int>{1, 2}, {QStringLiteral("Unplugged")});

        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("everest_state_not_allowed"));
        QCOMPARE(result.state, QStringLiteral("Charging"));
        QCOMPARE(rpc.stateQueries, QList<int>({1, 2}));
    }

    void allowsControllerOnlyAfterEveryAssociatedEvseIsAllowed() {
        FakeRpcApiClient rpc;
        rpc.states.insert(1, QStringLiteral("Unplugged"));
        rpc.states.insert(2, QStringLiteral("Unplugged"));

        const EverestStateAllowedResult result =
            EverestServiceControl::checkEverestStateAllowed(&rpc, QList<int>{1, 2}, {QStringLiteral("Unplugged")});

        QVERIFY(result.success);
        QCOMPARE(rpc.stateQueries, QList<int>({1, 2}));
    }

    void refusesControllerWhenAssociatedEvseStatusCannotBeRead() {
        FakeRpcApiClient rpc;
        rpc.states.insert(1, QStringLiteral("Unplugged"));
        rpc.failingStateIndex = 2;

        const EverestStateAllowedResult result =
            EverestServiceControl::checkEverestStateAllowed(&rpc, QList<int>{1, 2}, {QStringLiteral("Unplugged")});

        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_status_unavailable"));
        QCOMPARE(rpc.stateQueries, QList<int>({1, 2}));
    }

    void monitorsErrorsForEveryAssociatedEvse() {
        FakeRpcApiClient rpc;
        rpc.errorsPresent.insert(1, false);
        rpc.errorsPresent.insert(2, true);

        const EverestErrorPresentResult result =
            EverestServiceControl::monitorEverestErrorPresent(&rpc, QList<int>{1, 2});

        QVERIFY(result.success);
        QVERIFY(result.errorPresent);
        QCOMPARE(rpc.errorQueries, QList<int>({1, 2}));
    }

    void readinessWaitsForHelloAndEveryEvse() {
        FakeRpcApiClient rpc;
        rpc.ready = false;
        rpc.generation = 1;
        rpc.disconnects = 1;
        rpc.errorsPresent = {{1, false}, {2, false}};
        QTimer::singleShot(30, &rpc, [&] {
            rpc.ready = true;
            rpc.generation = 2;
            rpc.disconnects = 2;
        });
        const auto result = EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 600, {1, 2});
        QVERIFY(result.success);
        QCOMPARE(rpc.errorQueries, QList<int>({1, 2}));
        QVERIFY(!result.errorPresent);
    }

    void readinessRetriesNoResponseAndKeepsInitialErrors() {
        FakeRpcApiClient rpc;
        rpc.transientError = QStringLiteral("rpc_api_no_response");
        rpc.transientErrorFailures.insert(2, 1);
        rpc.errorsPresent = {{1, true}, {2, false}};
        const auto result = EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 700, {1, 2});
        QVERIFY(result.success);
        QVERIFY(result.errorPresent);
        QCOMPARE(rpc.errorQueries, QList<int>({1, 2, 1, 2}));
        rpc.errorQueries.clear();
        const auto monitor = EverestServiceControl::monitorEverestErrorPresent(&rpc, {1, 2}, 100, result.errorPresent);
        QVERIFY(monitor.success);
        QVERIFY(monitor.errorPresent);
        QVERIFY(rpc.errorQueries.isEmpty());
    }

    void readinessDoesNotAcceptStaleHelloOrMissingDisconnect() {
        FakeRpcApiClient rpc;
        rpc.errorsPresent.insert(1, false);
        rpc.generation = 1;
        QVERIFY(!EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 40, {1}).success);
        rpc.generation = 2;
        rpc.disconnects = 1;
        QVERIFY(!EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 40, {1}).success);
        QVERIFY(rpc.errorQueries.isEmpty());
    }

    void readinessSharesDeadlineAcrossHelloAndEvses() {
        FakeRpcApiClient rpc;
        rpc.ready = false;
        rpc.errorsPresent = {{1, false}, {2, false}};
        rpc.statusDelayMs = {{1, 70}, {2, 100}};
        QTimer::singleShot(20, &rpc, [&] { rpc.ready = true; });
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 300, {1, 2});
        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_api_no_response"));
        QVERIFY(elapsed.elapsed() >= 300);
        QVERIFY(elapsed.elapsed() < 650);
        QCOMPARE(rpc.statusBudgets.size(), 2);
        QVERIFY(rpc.statusBudgets.at(0) <= 110);
        QVERIFY(rpc.statusBudgets.at(1) < rpc.statusBudgets.at(0));
    }

    void readinessFailsPermanentStatusErrorsWithoutRetrying() {
        FakeRpcApiClient rpc;
        rpc.permanentErrors.insert(2, QStringLiteral("rpc_api_error_present_missing"));
        rpc.errorsPresent.insert(1, false);
        const auto result = EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 600, {1, 2});
        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_api_error_present_missing"));
        QCOMPARE(rpc.errorQueries, QList<int>({1, 2}));
    }

    void helloOnlyReadinessDoesNotQueryEvses() {
        FakeRpcApiClient rpc;
        const auto result = EverestServiceControl::waitForRpcApiReady(&rpc, 1, 1, true, 100);
        QVERIFY(result.success);
        QVERIFY(rpc.errorQueries.isEmpty());
    }

    void monitoringRetriesNoResponseWithinObservationWindow() {
        FakeRpcApiClient rpc;
        rpc.transientError = QStringLiteral("rpc_api_no_response");
        rpc.transientErrorFailures.insert(1, 1);
        rpc.errorsPresent = {{1, false}, {2, false}};
        const auto result = EverestServiceControl::monitorEverestErrorPresent(&rpc, {1, 2}, 450);
        QCOMPARE(result.error, QString::fromLatin1(kInfoEverestErrorPresentNotDetected));
        QVERIFY(rpc.errorQueries.count(1) >= 2);
    }

    void monitoringRetainsUnresolvedErrorsForOtherEvses() {
        FakeRpcApiClient rpc;
        rpc.transientError = QStringLiteral("rpc_api_no_response");
        rpc.transientErrorFailures.insert(1, 100);
        rpc.errorsPresent = {{1, false}, {2, false}};
        const auto result = EverestServiceControl::monitorEverestErrorPresent(&rpc, {1, 2}, 80);
        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_api_no_response"));
        QCOMPARE(rpc.errorQueries, QList<int>({1, 2}));
    }

    void retriesTransientRpcDisconnectDuringErrorMonitoring() {
        FakeRpcApiClient rpc;
        rpc.transientErrorFailures.insert(1, 1);
        rpc.errorsPresent.insert(1, true);

        const EverestErrorPresentResult result = EverestServiceControl::monitorEverestErrorPresent(&rpc, QList<int>{1});

        QVERIFY(result.success);
        QVERIFY(result.errorPresent);
        QCOMPARE(rpc.errorQueries, QList<int>({1, 1}));
    }

    void failsPostRestartMonitoringWhenAnyAssociatedEvseCannotBeQueried() {
        FakeRpcApiClient rpc;
        rpc.errorsPresent.insert(1, false);
        rpc.failingErrorIndex = 2;

        const EverestErrorPresentResult result =
            EverestServiceControl::monitorEverestErrorPresent(&rpc, QList<int>{1, 2});

        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_status_unavailable"));
        QCOMPARE(rpc.errorQueries, QList<int>({1, 2}));
    }

    void marksMultiEvseControllerWithoutMappingsReadOnly() {
        const QJsonObject config = activeConfig({
            {QStringLiteral("evse_a"), evseModule({QStringLiteral("board_a")})},
            {QStringLiteral("evse_b"), evseModule({QStringLiteral("board_b")})},
            {QStringLiteral("board_a"),
             bspModule(QStringLiteral("BoardDriverA"), QStringLiteral("ttyUSB0"), QStringLiteral("nSAFETY_RESET_INT"))},
            {QStringLiteral("board_b"), bspModule(QStringLiteral("BoardDriverB"), QStringLiteral("ttyUSB1"),
                                                  QStringLiteral("nSAFETY2_RESET_INT"))},
        });

        const SafetyControllerDeviceResolution result =
            resolveSafetyControllerDevices(config, QStringLiteral("/no-manifests"));

        QCOMPARE(result.devices.size(), 2);
        QVERIFY(!result.devices.at(0).evseMappingResolved);
        QVERIFY(result.devices.at(0).evseIndices.isEmpty());
        QVERIFY(result.errors.join(QLatin1Char(';')).contains(QStringLiteral("mapping.module.evse")));
    }

    void doesNotMarkControllerWritableWhenEvseMappingIsMissing() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString yamlPath = directory.filePath(QStringLiteral("ttyUSB0.yaml"));
        QVERIFY(writeTextFile(yamlPath, "version: 1\npt1000s: []\n"));
        const SafetyControllerDevice device{QStringLiteral("ttyUSB0"),
                                            QStringLiteral("nSAFETY_RESET_INT"),
                                            QStringLiteral("SAFETY_BOOTMODE_SET"),
                                            QStringLiteral("board"),
                                            QStringLiteral("BoardDriver"),
                                            yamlPath,
                                            {QStringLiteral("evse_a"), QStringLiteral("evse_b")},
                                            {},
                                            false};

        const QJsonObject result = safetyControllerDeviceToJson(device, {}, true);

        QVERIFY(result.value(QStringLiteral("available")).toBool());
        QVERIFY(!result.value(QStringLiteral("writable")).toBool());
        QVERIFY(result.value(QStringLiteral("message")).toString().contains(QStringLiteral("mapping")));
    }

    void readsCachedYamlWithoutRpcAndReportsReadOnly() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString yamlPath = directory.filePath(QStringLiteral("ttyUSB0.yaml"));
        QVERIFY(writeTextFile(yamlPath, "version: 1\npt1000s:\n  - abort-temperature: 90 °C\n"
                                        "    resistance-offset: 0 Ω\ncontactors: []\nestops: [enabled]\n"));
        const SafetyControllerDevice device{QStringLiteral("ttyUSB0"),
                                            QStringLiteral("nSAFETY_RESET_INT"),
                                            QStringLiteral("SAFETY_BOOTMODE_SET"),
                                            QStringLiteral("board"),
                                            QStringLiteral("BoardDriver"),
                                            yamlPath,
                                            {QStringLiteral("evse")},
                                            {1},
                                            true};
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
