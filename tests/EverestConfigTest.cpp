// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/EverestConfig.cpp"

#include <QJsonObject>
#include <QTest>

class EverestConfigTest final : public QObject {
    Q_OBJECT

private slots:
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

        const QJsonObject overlay = buildEverestConfigOverlayObject(requestParameters, baseConfig);
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

        const QJsonObject overlay = buildEverestConfigOverlayObject(requestParameters, baseConfig);
        QVERIFY(overlay.value(QStringLiteral("active_modules")).toObject().isEmpty());
    }
};

QTEST_MAIN(EverestConfigTest)
#include "EverestConfigTest.moc"
