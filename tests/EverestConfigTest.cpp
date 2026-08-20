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
