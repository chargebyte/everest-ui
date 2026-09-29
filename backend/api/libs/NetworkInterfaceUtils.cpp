// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "NetworkInterfaceUtils.hpp"

#include "BackendConfig.hpp"

#include <QAbstractSocket>
#include <QFileInfo>
#include <QHostAddress>

namespace {
constexpr char kNetworkIsoHighLevelCommsDrivers[] = "network_iso_high_level_comms_drivers";
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif
}

namespace NetworkInterfaceUtils {

QStringList parseIsoHighLevelCommsDriverList(const QString &configuredDrivers) {
    QStringList drivers;
    for (const QString &driver : configuredDrivers.split(QLatin1Char(','), kSkipEmptyParts)) {
        const QString trimmedDriver = driver.trimmed();
        if (!trimmedDriver.isEmpty()) {
            drivers.append(trimmedDriver);
        }
    }
    return drivers;
}

QStringList configuredIsoHighLevelCommsDrivers() {
    return parseIsoHighLevelCommsDriverList(
        readBackendConfigValue(QLatin1String(kNetworkIsoHighLevelCommsDrivers)));
}

QString driverName(const QString &interfaceName) {
    const QFileInfo driverInfo(QStringLiteral("/sys/class/net/%1/device/driver")
                                   .arg(interfaceName));
    if (!driverInfo.isSymLink()) {
        return {};
    }
    return QFileInfo(driverInfo.symLinkTarget()).fileName();
}

bool hasBridgeMaster(const QString &interfaceName) {
    return QFileInfo(QStringLiteral("/sys/class/net/%1/master").arg(interfaceName)).isSymLink();
}

bool hasLinkLocalIpv6(const QNetworkInterface &networkInterface) {
    const QHostAddress linkLocalSubnet(QStringLiteral("fe80::"));
    for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol &&
            entry.ip().isInSubnet(linkLocalSubnet, 10)) {
            return true;
        }
    }
    return false;
}

bool isIsoHighLevelCommsDriver(const QString &driver,
                               const QStringList &configuredDrivers) {
    return configuredDrivers.contains(driver, Qt::CaseInsensitive);
}

bool isLikelyIsoHighLevelCommsInterface(bool loopback, bool bridgeMember,
                                        bool up, bool running,
                                        bool hasLinkLocalIpv6Address,
                                        bool isoHighLevelCommsDriver) {
    return !loopback && !bridgeMember && up && running && hasLinkLocalIpv6Address &&
           isoHighLevelCommsDriver;
}

bool isLikelyIsoHighLevelCommsInterface(const QNetworkInterface &networkInterface,
                                        const QStringList &configuredDrivers) {
    const auto flags = networkInterface.flags();
    return isLikelyIsoHighLevelCommsInterface(
        flags.testFlag(QNetworkInterface::IsLoopBack),
        hasBridgeMaster(networkInterface.name()),
        flags.testFlag(QNetworkInterface::IsUp),
        flags.testFlag(QNetworkInterface::IsRunning),
        hasLinkLocalIpv6(networkInterface),
        isIsoHighLevelCommsDriver(driverName(networkInterface.name()), configuredDrivers));
}

} // namespace NetworkInterfaceUtils
