// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "PCAP.hpp"

#include "ConsoleConnector.hpp"
#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"
#include "ResponseBuilder.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QAbstractSocket>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QHostAddress>
#include <QStringList>
#include <QTemporaryFile>

#include <stdexcept>

namespace {
constexpr char kTcpdumpTemplate[] = "tcpdump -i <interface> -w <file_name> <filter>";
constexpr char kStopTemplate[] = "true";
constexpr char kPlaceholderInterface[] = "<interface>";
constexpr char kPlaceholderFileName[] = "<file_name>";
constexpr char kPcapTmpFileName[] = "pcap_XXXXXX.pcap";
constexpr char kConfPcapPowerlineDrivers[] = "pcap_powerline_drivers";
constexpr char kInterfaceAny[] = "any";
constexpr char kKeyInterfaces[] = "interfaces";
constexpr char kKeyName[] = "name";
constexpr char kKeyUp[] = "up";
constexpr char kKeyRunning[] = "running";
constexpr char kKeyLoopback[] = "loopback";
constexpr char kKeyBridgeMember[] = "bridge_member";
constexpr char kKeyDriver[] = "driver";
constexpr char kKeyHasIpv4[] = "has_ipv4";
constexpr char kKeyHasIpv6LinkLocal[] = "has_ipv6_link_local";
constexpr char kKeyHasIpv6Configured[] = "has_ipv6_configured";
constexpr char kKeyAvailable[] = "available";
constexpr char kKeyLikelyPowerline[] = "likely_powerline";
constexpr char kKeyRecommendation[] = "recommendation";
constexpr char kKeyWarning[] = "warning";

QStringList configuredPowerlineDrivers() {
    const QString value = readBackendConfigValue(QLatin1String(kConfPcapPowerlineDrivers));
    QStringList drivers;
    for (const QString &driver : value.split(QLatin1Char(','))) {
        const QString trimmedDriver = driver.trimmed();
        if (!trimmedDriver.isEmpty()) {
            drivers.append(trimmedDriver);
        }
    }
    return drivers;
}

bool hasLinkLocalIpv6(const QNetworkInterface &netIf) {
    const QHostAddress linkLocalSubnet(QStringLiteral("fe80::"));
    for (const QNetworkAddressEntry &entry : netIf.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol &&
            entry.ip().isInSubnet(linkLocalSubnet, 10)) {
            return true;
        }
    }
    return false;
}

bool hasIpv4(const QNetworkInterface &netIf) {
    for (const QNetworkAddressEntry &entry : netIf.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            return true;
        }
    }
    return false;
}

bool hasConfiguredIpv6(const QNetworkInterface &netIf) {
    for (const QNetworkAddressEntry &entry : netIf.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol &&
            !entry.ip().isInSubnet(QHostAddress(QStringLiteral("fe80::")), 10)) {
            return true;
        }
    }
    return false;
}

QString interfaceDriver(const QString &name) {
    const QFileInfo driverInfo(QStringLiteral("/sys/class/net/%1/device/driver").arg(name));
    if (!driverInfo.isSymLink()) {
        return QString();
    }
    return QFileInfo(driverInfo.symLinkTarget()).fileName();
}

bool isBridgeMember(const QString &name) {
    return QFileInfo(QStringLiteral("/sys/class/net/%1/master").arg(name)).isSymLink();
}

