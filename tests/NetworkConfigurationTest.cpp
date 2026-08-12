// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/NetworkConfiguration.cpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include <QVector>

class NetworkConfigurationTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesDhcpFamiliesAndEquivalentSections() {
        NetworkDocument document{
            {QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
             QStringLiteral("DNS=192.168.1.1"), QStringLiteral("[Address]"),
             QStringLiteral("Address=192.168.1.20/24"), QStringLiteral("[Route]"),
             QStringLiteral("Gateway=192.168.1.1")}};

        const QJsonObject settings = parseDocument(document, QStringLiteral("eth0"), QStringLiteral("file"));
        QVERIFY(settings.value(QStringLiteral("dhcp_ipv4")).toBool());
        QVERIFY(settings.value(QStringLiteral("dhcp_ipv6")).toBool());
        QCOMPARE(settings.value(QStringLiteral("ipv4_addresses")).toArray().at(0).toString(),
                 QStringLiteral("192.168.1.20/24"));
        QCOMPARE(settings.value(QStringLiteral("gateway")).toString(), QStringLiteral("192.168.1.1"));
    }

    void writingDhcpOnlyPreservesIpv6AndMatch() {
        NetworkDocument document{
            {QStringLiteral("[Match]"), QStringLiteral("Name=en*"),
             QStringLiteral("Driver=example"), QStringLiteral("[Network]"),
             QStringLiteral("DHCP=yes"), QStringLiteral("Address=192.168.1.20/24"),
             QStringLiteral("Gateway=192.168.1.1")}};
        QJsonArray addresses;
        addresses.append(QStringLiteral("192.168.1.20/24"));
        const QJsonObject settings{
            {QStringLiteral("dhcp_ipv4"), true},
            {QStringLiteral("dhcp_ipv6"), true},
            {QStringLiteral("ipv4_addresses"), addresses},
            {QStringLiteral("gateway"), QStringLiteral("192.168.1.1")},
            {QStringLiteral("dns"), QJsonArray{}}};

        restrictMatchToInterface(document, QStringLiteral("eth0"));
        replaceOwnedKeys(document, settings);
        const QString output = document.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
        QVERIFY(output.contains(QStringLiteral("Name=eth0\n")));
        QVERIFY(output.contains(QStringLiteral("DHCP=yes\n")));
        QVERIFY(!output.contains(QStringLiteral("Address=192.168.1.20/24")));
        QVERIFY(!output.contains(QStringLiteral("Gateway=192.168.1.1")));
        QVERIFY(output.endsWith(QLatin1Char('\n')));
    }

    void resetResponseContractIncludesFactoryState() {
        QJsonObject parameters{
            {QStringLiteral("interface"), QStringLiteral("eth0")},
            {QStringLiteral("network_file"), QStringLiteral("/lib/systemd/network/80-wired.network")},
            {QStringLiteral("user_override"), false},
            {QStringLiteral("dhcp_ipv4"), true},
            {QStringLiteral("dhcp_ipv6"), true}};
        QVERIFY(!parameters.value(QStringLiteral("user_override")).toBool());
        QCOMPARE(parameters.value(QStringLiteral("interface")).toString(), QStringLiteral("eth0"));
        QVERIFY(parameters.value(QStringLiteral("dhcp_ipv4")).toBool());
        QVERIFY(parameters.value(QStringLiteral("dhcp_ipv6")).toBool());
    }

    void applyReloadsOnlyOnce() {
        QVector<QStringList> commands;
        const bool applied = applyNetworkConfiguration(
            [&commands](const QString &program, const QStringList &arguments) {
                commands.append(QStringList{program, arguments.join(QLatin1Char(' '))});
                return CommandResult{true, 0, {}};
            });

        QVERIFY(applied);
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands.at(0).at(0), QStringLiteral("networkctl"));
        QCOMPARE(commands.at(0).at(1), QStringLiteral("reload"));
    }

    void applyFailsWhenReloadFails() {
        int commandCount = 0;
        const bool applied = applyNetworkConfiguration(
            [&commandCount](const QString &, const QStringList &) {
                ++commandCount;
                return CommandResult{true, 1, {}};
            });

        QVERIFY(!applied);
        QCOMPARE(commandCount, 1);
    }
};

QTEST_MAIN(NetworkConfigurationTest)
#include "NetworkConfigurationTest.moc"
