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
#include <QDebug>
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
constexpr char kConfPcapMaxSizeBytes[] = "pcap_max_size_bytes";
constexpr char kConfPcapMaxDurationSeconds[] = "pcap_max_duration_seconds";
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
    : QObject(parent), m_console(new ConsoleConnector(this)), m_limitTimer(new QTimer(this)) {
    connect(m_console, &ConsoleConnector::streamingFinished, this, &PCAP::handleCaptureFinished);
    m_maxSizeBytes = configuredLimit(QLatin1String(kConfPcapMaxSizeBytes), 100 * 1024 * 1024);
    m_maxDurationSeconds = configuredLimit(QLatin1String(kConfPcapMaxDurationSeconds), 15 * 60);
    connect(m_limitTimer, &QTimer::timeout, this, &PCAP::checkCaptureLimits);
    m_limitTimer->setInterval(1000);
}

void PCAP::handleCaptureFinished(const ConsoleConnector::RunResult &result) {
    if (m_state == PCAPState::Starting) {
        const QString details = QString::fromLocal8Bit(result.stderrData).trimmed();
        cleanupCapture(true);
        m_startFailureDetails = details;
        return;
    }
    if (m_state != PCAPState::Recording) {
        return;
    }

    const QString details = QString::fromLocal8Bit(result.stderrData).trimmed();
    qWarning().noquote() << "tcpdump exited before capture was stopped"
                         << "with code" << result.exitCode
                         << (details.isEmpty() ? QString() : QStringLiteral(": ") + details);

    const qint64 requestId = m_captureRequestId;
    cleanupCapture(true);
    emit responseReady(ResponseBuilder::buildResponse(ModuleResponse{
        .requestId = requestId,
        .group = QLatin1String(kGroupPcap),
        .action = QLatin1String(kActionWrite),
        .parameters = captureError(kErrorPcapCaptureFailed, details, requestId).parameters,
        .success = false,
        .final = true,
    }));
}

qint64 PCAP::configuredLimit(const QString &key, qint64 defaultValue) {
    bool ok = false;
    const qint64 value = readBackendConfigValue(key).toLongLong(&ok);
    if (!ok || value <= 0) {
        qWarning() << "Invalid or missing PCAP limit" << key << "; using" << defaultValue;
        return defaultValue;
    }
    return value;
}

ModuleResponse PCAP::captureError(const char *error, const QString &details, qint64 requestId) const {
    QJsonObject parameters{
        {QLatin1String(kError), QLatin1String(error)},
    };
    if (!details.isEmpty()) {
        parameters.insert(QLatin1String(kKeyDetails), details);
    }
    return ModuleResponse{
        .requestId = requestId >= 0 ? requestId : m_captureRequestId,
        .group = QLatin1String(kGroupPcap),
        .action = QLatin1String(kActionWrite),
        .parameters = parameters,
        .success = false,
        .final = true,
    };
}

void PCAP::cleanupCapture(bool removeFile) {
    m_limitTimer->stop();
    if (removeFile && !m_lastFile.isEmpty()) {
        QFile::remove(m_lastFile);
    }
    m_lastFile.clear();
    m_captureRequestId = 0;
    m_startFailureDetails.clear();
    m_captureElapsed.invalidate();
    m_state = PCAPState::Idle;
    m_busy = false;
}

void PCAP::checkCaptureLimits() {
    if (m_state != PCAPState::Recording) {
        return;
    }

    if (QFileInfo(m_lastFile).size() >= m_maxSizeBytes) {
        stopCaptureForLimit(QStringLiteral("size"));
        return;
    }
    if (m_captureElapsed.isValid() &&
        m_captureElapsed.elapsed() >= m_maxDurationSeconds * 1000) {
        stopCaptureForLimit(QStringLiteral("duration"));
    }
}

