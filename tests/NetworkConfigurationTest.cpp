// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "../backend/api/modules/NetworkConfiguration.cpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <QVector>

namespace {
bool writeOwnedOverlay(const QString &path, const QByteArray &contents = {}) {
    if (!QDir().mkpath(QFileInfo(path).dir().absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Text) &&
           file.write(QByteArray(kOwnedOverlayMarker) + '\n' + contents) >= 0;
}

QString testOverlayPath(const QString &root, const QString &interfaceName) {
    return root + QLatin1Char('/') + interfaceName + QStringLiteral(".network.d/50-everest-ui.conf");
}
} // namespace

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
        NetworkDocument overlay;
        const bool overlayBuilt = buildOverlayDocument(
            document, QJsonObject{{QStringLiteral("dhcp_ipv4"), true},
                                  {QStringLiteral("dhcp_ipv6"), true},
                                  {QStringLiteral("dhcp_ipv4_static"), true},
                                  {QStringLiteral("ipv4_addresses"), replacementAddresses},
                                  {QStringLiteral("gateway"), QString()},
                                  {QStringLiteral("dns"), QJsonArray{}}},
            overlay);
        QVERIFY(!overlayBuilt);
    }

    void parsesFallbackLabelAfterAddress() {
        NetworkDocument document{{QStringLiteral("[Network]"), QStringLiteral("Address=192.168.0.38/24"),
                                  QStringLiteral("[Address]"), QStringLiteral("Address=169.254.12.53/16"),
                                  QStringLiteral("Label=br0:fallback")}};
        const QJsonArray addresses = parseDocument(document, QStringLiteral("br0"), QStringLiteral("file"))
                                         .value(QStringLiteral("ipv4_addresses"))
                                         .toArray();
        QCOMPARE(addresses.size(), 2);
        QCOMPARE(addresses.at(0).toString(), QStringLiteral("192.168.0.38/24"));
        QCOMPARE(addresses.at(1).toString(), QStringLiteral("169.254.12.53/16"));
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

    void rejectsRepeatedNetworkAddressesAndGateways() {
        const auto writeAndAnalyze = [](const QStringList &lines) {
            const QTemporaryDir directory;
            if (!directory.isValid()) {
                return true;
            }
            const QString path = directory.filePath(QStringLiteral("test.network"));
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return true;
            }
            file.write(lines.join(QLatin1Char('\n')).toUtf8());
            file.close();
            return analyzeNetworkFile(path).supported;
        };
        QVERIFY(!writeAndAnalyze({QStringLiteral("[Network]"), QStringLiteral("Address=192.168.1.20/24"),
                                  QStringLiteral("Address=192.168.1.21/24")}));
        QVERIFY(!writeAndAnalyze({QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
                                  QStringLiteral("Gateway=192.168.1.1"), QStringLiteral("Gateway=192.168.1.2")}));
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
        NetworkDocument overlay;
        QVERIFY(buildOverlayDocument(document, settings, overlay));
        const QString output = overlay.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
        QVERIFY(output.startsWith(QLatin1String(kOwnedOverlayMarker)));
        QVERIFY(output.contains(QStringLiteral("[Network]\n")));
        QVERIFY(output.contains(QStringLiteral("Address=192.168.1.20/24\n")));
        QVERIFY(output.contains(QStringLiteral("Gateway=192.168.1.1\n")));
        QVERIFY(!output.contains(QStringLiteral("[Match]")));
    }

    void writingDhcpOnlyEmitsOnlyChangedDirective() {
        NetworkDocument document{{QStringLiteral("[Match]"), QStringLiteral("Name=en*"),
                                  QStringLiteral("Driver=example"), QStringLiteral("[Network]"),
                                  QStringLiteral("DHCP=yes"), QStringLiteral("Address=192.168.1.20/24"),
                                  QStringLiteral("Gateway=192.168.1.1")}};
        QJsonArray addresses;
        addresses.append(QStringLiteral("192.168.1.20/24"));
        const QJsonObject settings{
            {QStringLiteral("dhcp_ipv4"), true},
            {QStringLiteral("dhcp_ipv6"), false},
            {QStringLiteral("dhcp_ipv4_static"), true},
            {QStringLiteral("ipv4_addresses"), addresses},
            {QStringLiteral("gateway"), QStringLiteral("192.168.1.1")},
            {QStringLiteral("dns"), QJsonArray{}}};

        NetworkDocument overlay;
        QVERIFY(buildOverlayDocument(document, settings, overlay));
        const QString overlayText = overlay.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
        QVERIFY(overlayText.contains(QStringLiteral("DHCP=ipv4\n")));
        QCOMPARE(overlayText.count(QStringLiteral("DHCP=")), 1);
        QVERIFY(!overlayText.contains(QStringLiteral("Name=")));
        QVERIFY(!overlayText.contains(QStringLiteral("Driver=")));
        QVERIFY(!overlayText.contains(QStringLiteral("Address=")));
        QVERIFY(!overlayText.contains(QStringLiteral("Gateway=")));
    }

    void unchangedSettingsProduceNoOverlayDirectives() {
        NetworkDocument underlay{{QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
                                  QStringLiteral("DNS=8.8.8.8")}};
        QJsonArray dns;
        dns.append(QStringLiteral("8.8.8.8"));
        NetworkDocument overlay;
        QVERIFY(buildOverlayDocument(
            underlay, QJsonObject{{QStringLiteral("dhcp_ipv4"), true},
                                  {QStringLiteral("dhcp_ipv6"), true},
                                  {QStringLiteral("dhcp_ipv4_static"), false},
                                  {QStringLiteral("ipv4_addresses"), QJsonArray{}},
                                  {QStringLiteral("gateway"), QString()},
                                  {QStringLiteral("dns"), dns}},
            overlay));
        QVERIFY(overlay.lines.isEmpty());
    }

    void dropInDirectoryUsesSelectedNetworkFilename() {
        QCOMPARE(userNetworkOverlayPath(QStringLiteral("/lib/systemd/network/10-wired.network")),
                 QStringLiteral("/etc/systemd/network/10-wired.network.d/50-everest-ui.conf"));
    }

    void lowerPriorityOwnedNameIsNotShadowedByNewOverlay() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString networkFile = QStringLiteral("/usr/lib/systemd/network/10-wired.network");
        const QString target = directory.filePath(QStringLiteral("etc/50-everest-ui.conf"));
        const QString lowerPath = directory.filePath(
            QStringLiteral("run/10-wired.network.d/50-everest-ui.conf"));
        QVERIFY(QDir().mkpath(QFileInfo(lowerPath).dir().absolutePath()));
        QFile lower(lowerPath);
        QVERIFY(lower.open(QIODevice::WriteOnly | QIODevice::Text));
        lower.write("[Network]\nDHCP=no\n");
        lower.close();
        QVERIFY(lowerPriorityOverlayConflict(networkFile, target,
                                             {directory.filePath(QStringLiteral("run"))}));

        QVERIFY(QDir().mkpath(QFileInfo(target).dir().absolutePath()));
        QFile adopted(target);
        QVERIFY(adopted.open(QIODevice::WriteOnly | QIODevice::Text));
        adopted.write("user-owned until adopted");
        adopted.close();
        QVERIFY(!lowerPriorityOverlayConflict(networkFile, target,
                                              {directory.filePath(QStringLiteral("run"))}));
    }

    void mergesDropInsByNameAndDirectoryPrecedence() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString etcRoot = directory.filePath(QStringLiteral("etc")) + QLatin1Char('/');
        const QString runRoot = directory.filePath(QStringLiteral("run")) + QLatin1Char('/');
        const QString usrRoot = directory.filePath(QStringLiteral("usr")) + QLatin1Char('/');
        const QString mainFile = usrRoot + QStringLiteral("10-wired.network");
        const QString dropInSuffix = QStringLiteral("10-wired.network.d/");
        const auto writeFile = [](const QString &path, const QByteArray &contents) {
            if (!QDir().mkpath(QFileInfo(path).dir().absolutePath())) {
                return false;
            }
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Text) && file.write(contents) >= 0;
        };
        QVERIFY(writeFile(mainFile, "[Network]\nDHCP=no\nDNS=8.8.8.8\n"));
        QVERIFY(writeFile(usrRoot + dropInSuffix + QStringLiteral("20-vendor.conf"),
                          "[Network]\nDHCP=ipv4\n"));
        QVERIFY(writeFile(runRoot + dropInSuffix + QStringLiteral("20-vendor.conf"),
                          "[Network]\nDHCP=ipv6\n"));
        QVERIFY(writeFile(etcRoot + dropInSuffix + QStringLiteral("20-vendor.conf"),
                          "[Network]\nDHCP=yes\n"));
        QVERIFY(writeFile(runRoot + dropInSuffix + QStringLiteral("30-run.conf"),
                          "[Network]\nDNS=\nDNS=1.1.1.1\n"));

        bool ok = false;
        const NetworkDocument effective = readEffectiveNetworkDocumentFromRoots(
            mainFile, true, ok, {etcRoot, runRoot, usrRoot},
            etcRoot + dropInSuffix + QLatin1String(kOwnedOverlayName));
        QVERIFY(ok);
        const QJsonObject settings = parseDocument(effective, QStringLiteral("eth0"), mainFile);
        QVERIFY(settings.value(QStringLiteral("dhcp_ipv4")).toBool());
        QVERIFY(settings.value(QStringLiteral("dhcp_ipv6")).toBool());
        QCOMPARE(settings.value(QStringLiteral("dns")).toArray().size(), 1);
        QCOMPARE(settings.value(QStringLiteral("dns")).toArray().at(0).toString(),
                 QStringLiteral("1.1.1.1"));
    }

    void deltaPreservesDnsValuesOutsideTheEditor() {
        NetworkDocument underlay{{QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
                                  QStringLiteral("DNS=2001:db8::53"),
                                  QStringLiteral("DNS=8.8.8.8")}};
        QJsonArray dns;
        dns.append(QStringLiteral("1.1.1.1"));
        NetworkDocument overlay;
        QVERIFY(buildOverlayDocument(
            underlay, QJsonObject{{QStringLiteral("dhcp_ipv4"), true},
                                  {QStringLiteral("dhcp_ipv6"), true},
                                  {QStringLiteral("dhcp_ipv4_static"), false},
                                  {QStringLiteral("ipv4_addresses"), QJsonArray{}},
                                  {QStringLiteral("gateway"), QString()},
                                  {QStringLiteral("dns"), dns}},
            overlay));
        const QString output = overlay.lines.join(QLatin1Char('\n'));
        QVERIFY(output.contains(QStringLiteral("DNS=\nDNS=2001:db8::53\nDNS=1.1.1.1")));
        QVERIFY(!output.contains(QStringLiteral("DNS=8.8.8.8")));
    }

    void emptyAddressAndGatewayValuesClearTheUnderlay() {
        NetworkDocument underlay{{QStringLiteral("[Network]"), QStringLiteral("DHCP=yes"),
                                  QStringLiteral("Address=192.168.1.20/24"),
                                  QStringLiteral("Gateway=192.168.1.1")}};
        NetworkDocument overlay;
        QVERIFY(buildOverlayDocument(
            underlay, QJsonObject{{QStringLiteral("dhcp_ipv4"), true},
                                  {QStringLiteral("dhcp_ipv6"), true},
                                  {QStringLiteral("dhcp_ipv4_static"), false},
                                  {QStringLiteral("ipv4_addresses"), QJsonArray{}},
                                  {QStringLiteral("gateway"), QString()},
                                  {QStringLiteral("dns"), QJsonArray{}}},
            overlay));
        const QString output = overlay.lines.join(QLatin1Char('\n'));
        QVERIFY(output.contains(QStringLiteral("Address=")));
        QVERIFY(output.contains(QStringLiteral("Gateway=")));
        QVERIFY(!output.contains(QStringLiteral("Address=192.168.1.20/24")));
        QVERIFY(!output.contains(QStringLiteral("Gateway=192.168.1.1")));
    }

    void addressDeltaRejectsChangesThatWouldClearIpv6() {
        NetworkDocument underlay{{QStringLiteral("[Network]"), QStringLiteral("DHCP=no"),
                                  QStringLiteral("Address=192.168.1.20/24"),
                                  QStringLiteral("Address=2001:db8::20/64")}};
        QJsonArray addresses;
        addresses.append(QStringLiteral("192.168.1.21/24"));
        NetworkDocument overlay;
        QVERIFY(!buildOverlayDocument(
            underlay, QJsonObject{{QStringLiteral("dhcp_ipv4"), false},
                                  {QStringLiteral("dhcp_ipv6"), false},
                                  {QStringLiteral("dhcp_ipv4_static"), false},
                                  {QStringLiteral("ipv4_addresses"), addresses},
                                  {QStringLiteral("gateway"), QString()},
                                  {QStringLiteral("dns"), QJsonArray{}}},
            overlay));
    }

    void ownershipMarkerControlsOverlayManagement() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("50-everest-ui.conf"));
        QFile unmarked(path);
        QVERIFY(unmarked.open(QIODevice::WriteOnly | QIODevice::Text));
        unmarked.write("[Network]\nDHCP=no\n");
        unmarked.close();
        QVERIFY(!isUiOwnedOverlay(path));
        QVERIFY(writeOwnedOverlay(path, "[Network]\nDHCP=no\n"));
        QVERIFY(isUiOwnedOverlay(path));
    }

    void resetRejectsUnmarkedOverlayAndPreservesMainNetworkFile() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString interfaceName = QStringLiteral("eth0");
        const QString mainFile = directory.filePath(QStringLiteral("eth0.network"));
        QFile main(mainFile);
        QVERIFY(main.open(QIODevice::WriteOnly | QIODevice::Text));
        main.write("base config");
        main.close();
        const QString overlayPath = testOverlayPath(directory.path(), interfaceName);
        QFile overlay(overlayPath);
        QVERIFY(QDir().mkpath(QFileInfo(overlayPath).dir().absolutePath()));
        QVERIFY(overlay.open(QIODevice::WriteOnly | QIODevice::Text));
        overlay.write("[Network]\nDHCP=no\n");
        overlay.close();

        QSet<QString> pending{interfaceName};
        int reloadCount = 0;
        const ResetApplyResult result = applyPendingResets(
            pending, [&overlayPath](const QString &) { return overlayPath; },
            [&reloadCount](const QString &, const QStringList &) {
                ++reloadCount;
                return CommandResult{true, 0, {}};
            });

        QVERIFY(result.writeFailed);
        QCOMPARE(reloadCount, 0);
        QVERIFY(QFile::exists(mainFile));
        QVERIFY(QFile::exists(overlayPath));
        QVERIFY(pending.contains(interfaceName));
    }

    void resetResponseContractIsStaged() {
        QJsonObject parameters{
            {QStringLiteral("interface"), QStringLiteral("eth0")},
            {QStringLiteral("user_override"), true},
            {QStringLiteral("reset_staged"), true}};
        QVERIFY(parameters.value(QStringLiteral("user_override")).toBool());
        QCOMPARE(parameters.value(QStringLiteral("interface")).toString(), QStringLiteral("eth0"));
        QVERIFY(parameters.value(QStringLiteral("reset_staged")).toBool());
    }

    void resetStateCanBeCancelled() {
        const QString interfaceName = QStringLiteral("eth-test-reset");
        g_pendingResetInterfaces.insert(interfaceName);
        QVERIFY(g_pendingResetInterfaces.contains(interfaceName));
        g_pendingResetInterfaces.remove(interfaceName);
        QVERIFY(!g_pendingResetInterfaces.contains(interfaceName));
    }

    void resetApplyRemovesAllOverridesAfterSuccessfulReload() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSet<QString> pending{QStringLiteral("eth0"), QStringLiteral("eth1")};
        for (const QString &interfaceName : pending) {
            QFile base(directory.filePath(interfaceName + QStringLiteral(".network")));
            QVERIFY(base.open(QIODevice::WriteOnly | QIODevice::Text));
            QVERIFY(base.write("base configuration") >= 0);
            base.close();
            QVERIFY(writeOwnedOverlay(testOverlayPath(directory.path(), interfaceName),
                                      interfaceName.toUtf8()));
        }

        const ResetApplyResult result = applyPendingResets(
            pending,
            [&directory](const QString &interfaceName) {
                return testOverlayPath(directory.path(), interfaceName);
            },
            [](const QString &, const QStringList &) { return CommandResult{true, 0, {}}; });

        QVERIFY(result.success);
        QVERIFY(pending.isEmpty());
        for (const QString &interfaceName : {QStringLiteral("eth0"), QStringLiteral("eth1")}) {
            const QString path = testOverlayPath(directory.path(), interfaceName);
            QVERIFY(QFile::exists(directory.filePath(interfaceName + QStringLiteral(".network"))));
            QVERIFY(!QFile::exists(path));
            QVERIFY(!QFile::exists(path + QLatin1String(kResetBackupSuffix)));
            QVERIFY(!QFile::exists(path + QLatin1String(kResetCommittedSuffix)));
        }
    }

    void resetApplyRestoresAllOverridesAfterReloadFailure() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSet<QString> pending{QStringLiteral("eth0"), QStringLiteral("eth1")};
        QHash<QString, QByteArray> contents;
        for (const QString &interfaceName : pending) {
            contents.insert(interfaceName, interfaceName.toUtf8());
            QVERIFY(writeOwnedOverlay(testOverlayPath(directory.path(), interfaceName),
                                      contents.value(interfaceName)));
        }

        int reloadCount = 0;
        const ResetApplyResult result = applyPendingResets(
            pending,
            [&directory](const QString &interfaceName) {
                return testOverlayPath(directory.path(), interfaceName);
            },
            [&reloadCount](const QString &, const QStringList &) {
                ++reloadCount;
                return CommandResult{true, 1, {}};
            });

        QVERIFY(!result.success);
        QVERIFY(result.applyFailed);
        QVERIFY(!result.writeFailed);
        QCOMPARE(reloadCount, 2);
        QCOMPARE(pending.size(), 2);
        for (const QString &interfaceName : pending) {
            const QString path = testOverlayPath(directory.path(), interfaceName);
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            QCOMPARE(file.readAll(), QByteArray(kOwnedOverlayMarker) + '\n' + contents.value(interfaceName));
            QVERIFY(!QFile::exists(path + QLatin1String(kResetBackupSuffix)));
        }
    }

    void resetApplyRollsBackWhenCommittedMarkerCreationFails() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSet<QString> pending{QStringLiteral("eth0"), QStringLiteral("eth1")};
        for (const QString &interfaceName : pending) {
            QVERIFY(writeOwnedOverlay(testOverlayPath(directory.path(), interfaceName),
                                      interfaceName.toUtf8()));
        }
        QFile existingCommitted(testOverlayPath(directory.path(), QStringLiteral("eth1")) +
                                QLatin1String(kResetCommittedSuffix));
        QVERIFY(QDir().mkpath(QFileInfo(existingCommitted.fileName()).dir().absolutePath()));
        QVERIFY(existingCommitted.open(QIODevice::WriteOnly | QIODevice::Text));
        existingCommitted.write("existing committed marker");
        existingCommitted.close();

        int reloadCount = 0;
        const ResetApplyResult result = applyPendingResets(
            pending,
            [&directory](const QString &interfaceName) {
                return testOverlayPath(directory.path(), interfaceName);
            },
            [&reloadCount](const QString &, const QStringList &) {
                ++reloadCount;
                return CommandResult{true, 0, {}};
            });

        QVERIFY(!result.success);
        QVERIFY(result.applyFailed);
        QCOMPARE(reloadCount, 2);
        for (const QString &interfaceName : pending) {
            const QString path = testOverlayPath(directory.path(), interfaceName);
            QVERIFY(QFile::exists(path));
            QVERIFY(!QFile::exists(path + QLatin1String(kResetBackupSuffix)));
        }
        QVERIFY(QFile::exists(testOverlayPath(directory.path(), QStringLiteral("eth1")) +
                              QLatin1String(kResetCommittedSuffix)));
    }

    void resetApplyRestoresEarlierOverridesWhenStagingLaterOneFails() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSet<QString> pending{QStringLiteral("eth0"), QStringLiteral("eth1")};
        for (const QString &interfaceName : pending) {
            QVERIFY(writeOwnedOverlay(testOverlayPath(directory.path(), interfaceName),
                                      interfaceName.toUtf8()));
        }
        QFile existingBackup(testOverlayPath(directory.path(), QStringLiteral("eth1")) +
                             QLatin1String(kResetBackupSuffix));
        QVERIFY(QDir().mkpath(QFileInfo(existingBackup.fileName()).dir().absolutePath()));
        QVERIFY(existingBackup.open(QIODevice::WriteOnly | QIODevice::Text));
        existingBackup.write("existing backup");
        existingBackup.close();

        int reloadCount = 0;
        const ResetApplyResult result = applyPendingResets(
            pending,
            [&directory](const QString &interfaceName) {
                return testOverlayPath(directory.path(), interfaceName);
            },
            [&reloadCount](const QString &, const QStringList &) {
                ++reloadCount;
                return CommandResult{true, 0, {}};
            });

        QVERIFY(!result.success);
        QVERIFY(result.writeFailed);
        QCOMPARE(reloadCount, 0);
        QVERIFY(QFile::exists(testOverlayPath(directory.path(), QStringLiteral("eth0"))));
        QVERIFY(QFile::exists(testOverlayPath(directory.path(), QStringLiteral("eth1"))));
        QVERIFY(QFile::exists(testOverlayPath(directory.path(), QStringLiteral("eth1")) +
                              QLatin1String(kResetBackupSuffix)));
        QVERIFY(pending.contains(QStringLiteral("eth0")));
        QVERIFY(pending.contains(QStringLiteral("eth1")));
    }

    void orphanedResetBackupIsRecovered() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString originalPath = directory.filePath(QStringLiteral("eth0.network"));
        const QString backupPath = originalPath + QLatin1String(kResetBackupSuffix);
        QFile backup(backupPath);
        QVERIFY(backup.open(QIODevice::WriteOnly | QIODevice::Text));
        backup.write("original");
        backup.close();

        QVERIFY(recoverResetBackupsInDirectory(
            directory.path(), [](const QString &interfaceName) { return interfaceName == QStringLiteral("eth0"); }));
        QVERIFY(QFile::exists(originalPath));
        QVERIFY(!QFile::exists(backupPath));
    }

    void unavailableResetBackupIsRetried() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString originalPath = directory.filePath(QStringLiteral("eth1.network"));
        const QString backupPath = originalPath + QLatin1String(kResetBackupSuffix);
        QFile backup(backupPath);
        QVERIFY(backup.open(QIODevice::WriteOnly | QIODevice::Text));
        backup.write("original");
        backup.close();

        QVERIFY(!recoverResetBackupsInDirectory(directory.path(), [](const QString &) { return false; }));
        QVERIFY(QFile::exists(backupPath));
        QVERIFY(recoverResetBackupsInDirectory(
            directory.path(), [](const QString &interfaceName) { return interfaceName == QStringLiteral("eth1"); }));
        QVERIFY(QFile::exists(originalPath));
        QVERIFY(!QFile::exists(backupPath));
    }

    void committedResetBackupIsRemovedWithoutRestoring() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString originalPath = directory.filePath(QStringLiteral("eth2.network"));
        const QString committedPath = originalPath + QLatin1String(kResetCommittedSuffix);
        QFile committed(committedPath);
        QVERIFY(committed.open(QIODevice::WriteOnly | QIODevice::Text));
        committed.write("old configuration");
        committed.close();

        QVERIFY(recoverResetBackupsInDirectory(directory.path(), [](const QString &) { return false; }));
        QVERIFY(!QFile::exists(originalPath));
        QVERIFY(!QFile::exists(committedPath));
    }

    void orphanedOverlayResetBackupIsRecovered() {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString dropInDirectory = directory.filePath(QStringLiteral("10-wired.network.d"));
        QVERIFY(QDir().mkpath(dropInDirectory));
        const QString overlayPath = dropInDirectory + QStringLiteral("/50-everest-ui.conf");
        const QString backupPath = overlayPath + QLatin1String(kResetBackupSuffix);
        QFile backup(backupPath);
        QVERIFY(backup.open(QIODevice::WriteOnly | QIODevice::Text));
        backup.write(kOwnedOverlayMarker);
        backup.close();

        QVERIFY(recoverOverlayResetBackupsInDirectory(directory.path()));
        QVERIFY(QFile::exists(overlayPath));
        QVERIFY(isUiOwnedOverlay(overlayPath));
        QVERIFY(!QFile::exists(backupPath));
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