QJsonObject interfaceDescription(const QNetworkInterface &netIf) {
    const auto flags = netIf.flags();
    const bool loopback = flags.testFlag(QNetworkInterface::IsLoopBack);
    const bool up = flags.testFlag(QNetworkInterface::IsUp);
    const bool running = flags.testFlag(QNetworkInterface::IsRunning);
    const bool bridgeMember = isBridgeMember(netIf.name());
    const bool ipv4 = hasIpv4(netIf);
    const bool ipv6LinkLocal = hasLinkLocalIpv6(netIf);
    const bool ipv6Configured = hasConfiguredIpv6(netIf);
    const QString driver = interfaceDriver(netIf.name());
    const bool driverMatches = configuredPowerlineDrivers().contains(driver, Qt::CaseInsensitive);
    const bool operational = up && running;
    const bool likelyPowerline = !loopback && !bridgeMember && operational && ipv6LinkLocal && !ipv4 &&
        !ipv6Configured && driverMatches;

    QJsonObject description{
        {QLatin1String(kKeyName), netIf.name()},
        {QLatin1String(kKeyUp), up},
        {QLatin1String(kKeyRunning), running},
        {QLatin1String(kKeyLoopback), loopback},
        {QLatin1String(kKeyBridgeMember), bridgeMember},
        {QLatin1String(kKeyDriver), driver},
        {QLatin1String(kKeyHasIpv4), ipv4},
        {QLatin1String(kKeyHasIpv6LinkLocal), ipv6LinkLocal},
        {QLatin1String(kKeyHasIpv6Configured), ipv6Configured},
        {QLatin1String(kKeyAvailable), operational},
        {QLatin1String(kKeyLikelyPowerline), likelyPowerline},
    };
    if (likelyPowerline) {
        description.insert(QLatin1String(kKeyRecommendation),
                           QStringLiteral("Likely PLC/HomePlug interface"));
    }
    if (loopback) {
        description.insert(QLatin1String(kKeyWarning),
                           QStringLiteral("Records local loopback traffic and is usually only useful for MQTT, if configured to use TCP"));
    } else if (bridgeMember) {
        description.insert(QLatin1String(kKeyWarning),
                           QStringLiteral("This interface is a member of a bridge"));
    } else if (!operational) {
        description.insert(QLatin1String(kKeyWarning), QStringLiteral("Interface is down or has no carrier"));
    }
    return description;
}

QJsonObject anyInterfaceDescription() {
    return QJsonObject{
        {QLatin1String(kKeyName), QLatin1String(kInterfaceAny)},
        {QLatin1String(kKeyUp), true},
        {QLatin1String(kKeyRunning), true},
        {QLatin1String(kKeyLoopback), false},
        {QLatin1String(kKeyBridgeMember), false},
        {QLatin1String(kKeyDriver), QString()},
        {QLatin1String(kKeyHasIpv4), false},
        {QLatin1String(kKeyHasIpv6LinkLocal), false},
        {QLatin1String(kKeyHasIpv6Configured), false},
        {QLatin1String(kKeyAvailable), true},
        {QLatin1String(kKeyLikelyPowerline), false},
        {QLatin1String(kKeyWarning), QStringLiteral("May record a very large amount of unrelated traffic")},
    };
}

QHash<QString, QString> buildPcapValues(const QString &iface,
                                         const QString &filePath,
                                         const QString &filter) {
    QHash<QString, QString> values;
    values.insert(QLatin1String(kPlaceholderInterface), iface);
    values.insert(QLatin1String(kPlaceholderFileName), filePath);
    values.insert(QLatin1String("<filter>"), filter);
    return values;
}

QString createTempPcapFile() {
    QTemporaryFile tempFile(QDir::tempPath() + QLatin1String("/") + QLatin1String(kPcapTmpFileName));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        return QString();
    }

    const QString filePath = tempFile.fileName();
    tempFile.close();
    return filePath;
}

bool isInterfaceAvailable(const QString &iface) {
    if (iface == QLatin1String(kInterfaceAny)) {
        return true;
    }

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &netIf : interfaces) {
        if (netIf.name() == iface) {
            return netIf.flags().testFlag(QNetworkInterface::IsUp);
        }
    }

    return false;
}
} // namespace

PCAP::PCAP(QObject *parent)
    : QObject(parent), m_console(new ConsoleConnector(this)) {}

ModuleResponse PCAP::handleRequest(const ModuleRequest &request) {
    switch (toPcapAction(request.action)) {
    case PCAPAction::ReadInterfaces:
        return handleReadInterfacesRequest(request);
    case PCAPAction::Read:
        return handleReadRequest(request);
    case PCAPAction::Write:
        return handleWriteRequest(request);
    case PCAPAction::Unknown:
        throw std::runtime_error("PCAP::handleRequest got unsupported action");
    }

    throw std::runtime_error("PCAP::handleRequest reached unreachable code");
}

ModuleResponse PCAP::handleReadInterfacesRequest(const ModuleRequest &request) {
    QJsonArray interfaces;
    interfaces.append(anyInterfaceDescription());
    for (const QNetworkInterface &netIf : QNetworkInterface::allInterfaces()) {
        interfaces.append(interfaceDescription(netIf));
    }

    return ModuleResponse{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupPcap),
        .action = request.action,
        .parameters = QJsonObject{{QLatin1String(kKeyInterfaces), interfaces}},
        .success = true,
        .final = true,
    };
}

