// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/EverestConfig.cpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class EverestConfigTest final : public QObject {
    Q_OBJECT

private slots:
    void restartPolicy_data() {
        QTest::addColumn<QString>("rpcError");
        QTest::addColumn<bool>("upload");
        QTest::addColumn<bool>("stopped");
        QTest::addColumn<bool>("expectRestart");
        QTest::addColumn<bool>("expectServiceQuery");
        QTest::newRow("normal-allowed-upload") << QString() << true << false << true << false;
        QTest::newRow("normal-allowed-edit") << QString() << false << false << true << false;
        for (const auto& error : {QString("rpc_api_not_connected"), QString("rpc_api_no_response"),
                                  QString("rpc_api_send_failed"), QString("rpc_api_client_unavailable")}) {
            QTest::newRow(qPrintable(error)) << error << true << true << true << true;
        }
        QTest::newRow("running-or-unverified") << QString("rpc_api_not_connected") << true << false << false << true;
        QTest::newRow("ordinary-edit-does-not-bypass")
            << QString("rpc_api_not_connected") << false << true << false << false;
        for (const auto& error :
             {QString("everest_state_not_allowed"), QString("rpc_api_state_missing"), QString("rpc_api_status_missing"),
              QString("rpc_api_request_failed"), QString("rpc_api_request_pending"),
              QString("everest_restart_allowed_states_missing")}) {
            QTest::newRow(qPrintable(error)) << error << true << true << false << false;
        }
    }
    void restartPolicy() {
        QFETCH(QString, rpcError);
        QFETCH(bool, upload);
        QFETCH(bool, stopped);
        QFETCH(bool, expectRestart);
        QFETCH(bool, expectServiceQuery);
        EverestConfig::EverestConfigServiceActions actions;
        int serviceQueries = 0;
        int restarts = 0;
        actions.checkState = [&]() {
            return EverestStateAllowedResult{rpcError.isEmpty(), QString("Charging"), rpcError};
        };
        actions.stoppedForRecovery = [&]() {
            ++serviceQueries;
            return stopped;
        };
        actions.restart = [&]() {
            ++restarts;
            return EverestServiceControlResult{true, {}};
        };
        ModuleResponse response;
        response.success = false;
        const auto result = EverestConfig::restartEverestStack(response, upload, actions);
        QCOMPARE(result.success, expectRestart);
        QCOMPARE(restarts, expectRestart ? 1 : 0);
        QCOMPARE(serviceQueries, expectServiceQuery ? 1 : 0);
        if (expectRestart)
            QVERIFY(result.parameters.isEmpty());
        else
            QVERIFY(result.parameters.contains(QStringLiteral("error")));
    }

    void stoppedRecoveryRetainsRestartAndReadinessErrors_data() {
        QTest::addColumn<QString>("error");
        QTest::newRow("restart-failed") << QString("everest_restart_failed");
        QTest::newRow("readiness-disconnected") << QString("rpc_api_not_connected");
        QTest::newRow("readiness-timeout") << QString("rpc_api_no_response");
    }
    void stoppedRecoveryRetainsRestartAndReadinessErrors() {
        QFETCH(QString, error);
        EverestConfig::EverestConfigServiceActions actions;
        actions.checkState = []() {
            return EverestStateAllowedResult{false, {}, QStringLiteral("rpc_api_not_connected")};
        };
        int serviceQueries = 0;
        actions.stoppedForRecovery = [&]() { return ++serviceQueries == 1; };
        actions.serviceActive = []() { return false; };
        actions.restart = [&]() { return EverestServiceControlResult{false, error}; };
        ModuleResponse response;
        response.success = false;
        const auto result = EverestConfig::restartEverestStack(response, true, actions);
        QVERIFY(!result.success);
        QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(), error);
    }

    void restartFailureDiagnostics_data() {
        QTest::addColumn<QString>("restartError");
        QTest::addColumn<bool>("stopped");
        QTest::addColumn<bool>("active");
        QTest::addColumn<QString>("expectedError");
        QTest::addColumn<QString>("messageFragment");
        QTest::addColumn<int>("expectedQueries");
        QTest::newRow("success") << QString() << true << false << QString() << QString() << 0;
        QTest::newRow("restart-rejected")
            << QString("everest_restart_failed") << false << false << QString("everest_restart_failed")
            << QString("restart request for EVerest failed") << 0;
        for (const auto& error : {QString("rpc_api_not_connected"), QString("rpc_api_no_response")}) {
            QTest::newRow(qPrintable(error + "-stopped")) << error << true << false << QString("everest_startup_failed")
                                                          << QString("not running after the restart attempt") << 1;
            QTest::newRow(qPrintable(error + "-active"))
                << error << false << true << error << QString("service is running") << 2;
            QTest::newRow(qPrintable(error + "-transitional-or-unreadable"))
                << error << false << false << error << QString("could not confirm") << 2;
        }
        QTest::newRow("permanent-rpc-error") << QString("rpc_api_error_present_missing") << true << false
                                             << QString("rpc_api_error_present_missing") << QString() << 0;
    }
    void restartFailureDiagnostics() {
        QFETCH(QString, restartError);
        QFETCH(bool, stopped);
        QFETCH(bool, active);
        QFETCH(QString, expectedError);
        QFETCH(QString, messageFragment);
        QFETCH(int, expectedQueries);
        for (const bool upload : {false, true}) {
            EverestConfig::EverestConfigServiceActions actions;
            int queries = 0;
            int restarts = 0;
            actions.checkState = []() { return EverestStateAllowedResult{true, QStringLiteral("Unplugged"), {}}; };
            actions.stoppedForRecovery = [&]() {
                ++queries;
                return stopped;
            };
            actions.serviceActive = [&]() {
                ++queries;
                return active;
            };
            actions.restart = [&]() {
                ++restarts;
                return EverestServiceControlResult{restartError.isEmpty(), restartError};
            };
            ModuleResponse response;
            const auto result = EverestConfig::restartEverestStack(response, upload, actions);
            QCOMPARE(restarts, 1);
            QCOMPARE(queries, expectedQueries);
            QCOMPARE(result.success, restartError.isEmpty());
            QCOMPARE(result.parameters.value(QStringLiteral("error")).toString(), expectedError);
            if (messageFragment.isEmpty()) {
                QVERIFY(!result.parameters.contains(QStringLiteral("message")));
            } else {
                const auto message = result.parameters.value(QStringLiteral("message")).toString();
                QVERIFY2(message.contains(messageFragment), qPrintable(message));
                QVERIFY(message.contains(QStringLiteral("configuration was saved")));
                QVERIFY(message.contains(QStringLiteral("Check the EVerest service logs")));
            }
        }
    }

    void uploadedFileNamingAndActivationStayUnchanged() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto basePath = directory.filePath(QStringLiteral("everest-ui-config.yaml"));
        const auto activePath = directory.filePath(QStringLiteral("config.yaml"));
        const auto name = EverestConfig::sanitizeUploadedConfigFileName(QStringLiteral("/some/path/rescue.yaml"));
        const auto target = EverestConfig::buildUploadedConfigTargetPath(basePath, activePath, name);
        QCOMPARE(target, directory.filePath(QStringLiteral("everest-ui-uploaded-rescue.yaml")));
        const auto yaml = QStringLiteral("active_modules: {}\n");
        QVERIFY(EverestConfig::writeTextFile(target, yaml));
        // Install the symlink before checking restart eligibility, as upload does.
        QVERIFY(QFile::link(target, activePath));
        QCOMPARE(QFileInfo(activePath).symLinkTarget(), target);
        EverestConfig::EverestConfigServiceActions actions;
        actions.checkState = []() {
            return EverestStateAllowedResult{false, {}, QStringLiteral("rpc_api_not_connected")};
        };
        actions.stoppedForRecovery = []() { return false; };
        actions.restart = []() { return EverestServiceControlResult{true, {}}; };
        ModuleResponse response;
        response.success = false;
        QVERIFY(!EverestConfig::restartEverestStack(response, true, actions).success);
        QString installed;
        QVERIFY(EverestConfig::readTextFile(activePath, installed));
        QCOMPARE(installed, yaml);
        QCOMPARE(QFileInfo(activePath).symLinkTarget(), target);
    }

    void missingYamlFileReportsOpenFailure() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const YamlLoadResult result = loadYamlFile(directory.filePath(QStringLiteral("missing.yaml")));

        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("everest_config_open_failed"));
    }

    void userConfigPathUsesCanonicalConfigTargetName() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString targetPath = directory.filePath(QStringLiteral("everest-ui-config.yaml"));
        const QString activeConfigPath = directory.filePath(QStringLiteral("config.yaml"));
        QFile targetFile(targetPath);
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        targetFile.close();
        QVERIFY(QFile::link(targetPath, activeConfigPath));

        const ConfigPathResult result = EverestConfig::resolveEverestUserConfigPath(activeConfigPath);

        QVERIFY(result.success);
        QCOMPARE(result.path,
                 directory.filePath(QStringLiteral("user-config/everest-ui-config.yaml")));
    }

    void appliesEverestCoreMergePatchSemantics() {
        const QJsonObject baseConfig{
            {QStringLiteral("active_modules"), QJsonObject{
                                                   {QStringLiteral("connector"), QJsonObject{
                                                                                      {QStringLiteral("module"), QStringLiteral("EvseManager")},
                                                                                      {QStringLiteral("config_module"), QJsonObject{
                                                                                                                       {QStringLiteral("old"), 1},
                                                                                                                       {QStringLiteral("kept"), true}}}}}}},
            {QStringLiteral("array"), QJsonArray{1, 2}}};
        const QJsonObject overlay{
            {QStringLiteral("active_modules"), QJsonObject{
                                                   {QStringLiteral("connector"), QJsonObject{
                                                                                      {QStringLiteral("config_module"), QJsonObject{
                                                                                                                       {QStringLiteral("old"), 2},
                                                                                                                       {QStringLiteral("removed"), QJsonValue(QJsonValue::Null)}}}}}}},
            {QStringLiteral("array"), QJsonArray{3}}};

        const QJsonObject result = EverestConfig::applyJsonMergePatch(baseConfig, overlay).toObject();
        const QJsonObject configModule = result.value(QStringLiteral("active_modules"))
                                              .toObject()
                                              .value(QStringLiteral("connector"))
                                              .toObject()
                                              .value(QStringLiteral("config_module"))
                                              .toObject();
        QCOMPARE(configModule.value(QStringLiteral("old")).toInt(), 2);
        QVERIFY(configModule.value(QStringLiteral("kept")).toBool());
        QVERIFY(!configModule.contains(QStringLiteral("removed")));
        QCOMPARE(result.value(QStringLiteral("array")).toArray().at(0).toInt(), 3);
        QCOMPARE(result.value(QStringLiteral("array")).toArray().size(), 1);
    }

    void omitsEmptyNonBooleanValuesAndKeepsFalse() {
        const QJsonObject baseConfig{
            {QStringLiteral("active_modules"), QJsonObject{
                                                   {QStringLiteral("module_profinet"), QJsonObject{
                                                                                          {QStringLiteral("module"), QStringLiteral("Profinet")}}}}}};
        const QJsonObject requestParameters{
            {QStringLiteral("Profinet"), QJsonObject{
                                              {QStringLiteral("profinet_timeout"), QString()},
                                              {QStringLiteral("charger_name"), QStringLiteral(" ")},
                                              {QStringLiteral("debug_mode"), false},
                                              {QStringLiteral("update_time"), 50}}}};

        const QJsonObject overlay =
            EverestConfig::buildEverestConfigOverlayObject(requestParameters, baseConfig);
        const QJsonObject configModule = overlay.value(QStringLiteral("active_modules"))
                                             .toObject()
                                             .value(QStringLiteral("module_profinet"))
                                             .toObject()
                                             .value(QStringLiteral("config_module"))
                                             .toObject();
        QVERIFY(!configModule.contains(QStringLiteral("profinet_timeout")));
        QVERIFY(!configModule.contains(QStringLiteral("charger_name")));
        QVERIFY(configModule.contains(QStringLiteral("debug_mode")));
        QVERIFY(!configModule.value(QStringLiteral("debug_mode")).toBool());
        QCOMPARE(configModule.value(QStringLiteral("update_time")).toInt(), 50);
    }

    void omitsModuleWhenAllValuesAreEmpty() {
        const QJsonObject baseConfig{
            {QStringLiteral("active_modules"), QJsonObject{
                                                   {QStringLiteral("module_profinet"), QJsonObject{
                                                                                          {QStringLiteral("module"), QStringLiteral("Profinet")}}}}}};
        const QJsonObject requestParameters{
            {QStringLiteral("Profinet"), QJsonObject{
                                              {QStringLiteral("profinet_timeout"), QString()},
                                              {QStringLiteral("charger_name"), QString()}}}};

        const QJsonObject overlay =
            EverestConfig::buildEverestConfigOverlayObject(requestParameters, baseConfig);
        QVERIFY(overlay.value(QStringLiteral("active_modules")).toObject().isEmpty());
    }
};

QTEST_MAIN(EverestConfigTest)
#include "EverestConfigTest.moc"
