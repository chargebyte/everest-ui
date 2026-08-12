// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/NetworkConfiguration.cpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <QVector>

class NetworkConfigurationTest final : public QObject {
    Q_OBJECT

private slots:
    void bridgeInterfaceRemainsEditable() {
        InterfaceInfo info;
        info.name = QStringLiteral("br0");
        info.kind = QStringLiteral("bridge");
        QVERIFY(interfaceObject(info).value(QStringLiteral("editable")).toBool());
    }

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
        QVERIFY(settings.value(QStringLiteral("dhcp_ipv4_static")).toBool());
    }

    void supportsBridgeStyleStructuredAddress() {
        NetworkDocument document{{QStringLiteral("[Match]"), QStringLiteral("Name=br0"),
                                  QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
                                  QStringLiteral("[Address]"), QStringLiteral("Label=br0:fallback"),
                                  QStringLiteral("Address=169.254.12.53/16"),
                                  QStringLiteral("DuplicateAddressDetection=none")}};
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("test.network"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(document.lines.join(QLatin1Char('\n')).toUtf8());
        file.close();
        QVERIFY(analyzeNetworkFile(path).supported);

        const QJsonObject settings = parseDocument(document, QStringLiteral("br0"), path);
        const QJsonArray parsedAddresses = settings.value(QStringLiteral("ipv4_addresses")).toArray();
        QCOMPARE(parsedAddresses.size(), 2);
        QCOMPARE(parsedAddresses.at(0).toString(), QString());
        QCOMPARE(parsedAddresses.at(1).toString(), QStringLiteral("169.254.12.53/16"));
        QJsonArray replacementAddresses;
        replacementAddresses.append(QStringLiteral("192.168.0.38/24"));
        replacementAddresses.append(QStringLiteral("169.254.20.1/16"));
        restrictMatchToInterface(document, QStringLiteral("br0"));
        QVERIFY(replaceOwnedKeys(document, QJsonObject{
                                             {QStringLiteral("dhcp_ipv4"), true},
                                             {QStringLiteral("dhcp_ipv6"), true},
                                             {QStringLiteral("dhcp_ipv4_static"), true},
                                             {QStringLiteral("ipv4_addresses"), replacementAddresses},
                                             {QStringLiteral("gateway"), QString()},
                                             {QStringLiteral("dns"), QJsonArray{}}}));
        const QString output = document.lines.join(QLatin1Char('\n'));
        QCOMPARE(output.count(QStringLiteral("[Network]")), 1);
        QVERIFY(output.contains(QStringLiteral("[Match]\nName=br0\n\n[Network]")));
        QVERIFY(output.contains(QStringLiteral("Label=br0:fallback")));
        QVERIFY(output.contains(QStringLiteral("Address=192.168.0.38/24")));
        QVERIFY(output.contains(QStringLiteral("Address=169.254.20.1/16")));
        QVERIFY(output.contains(QStringLiteral("DuplicateAddressDetection=none")));

        QJsonArray noAddresses;
        document = NetworkDocument{{QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
                                    QStringLiteral("[Address]"), QStringLiteral("Label=br0:fallback"),
                                    QStringLiteral("Address=169.254.12.53/16"),
                                    QStringLiteral("DuplicateAddressDetection=none")}};
        QVERIFY(replaceOwnedKeys(document, QJsonObject{
                                             {QStringLiteral("dhcp_ipv4"), true},
                                             {QStringLiteral("dhcp_ipv6"), true},
                                             {QStringLiteral("dhcp_ipv4_static"), false},
                                             {QStringLiteral("ipv4_addresses"), noAddresses},
                                             {QStringLiteral("gateway"), QString()},
                                             {QStringLiteral("dns"), QJsonArray{}}}));
        QVERIFY(!document.lines.join(QLatin1Char('\n')).contains(QStringLiteral("Label=br0:fallback")));
    }

    void rejectsAmbiguousStructuredAddresses() {
        NetworkDocument document{{QStringLiteral("[Address]"), QStringLiteral("Address=192.168.1.20/24"),
                                  QStringLiteral("Address=192.168.1.21/24")}};
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("test.network"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(document.lines.join(QLatin1Char('\n')).toUtf8());
        file.close();
        QVERIFY(!analyzeNetworkFile(path).supported);
    }

    void rejectsStructuredRouteProperties() {
        NetworkDocument document{{QStringLiteral("[Route]"), QStringLiteral("Destination=10.20.0.0/16"),
                                  QStringLiteral("Gateway=192.168.1.1"), QStringLiteral("Metric=100")}};
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("test.network"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(document.lines.join(QLatin1Char('\n')).toUtf8());
        file.close();
        QVERIFY(!analyzeNetworkFile(path).supported);
    }

    void validatesOptionalAddressSlots() {
        const auto addressArray = [](const QString &primary, const QString &fallback) {
            QJsonArray addresses;
            addresses.append(primary);
            addresses.append(fallback);
            return addresses;
        };
        const auto validateAddresses = [](const QJsonArray &addresses) {
            QString error;
            return validateSettings(QJsonObject{
                                         {QStringLiteral("dhcp_ipv4"), true},
                                         {QStringLiteral("dhcp_ipv6"), true},
                                         {QStringLiteral("dhcp_ipv4_static"), true},
                                         {QStringLiteral("ipv4_addresses"), addresses},
                                         {QStringLiteral("gateway"), QString()},
                                         {QStringLiteral("dns"), QJsonArray{}}},
                                     error);
        };

        QVERIFY(validateAddresses(addressArray(QStringLiteral("192.168.99.99/24"), QString())));
        QVERIFY(validateAddresses(addressArray(QString(), QStringLiteral("169.254.12.53/16"))));
        QVERIFY(validateAddresses(addressArray(QStringLiteral("192.168.99.99/24"),
                                               QStringLiteral("169.254.12.53/16"))));
        QVERIFY(!validateAddresses(addressArray(QString(), QString())));
        QVERIFY(!validateAddresses(addressArray(QStringLiteral("192.168.99.99/24"), QStringLiteral("bad"))));
    }

    void mixedDhcpPreservesStaticIpv4Settings() {
        NetworkDocument document{{QStringLiteral("[Network]"), QStringLiteral("DHCP=yes")}};
        QJsonArray addresses;
        addresses.append(QStringLiteral("192.168.1.20/24"));
        const QJsonObject settings{{QStringLiteral("dhcp_ipv4"), true},
                                   {QStringLiteral("dhcp_ipv6"), true},
                                   {QStringLiteral("dhcp_ipv4_static"), true},
                                   {QStringLiteral("ipv4_addresses"), addresses},
                                   {QStringLiteral("gateway"), QStringLiteral("192.168.1.1")},
                                   {QStringLiteral("dns"), QJsonArray{}}};
        replaceOwnedKeys(document, settings);
        const QString output = document.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
        QVERIFY(output.contains(QStringLiteral("DHCP=yes\n")));
        QVERIFY(output.contains(QStringLiteral("Address=192.168.1.20/24\n")));
        QVERIFY(output.contains(QStringLiteral("Gateway=192.168.1.1\n")));
    }

    void removesUserOverrideAndIsIdempotent() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("eth0.network"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(removeUserNetworkOverride(path));
        QVERIFY(!QFile::exists(path));
        QVERIFY(removeUserNetworkOverride(path));
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

    void resetResponseContractIsStaged() {
        QJsonObject parameters{
            {QStringLiteral("interface"), QStringLiteral("eth0")},
            {QStringLiteral("user_override"), false},
            {QStringLiteral("reset_staged"), true}};
        QVERIFY(!parameters.value(QStringLiteral("user_override")).toBool());
        QCOMPARE(parameters.value(QStringLiteral("interface")).toString(), QStringLiteral("eth0"));
        QVERIFY(parameters.value(QStringLiteral("reset_staged")).toBool());
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