ModuleResponse PCAP::handleReadRequest(const ModuleRequest &request) {
    return readCapture(request);
}

ModuleResponse PCAP::handleWriteRequest(const ModuleRequest &request) {
    return startCapture(request);
}

void PCAP::enqueueRequest(const ModuleRequest &request) {
    m_queue.enqueue(request);
    processQueue();
}

void PCAP::processQueue() {
    while (!m_busy && !m_queue.isEmpty()) {
        const ModuleRequest next = m_queue.dequeue();
        const ModuleResponse response = handleRequest(next);
        emit responseReady(ResponseBuilder::buildResponse(response));
    }
}

ModuleResponse PCAP::startCapture(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupPcap),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    if (m_busy || m_recording) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorPcapBusy)},
        };
        return response;
    }

    const QString iface = extractInterface(request);
    if (iface.isEmpty() || !isInterfaceAvailable(iface)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorInvalidParams)},
        };
        return response;
    }

    const QString filter = extractFilter(request);

    m_busy = true;
    const QString tempPath = createTempPcapFile();
    if (tempPath.isEmpty()) {
        m_busy = false;
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorFileIoFailed)},
        };
        return response;
    }
    m_lastFile = tempPath;

    ConsoleConnector::ExecOptions options;
    options.stop = false;
    const ConsoleConnector::RunResult result = m_console->executeTemplate(
        QLatin1String(kTcpdumpTemplate),
        buildPcapValues(iface, m_lastFile, filter),
        options,
        ConsoleConnector::ExecMode::Async);
    if (result.exitCode != 0) {
        m_busy = false;
        QFile::remove(m_lastFile);
        m_lastFile.clear();
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorInvalidParams)},
        };
        return response;
    }

    m_recording = true;
    m_busy = false;
    response.success = true;
    return response;
}

ModuleResponse PCAP::readCapture(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupPcap),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    if (m_busy) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorPcapBusy)},
        };
        return response;
    }
    if (!m_recording || m_lastFile.isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorNotRecording)},
        };
        return response;
    }

    m_busy = true;
    ConsoleConnector::ExecOptions options;
    options.stop = true;
    const ConsoleConnector::RunResult result = m_console->executeTemplate(
        QLatin1String(kStopTemplate),
        QHash<QString, QString>(),
        options,
        ConsoleConnector::ExecMode::Async);
    if (result.exitCode != 0) {
        m_busy = false;
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorPcapStopFailed)},
        };
        return response;
    }

    QFile file(m_lastFile);
    if (!file.open(QIODevice::ReadOnly)) {
        m_busy = false;
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorFileIoFailed)},
        };
        return response;
    }

    const QByteArray encoded = file.readAll().toBase64(QByteArray::Base64Encoding);
    const QString pcapFileName = fileNameFromPath(m_lastFile);

    file.close();
    QFile::remove(m_lastFile);

    response.parameters = QJsonObject{
        {QLatin1String(kKeyFile), pcapFileName},
        {QLatin1String(kKeyDataB64), QString::fromLatin1(encoded)},
    };
    response.success = true;

    m_lastFile.clear();
    m_recording = false;
    m_busy = false;
    return response;
}

PCAPAction PCAP::toPcapAction(const QString &action) {
    if (action == QLatin1String(kActionReadInterfaces)) {
        return PCAPAction::ReadInterfaces;
    }
    if (action == QLatin1String(kActionRead)) {
        return PCAPAction::Read;
    }

    if (action == QLatin1String(kActionWrite)) {
        return PCAPAction::Write;
    }

    return PCAPAction::Unknown;
}

QString PCAP::extractInterface(const ModuleRequest &request) {
    const QJsonObject parameters = request.parameters;
    const QJsonObject general = parameters.value(QLatin1String(kKeyGeneral)).toObject();
    return general.value(QLatin1String(kKeyInterface)).toString().trimmed();
}

QString PCAP::extractFilter(const ModuleRequest &request) {
    const QJsonObject general = request.parameters.value(QLatin1String(kKeyGeneral)).toObject();
    const QJsonObject filters = general.value(QLatin1String(kKeyFilters)).toObject();
    if (!filters.value(QLatin1String(kKeyV2gHlc)).toBool()) {
        return QString();
    }
    return QStringLiteral("ip6 or ether proto 0x88e1");
}

QString PCAP::fileNameFromPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return QString();
    }

    return QFileInfo(filePath).fileName();
}
