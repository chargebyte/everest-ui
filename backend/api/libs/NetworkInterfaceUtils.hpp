// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef NETWORK_INTERFACE_UTILS_HPP
#define NETWORK_INTERFACE_UTILS_HPP

#include <QNetworkInterface>
#include <QStringList>

namespace NetworkInterfaceUtils {

QStringList parseIsoHighLevelCommsDriverList(const QString &configuredDrivers);
QStringList configuredIsoHighLevelCommsDrivers();
QString driverName(const QString &interfaceName);
bool hasBridgeMaster(const QString &interfaceName);
bool hasLinkLocalIpv6(const QNetworkInterface &networkInterface);
bool isIsoHighLevelCommsDriver(const QString &driver,
                               const QStringList &configuredDrivers);
bool isLikelyIsoHighLevelCommsInterface(bool loopback, bool bridgeMember,
                                        bool up, bool running,
                                        bool hasLinkLocalIpv6,
                                        bool isoHighLevelCommsDriver);
bool isLikelyIsoHighLevelCommsInterface(const QNetworkInterface &networkInterface,
                                        const QStringList &configuredDrivers);

} // namespace NetworkInterfaceUtils

#endif // NETWORK_INTERFACE_UTILS_HPP
