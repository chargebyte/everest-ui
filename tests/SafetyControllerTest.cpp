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

        const EverestErrorPresentResult result =
            EverestServiceControl::monitorEverestErrorPresent(&rpc, QList<int>{1});

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