void PCAP::stopCaptureForLimit(const QString &limitName) {
    if (m_state != PCAPState::Recording) {
        return;
    }

    m_state = PCAPState::Stopping;
    m_busy = true;
    ConsoleConnector::ExecOptions options;
    options.stop = true;
    const ConsoleConnector::RunResult result = m_console->executeTemplate(
        QLatin1String(kStopTemplate), {}, options, ConsoleConnector::ExecMode::Async);
    if (result.exitCode != 0) {
        const qint64 requestId = m_captureRequestId;
        cleanupCapture(true);
        emit responseReady(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = requestId,
            .group = QLatin1String(kGroupPcap),
            .action = QLatin1String(kActionWrite),
            .parameters = captureError(kErrorPcapCaptureFailed,
                                        QStringLiteral("Failed to stop after %1 limit").arg(limitName),
                                        requestId).parameters,
            .success = false,
            .final = true,
        }));
        processQueue();
        return;
    }

    m_limitTimer->stop();
    m_state = PCAPState::Ready;
    m_busy = false;
    QJsonObject parameters = captureError(kErrorPcapLimitReached, limitName).parameters;
    parameters.insert(QLatin1String(kKeyLimit), limitName);
    emit responseReady(ResponseBuilder::buildResponse(ModuleResponse{
        .requestId = m_captureRequestId,
        .group = QLatin1String(kGroupPcap),
        .action = QLatin1String(kActionWrite),
        .parameters = parameters,
        .success = false,
        .final = true,
    }));
    processQueue();
}

void PCAP::handleClientDisconnected() {
    m_queue.clear();
    if (m_state == PCAPState::Recording || m_state == PCAPState::Starting) {
        m_state = PCAPState::Stopping;
        ConsoleConnector::ExecOptions options;
        options.stop = true;
        m_console->executeTemplate(QLatin1String(kStopTemplate), {}, options,
                                    ConsoleConnector::ExecMode::Async);
    }
    cleanupCapture(true);
}

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

    if (m_busy || m_state != PCAPState::Idle) {
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
    m_captureRequestId = request.requestId;
    m_state = PCAPState::Starting;
    m_startFailureDetails.clear();

    ConsoleConnector::ExecOptions options;
    options.stop = false;
    const ConsoleConnector::RunResult result = m_console->executeTemplate(
        QLatin1String(kTcpdumpTemplate),
        buildPcapValues(iface, m_lastFile, filter),
        options,
        ConsoleConnector::ExecMode::StreamingAsync);
    if (result.exitCode != 0) {
        const QString details = m_startFailureDetails;
        cleanupCapture(true);
        return captureError(kErrorPcapCaptureFailed, details, request.requestId);
    }
    if (m_state != PCAPState::Starting) {
        return captureError(kErrorPcapCaptureFailed, m_startFailureDetails, request.requestId);
    }

    m_state = PCAPState::Recording;
    m_busy = false;
    m_captureElapsed.start();
    m_limitTimer->start();
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
    if ((m_state != PCAPState::Recording && m_state != PCAPState::Ready) || m_lastFile.isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorNotRecording)},
        };
        return response;
    }

    if (m_state == PCAPState::Recording) {
        m_state = PCAPState::Stopping;
        m_busy = true;
        ConsoleConnector::ExecOptions options;
        options.stop = true;
        const ConsoleConnector::RunResult result = m_console->executeTemplate(
            QLatin1String(kStopTemplate), {}, options, ConsoleConnector::ExecMode::Async);
        if (result.exitCode != 0) {
            cleanupCapture(true);
            return response = captureError(kErrorPcapStopFailed, {}, request.requestId);
        }
    }

    return readCaptureFile(request);
}

ModuleResponse PCAP::readCaptureFile(const ModuleRequest &request) {
    m_busy = true;
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupPcap),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };
    QFile file(m_lastFile);
    if (!file.open(QIODevice::ReadOnly)) {
        cleanupCapture(true);
        return ModuleResponse{
            .requestId = request.requestId,
            .group = QLatin1String(kGroupPcap),
            .action = request.action,
            .parameters = QJsonObject{{QLatin1String(kError), QLatin1String(kErrorFileIoFailed)}},
            .success = false,
            .final = true,
        };
    }

    const QByteArray encoded = file.readAll().toBase64(QByteArray::Base64Encoding);
    const QString pcapFileName = fileNameFromPath(m_lastFile);

    if (file.error() != QFile::NoError) {
        file.close();
        cleanupCapture(true);
        return ModuleResponse{
            .requestId = request.requestId,
            .group = QLatin1String(kGroupPcap),
            .action = request.action,
            .parameters = QJsonObject{{QLatin1String(kError), QLatin1String(kErrorFileIoFailed)}},
            .success = false,
            .final = true,
        };
    }

    file.close();
    QFile::remove(m_lastFile);

    response.parameters = QJsonObject{
        {QLatin1String(kKeyFile), pcapFileName},
        {QLatin1String(kKeyDataB64), QString::fromLatin1(encoded)},
    };
    response.success = true;

    cleanupCapture(true);
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
