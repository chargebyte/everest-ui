// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "NetworkConfiguration.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"

#include <QFile>
#include <QFileInfo>
#include <QAbstractSocket>
#include <QDebug>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QList>
#include <QStringList>

#include <functional>
#include <stdexcept>

namespace {
constexpr char kConfigAvailableFeatures[] = "available_features";
constexpr char kParameterAvailable[] = "available";
constexpr char kParameterInterfaces[] = "interfaces";
constexpr char kParameterInterface[] = "interface";
constexpr char kParameterNetworkFile[] = "network_file";
constexpr char kParameterEditable[] = "editable";
constexpr char kParameterUserOverride[] = "user_override";
constexpr char kParameterWarning[] = "warning";
constexpr char kParameterName[] = "name";
constexpr char kParameterIndex[] = "index";
constexpr char kParameterKind[] = "kind";
constexpr char kParameterOperationalState[] = "operational_state";
constexpr char kParameterSetupState[] = "setup_state";
constexpr char kParameterDriver[] = "driver";
constexpr char kParameterBridgeMember[] = "bridge_member";
constexpr char kParameterLoopback[] = "loopback";
constexpr char kParameterProbablyPlc[] = "probably_plc";
constexpr char kParameterDhcpIpv4[] = "dhcp_ipv4";
constexpr char kParameterDhcpIpv6[] = "dhcp_ipv6";
constexpr char kParameterIpv4Addresses[] = "ipv4_addresses";
constexpr char kParameterGateway[] = "gateway";
constexpr char kParameterDns[] = "dns";
constexpr char kErrorUnavailable[] = "network_configuration_unavailable";
constexpr char kErrorInvalidInterface[] = "invalid_interface";
constexpr char kErrorStatusFailed[] = "network_status_failed";
constexpr char kErrorListFailed[] = "network_list_failed";
constexpr char kErrorNetworkFileNotFound[] = "network_file_not_found";
constexpr char kErrorUnsupportedNetworkFile[] = "unsupported_network_file";
constexpr char kErrorUnsupportedConfiguration[] = "unsupported_network_configuration";
constexpr char kErrorInvalidSettings[] = "invalid_network_settings";
constexpr char kErrorWriteFailed[] = "network_config_write_failed";
constexpr char kErrorApplyFailed[] = "network_config_apply_failed";
constexpr char kNetworkFileEtc[] = "/etc/systemd/network/";
constexpr char kNetworkFileLib[] = "/lib/systemd/network/";
constexpr char kNetworkFileUsrLib[] = "/usr/lib/systemd/network/";

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif

struct CommandResult {
    bool started = false;
    int exitCode = -1;
    QByteArray output;
};

struct InterfaceInfo {
    QString name;
    int index = 0;
    QString kind;
    QString operationalState;
    QString setupState;
    QString driver;
    QString networkFile;
    bool bridgeMember = false;
    bool loopback = false;
    bool probablyPlc = false;
};

struct NetworkDocument {
    QStringList lines;
};

struct NetworkFileAnalysis {
    bool supported = true;
    QString warning;
};

CommandResult runCommand(const QString &program, const QStringList &arguments) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }

    return {
        true,
        process.exitCode(),
        process.readAllStandardOutput(),
    };
}

using CommandRunner = std::function<CommandResult(const QString &, const QStringList &)>;

bool applyNetworkConfiguration(const CommandRunner &run) {
    const CommandResult reload = run(QStringLiteral("networkctl"), {QStringLiteral("reload")});
    return reload.started && reload.exitCode == 0;
}

bool featureAvailable(const QString &feature) {
    QFile configFile(resolveBackendConfigPath());
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;
    }

    bool featureListPresent = false;
    QString configured;
    QTextStream stream(&configFile);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0 || line.left(separator).trimmed() != QLatin1String(kConfigAvailableFeatures)) {
            continue;
        }
        featureListPresent = true;
        configured = line.mid(separator + 1).trimmed();
        break;
    }
    if (!featureListPresent) {
        return true;
    }

    for (const QString &entry : configured.split(QLatin1Char(','), kSkipEmptyParts)) {
        if (entry.trimmed() == feature) {
            return true;
        }
    }
    return false;
}

QString sysfsDriver(const QString &name) {
    const QFileInfo driverInfo(QStringLiteral("/sys/class/net/") + name + QStringLiteral("/device/driver"));
    if (!driverInfo.isSymLink()) {
        return {};
    }
    return QFileInfo(driverInfo.symLinkTarget()).fileName();
}

bool hasBridgeMaster(const QString &name) {
    return QFileInfo(QStringLiteral("/sys/class/net/") + name + QStringLiteral("/master")).isSymLink();
}

QStringList configuredPlcDrivers() {
    const QString configured = readBackendConfigValue(QStringLiteral("pcap_powerline_drivers"));
    QStringList drivers;
    for (const QString &driver : configured.split(QLatin1Char(','), kSkipEmptyParts)) {
        drivers.append(driver.trimmed());
    }
    return drivers;
}

bool validInterfaceName(const QString &name) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.:@-]{1,15}$"));
    return pattern.match(name).hasMatch() && QNetworkInterface::interfaceFromName(name).isValid();
}

QString networkFileFromStatus(const QString &name, bool &ok) {
    const CommandResult result = runCommand(QStringLiteral("networkctl"),
                                             {QStringLiteral("status"), QStringLiteral("--no-pager"),
                                              QStringLiteral("--full"), name});
    ok = result.started && result.exitCode == 0;
    if (!ok) {
        return {};
    }

    const QString output = QString::fromLocal8Bit(result.output);
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("Network File:"))) {
            const QString path = trimmed.mid(QStringLiteral("Network File:").size()).trimmed();
            if (path == QStringLiteral("n/a") || path == QStringLiteral("-") ||
                path == QStringLiteral("unknown")) {
                return {};
            }
            return path;
        }
    }
    return {};
}

QString canonicalNetworkFilePath(const QString &path) {
    return QFileInfo(path).canonicalFilePath();
}

bool isAllowedReadPath(const QString &path) {
    const QString cleanPath = canonicalNetworkFilePath(path);
    if (cleanPath.isEmpty()) {
        return false;
    }
    return cleanPath.startsWith(QLatin1String(kNetworkFileEtc)) ||
           cleanPath.startsWith(QLatin1String(kNetworkFileLib)) ||
           cleanPath.startsWith(QLatin1String(kNetworkFileUsrLib));
}

QString userNetworkFilePath(const QString &name) {
    return QString::fromLatin1(kNetworkFileEtc) + name + QStringLiteral(".network");
}

QStringList networkFileRoots() {
    return {QLatin1String(kNetworkFileEtc), QLatin1String(kNetworkFileLib),
            QLatin1String(kNetworkFileUsrLib)};
}

NetworkFileAnalysis analyzeNetworkFile(const QString &path) {
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo fileInfo(path);
    const QString dropInName = fileInfo.fileName() + QStringLiteral(".d");
    for (const QString &root : networkFileRoots()) {
        const QDir dropInDirectory(root + dropInName);
        if (!dropInDirectory.exists()) {
            continue;
        }
        const QStringList dropIns = dropInDirectory.entryList(QDir::Files);
        if (!dropIns.isEmpty()) {
            return {false, QStringLiteral("The effective network configuration has drop-ins that the Web UI cannot safely edit.")};
        }
    }
    return {};
}

bool isIpv4Cidr(const QString &value) {
    const int slash = value.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == value.size() - 1) {
        return false;
    }

    QHostAddress address;
    bool validPrefix = false;
    const int prefix = value.mid(slash + 1).toInt(&validPrefix);
    return validPrefix && prefix >= 0 && prefix <= 32 &&
           address.setAddress(value.left(slash)) && address.protocol() == QAbstractSocket::IPv4Protocol;
}

bool isIpv4Address(const QString &value) {
    QHostAddress address;
    return address.setAddress(value) && address.protocol() == QAbstractSocket::IPv4Protocol;
}

QString keyName(const QString &line, QString &value) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) ||
        trimmed.startsWith(QLatin1Char(';'))) {
        return {};
    }

    const int equals = trimmed.indexOf(QLatin1Char('='));
    if (equals <= 0) {
        return {};
    }
    value = trimmed.mid(equals + 1).trimmed();
    return trimmed.left(equals).trimmed();
}

NetworkDocument readDocument(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return {QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))};
}

bool factoryNetworkFileMatches(const QString &path, const QString &interfaceName) {
    const NetworkDocument document = readDocument(path);
    QString section;
    QStringList names;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        if (section.compare(QStringLiteral("Match"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        QString value;
        if (keyName(line, value).compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0) {
            names.append(value);
        }
    }
    if (names.isEmpty()) {
        return true;
    }
    for (const QString &name : names) {
        if (QDir::match(name, interfaceName)) {
            return true;
        }
    }
    return false;
}

QString factoryNetworkFilePath(const QString &interfaceName) {
    for (const QString &root : {QLatin1String(kNetworkFileLib), QLatin1String(kNetworkFileUsrLib)}) {
        const QDir directory(root);
        const QStringList files = directory.entryList(QStringList{QStringLiteral("*.network")}, QDir::Files,
                                                       QDir::Name);
        for (const QString &file : files) {
            const QString path = directory.filePath(file);
            if (factoryNetworkFileMatches(path, interfaceName)) {
                return path;
            }
        }
    }
    return {};
}

QJsonObject parseDocument(const NetworkDocument &document, const QString &name, const QString &path) {
    QString section;
    QString dhcp;
    QString gateway;
    QStringList addresses;
    QStringList dns;

    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }

        QString value;
        const QString key = keyName(line, value);
        if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 &&
            key.compare(QStringLiteral("DHCP"), Qt::CaseInsensitive) == 0) {
            dhcp = value;
        } else if ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                    section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) &&
                   key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
                   isIpv4Cidr(value)) {
            addresses.append(value);
        } else if ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                    section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0) &&
                   key.compare(QStringLiteral("Gateway"), Qt::CaseInsensitive) == 0 &&
                   isIpv4Address(value)) {
            gateway = value;
        } else if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 &&
                   key.compare(QStringLiteral("DNS"), Qt::CaseInsensitive) == 0 &&
                   isIpv4Address(value)) {
            dns.append(value);
        }
    }

    QJsonArray addressArray;
    for (const QString &address : addresses) {
        addressArray.append(address);
    }
    QJsonArray dnsArray;
    for (const QString &server : dns) {
        dnsArray.append(server);
    }

    const QString normalizedDhcp = dhcp.toLower();
    const bool dhcpIpv4 = normalizedDhcp == QStringLiteral("yes") ||
                          normalizedDhcp == QStringLiteral("true") ||
                          normalizedDhcp == QStringLiteral("ipv4");
    const bool dhcpIpv6 = normalizedDhcp == QStringLiteral("yes") ||
                          normalizedDhcp == QStringLiteral("true") ||
                          normalizedDhcp == QStringLiteral("ipv6");

    return {
        {QLatin1String(kParameterInterface), name},
        {QLatin1String(kParameterNetworkFile), path},
        {QLatin1String(kParameterDhcpIpv4), dhcpIpv4},
        {QLatin1String(kParameterDhcpIpv6), dhcpIpv6},
        {QLatin1String(kParameterIpv4Addresses), addressArray},
        {QLatin1String(kParameterGateway), gateway},
        {QLatin1String(kParameterDns), dnsArray},
    };
}

void replaceOwnedKeys(NetworkDocument &document, const QJsonObject &settings) {
    QStringList replacement;
    const bool dhcpIpv4 = settings.value(QLatin1String(kParameterDhcpIpv4)).toBool();
    const bool dhcpIpv6 = settings.value(QLatin1String(kParameterDhcpIpv6)).toBool();
    QString dhcpValue = QStringLiteral("no");
    if (dhcpIpv4 && dhcpIpv6) {
        dhcpValue = QStringLiteral("yes");
    } else if (dhcpIpv4) {
        dhcpValue = QStringLiteral("ipv4");
    } else if (dhcpIpv6) {
        dhcpValue = QStringLiteral("ipv6");
    }
    replacement.append(QStringLiteral("DHCP=") + dhcpValue);

    if (!dhcpIpv4) {
        for (const QJsonValue &value : settings.value(QLatin1String(kParameterIpv4Addresses)).toArray()) {
            replacement.append(QStringLiteral("Address=") + value.toString());
        }
        const QString gateway = settings.value(QLatin1String(kParameterGateway)).toString();
        if (!gateway.isEmpty()) {
            replacement.append(QStringLiteral("Gateway=") + gateway);
        }
    }
    for (const QJsonValue &value : settings.value(QLatin1String(kParameterDns)).toArray()) {
        replacement.append(QStringLiteral("DNS=") + value.toString());
    }

    QStringList output;
    QString section;
    bool inserted = false;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 && !inserted) {
                bool hadSectionSeparator = false;
                while (!output.isEmpty() && output.constLast().isEmpty()) {
                    output.removeLast();
                    hadSectionSeparator = true;
                }
                output.append(replacement);
                if (hadSectionSeparator) {
                    output.append(QString());
                }
                inserted = true;
            }
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            output.append(line);
            continue;
        }

        if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
            section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 ||
            section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0) {
            QString value;
            const QString key = keyName(line, value);
            const bool isIpv4OwnedKey =
                ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                  section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) &&
                 key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
                 isIpv4Cidr(value)) ||
                ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                  section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0) &&
                 key.compare(QStringLiteral("Gateway"), Qt::CaseInsensitive) == 0 &&
                 isIpv4Address(value)) ||
                (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 &&
                 key.compare(QStringLiteral("DNS"), Qt::CaseInsensitive) == 0 &&
                 isIpv4Address(value));
            if ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 &&
                 key.compare(QStringLiteral("DHCP"), Qt::CaseInsensitive) == 0) || isIpv4OwnedKey) {
                continue;
            }
        }
        output.append(line);
    }

    if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) != 0) {
        if (!output.isEmpty() && !output.constLast().isEmpty()) {
            output.append(QString());
            output.append(QStringLiteral("[Network]"));
        } else {
            output.append(QStringLiteral("[Network]"));
        }
    }
    if (!inserted) {
        while (!output.isEmpty() && output.constLast().isEmpty()) {
            output.removeLast();
        }
        output.append(replacement);
    }
    while (!output.isEmpty() && output.constLast().isEmpty()) {
        output.removeLast();
    }
    document.lines = output;
}

void restrictMatchToInterface(NetworkDocument &document, const QString &interfaceName) {
    QStringList output;
    QString section;
    bool matchFound = false;
    bool skippingMatch = false;

    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            if (section.compare(QStringLiteral("Match"), Qt::CaseInsensitive) == 0) {
                output.append(QStringLiteral("[Match]"));
                output.append(QStringLiteral("Name=") + interfaceName);
                matchFound = true;
                skippingMatch = true;
                continue;
            }
            skippingMatch = false;
        }
        if (!skippingMatch) {
            output.append(line);
        }
    }

    if (!matchFound) {
        output.prepend(QStringLiteral("Name=") + interfaceName);
        output.prepend(QStringLiteral("[Match]"));
        output.insert(2, QString());
    }
    document.lines = output;
}

bool validateSettings(const QJsonObject &settings, QString &error) {
    if (!settings.value(QLatin1String(kParameterDhcpIpv4)).isBool()) {
        error = QStringLiteral("dhcp_ipv4 must be boolean");
        return false;
    }
    if (settings.contains(QLatin1String(kParameterDhcpIpv6)) &&
        !settings.value(QLatin1String(kParameterDhcpIpv6)).isBool()) {
        error = QStringLiteral("dhcp_ipv6 must be boolean");
        return false;
    }

    const QJsonArray addresses = settings.value(QLatin1String(kParameterIpv4Addresses)).toArray();
    if (addresses.size() > 2) {
        error = QStringLiteral("at most two IPv4 addresses are supported");
        return false;
    }
    for (const QJsonValue &address : addresses) {
        if (!address.isString() || !isIpv4Cidr(address.toString())) {
            error = QStringLiteral("invalid IPv4 address");
            return false;
        }
    }
    if (!settings.value(QLatin1String(kParameterGateway)).isString() ||
        (!settings.value(QLatin1String(kParameterGateway)).toString().isEmpty() &&
         !isIpv4Address(settings.value(QLatin1String(kParameterGateway)).toString()))) {
        error = QStringLiteral("invalid IPv4 gateway");
        return false;
    }
    for (const QJsonValue &server : settings.value(QLatin1String(kParameterDns)).toArray()) {
        if (!server.isString() || !isIpv4Address(server.toString())) {
            error = QStringLiteral("invalid DNS server");
            return false;
        }
    }
    if (!settings.value(QLatin1String(kParameterDhcpIpv4)).toBool() && addresses.isEmpty()) {
        error = QStringLiteral("a static IPv4 address is required when DHCP is disabled");
        return false;
    }
    return true;
}

QJsonObject interfaceObject(const InterfaceInfo &info) {
    QStringList warnings;
    if (info.loopback) {
        warnings.append(QStringLiteral("Loopback traffic is local to the target and is normally not a management interface."));
    }
    if (info.probablyPlc) {
        warnings.append(QStringLiteral("This interface probably belongs to a PLC/HomePlug adapter."));
    }
    if (info.bridgeMember) {
        warnings.append(QStringLiteral("This interface is a bridge member; configure the bridge when appropriate."));
    }
    if (info.kind.compare(QStringLiteral("can"), Qt::CaseInsensitive) == 0) {
        warnings.append(QStringLiteral("CAN interfaces do not use IPv4 network configuration."));
    }
    if (!info.networkFile.isEmpty() && !isAllowedReadPath(info.networkFile)) {
        warnings.append(QStringLiteral("The effective network file is outside the locations supported by the Web UI."));
    }

    QJsonArray warningArray;
    for (const QString &warning : warnings) {
        warningArray.append(warning);
    }

    const bool special = info.loopback || info.kind.compare(QStringLiteral("can"), Qt::CaseInsensitive) == 0 ||
                         (!info.networkFile.isEmpty() && !isAllowedReadPath(info.networkFile));
    return {
        {QLatin1String(kParameterName), info.name},
        {QLatin1String(kParameterIndex), info.index},
        {QLatin1String(kParameterKind), info.kind},
        {QLatin1String(kParameterOperationalState), info.operationalState},
        {QLatin1String(kParameterSetupState), info.setupState},
        {QLatin1String(kParameterDriver), info.driver},
        {QLatin1String(kParameterNetworkFile), info.networkFile},
        {QLatin1String(kParameterBridgeMember), info.bridgeMember},
        {QLatin1String(kParameterLoopback), info.loopback},
        {QLatin1String(kParameterProbablyPlc), info.probablyPlc},
        {QLatin1String(kParameterEditable), !special},
        {QLatin1String(kParameterWarning), warningArray},
    };
}

QList<InterfaceInfo> readInterfaces(bool &success) {
    const CommandResult result = runCommand(QStringLiteral("networkctl"),
                                             {QStringLiteral("list"), QStringLiteral("--no-pager"),
                                              QStringLiteral("--no-legend"), QStringLiteral("--all")});
    success = result.started && result.exitCode == 0;
    if (!success) {
        return {};
    }

    const QStringList plcDrivers = configuredPlcDrivers();
    QList<InterfaceInfo> interfaces;
    for (const QString &line : QString::fromLocal8Bit(result.output).split(QLatin1Char('\n'))) {
        const QStringList fields = line.simplified().split(QLatin1Char(' '));
        if (fields.size() < 5 || !fields.at(0).toInt()) {
            continue;
        }
        InterfaceInfo info;
        info.index = fields.at(0).toInt();
        info.name = fields.at(1);
        info.kind = fields.at(2);
        info.operationalState = fields.at(fields.size() - 2);
        info.setupState = fields.constLast();
        info.driver = sysfsDriver(info.name);
        info.bridgeMember = hasBridgeMaster(info.name);
        info.loopback = info.name == QStringLiteral("lo");
        info.probablyPlc = plcDrivers.contains(info.driver);
        interfaces.append(info);
    }
    return interfaces;
}

ModuleResponse errorResponse(const ModuleRequest &request, const QString &error) {
    return {
        request.requestId, QLatin1String(kGroupNetwork), request.action,
        {{QLatin1String(kError), error}}, false, true
    };
}
} // namespace

namespace NetworkConfiguration {
ModuleResponse handleRequest(const ModuleRequest &request) {
    if (request.action == QLatin1String(kActionReadInterfaces)) {
        if (!featureAvailable(QStringLiteral("network"))) {
            return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                    {{QLatin1String(kParameterAvailable), false},
                     {QLatin1String(kParameterInterfaces), QJsonArray{}}},
                    true, true};
        }
        bool listSucceeded = false;
        const QList<InterfaceInfo> interfaces = readInterfaces(listSucceeded);
        if (!listSucceeded) {
            return errorResponse(request, QLatin1String(kErrorListFailed));
        }
        QJsonArray interfaceArray;
        for (const InterfaceInfo &info : interfaces) {
            interfaceArray.append(interfaceObject(info));
        }
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterAvailable), true},
                 {QLatin1String(kParameterInterfaces), interfaceArray}},
                true, true};
    }

    if (!featureAvailable(QStringLiteral("network"))) {
        return errorResponse(request, QLatin1String(kErrorUnavailable));
    }

    const QString interfaceName = request.parameters.value(QLatin1String(kParameterInterface)).toString();
    if (!validInterfaceName(interfaceName)) {
        return errorResponse(request, QLatin1String(kErrorInvalidInterface));
    }

    if (request.action == QLatin1String(kActionReadSettings)) {
        bool statusOk = false;
        const QString networkFile = networkFileFromStatus(interfaceName, statusOk);
        if (!statusOk) {
            qWarning() << "Failed to determine network settings file for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorStatusFailed));
        }
        if (!networkFile.isEmpty() && !isAllowedReadPath(networkFile)) {
            qWarning().noquote() << "Unsupported network settings file for" << interfaceName
                                 << ":" << networkFile
                                 << "canonical=" << canonicalNetworkFilePath(networkFile);
            return errorResponse(request, QLatin1String(kErrorUnsupportedNetworkFile));
        }
        const NetworkFileAnalysis analysis = analyzeNetworkFile(networkFile);
        if (!analysis.supported) {
            qWarning() << analysis.warning << "for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        const NetworkDocument document = networkFile.isEmpty() ? NetworkDocument{} : readDocument(networkFile);
        if (!networkFile.isEmpty() && document.lines.isEmpty()) {
            qWarning() << "Unable to read network settings file for" << interfaceName << ":" << networkFile;
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        QJsonObject parameters = document.lines.isEmpty()
                                           ? QJsonObject{
                                                 {QLatin1String(kParameterInterface), interfaceName},
                                                 {QLatin1String(kParameterNetworkFile), QString()},
                                                 {QLatin1String(kParameterDhcpIpv4), false},
                                                 {QLatin1String(kParameterDhcpIpv6), false},
                                                 {QLatin1String(kParameterIpv4Addresses), QJsonArray{}},
                                                 {QLatin1String(kParameterGateway), QString()},
                                                 {QLatin1String(kParameterDns), QJsonArray{}}}
                                           : parseDocument(document, interfaceName, networkFile);
        parameters.insert(QLatin1String(kParameterEditable), true);
        parameters.insert(QLatin1String(kParameterWarning), QJsonArray{});
        parameters.insert(QLatin1String(kParameterUserOverride), QFile::exists(userNetworkFilePath(interfaceName)));
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                parameters, true, true};
    }

    if (request.action == QLatin1String(kActionWriteSettings)) {
        QString validationError;
        if (!validateSettings(request.parameters, validationError)) {
            return errorResponse(request, QLatin1String(kErrorInvalidSettings) + QStringLiteral(": ") + validationError);
        }

        const QString targetPath = userNetworkFilePath(interfaceName);
        QString sourcePath = targetPath;
        if (!QFile::exists(sourcePath)) {
            bool statusOk = false;
            sourcePath = networkFileFromStatus(interfaceName, statusOk);
            if (!statusOk) {
                sourcePath.clear();
            }
            if (!sourcePath.isEmpty() && !isAllowedReadPath(sourcePath)) {
                return errorResponse(request, QLatin1String(kErrorUnsupportedNetworkFile));
            }
        }
        const NetworkFileAnalysis analysis = analyzeNetworkFile(sourcePath);
        if (!analysis.supported) {
            qWarning() << analysis.warning << "for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }

        qInfo().noquote() << "Writing network settings for" << interfaceName
                          << "source=" << sourcePath
                          << "target=" << targetPath;

        NetworkDocument document;
        if (!sourcePath.isEmpty()) {
            document = readDocument(sourcePath);
        }
        if (document.lines.isEmpty()) {
            document.lines.append(QStringLiteral("[Match]"));
            document.lines.append(QStringLiteral("Name=") + interfaceName);
            document.lines.append(QString());
            document.lines.append(QStringLiteral("[Network]"));
        }
        restrictMatchToInterface(document, interfaceName);
        replaceOwnedKeys(document, request.parameters);

        QSaveFile file(targetPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return errorResponse(request, QLatin1String(kErrorWriteFailed));
        }
        QTextStream stream(&file);
        stream << document.lines.join(QLatin1Char('\n')) << QLatin1Char('\n');
        if (stream.status() != QTextStream::Ok || !file.commit()) {
            return errorResponse(request, QLatin1String(kErrorWriteFailed));
        }
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterNetworkFile), targetPath}}, true, true};
    }

    if (request.action == QLatin1String(kActionResetSettings)) {
        const QString targetPath = userNetworkFilePath(interfaceName);
        const QString factoryPath = factoryNetworkFilePath(interfaceName);
        if (factoryPath.isEmpty()) {
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        if (!isAllowedReadPath(factoryPath)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedNetworkFile));
        }
        const NetworkFileAnalysis analysis = analyzeNetworkFile(factoryPath);
        if (!analysis.supported) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        const NetworkDocument document = readDocument(factoryPath);
        if (document.lines.isEmpty()) {
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        if (QFile::exists(targetPath) && !QFile::remove(targetPath)) {
            return errorResponse(request, QLatin1String(kErrorWriteFailed));
        }
        QJsonObject parameters = parseDocument(document, interfaceName, factoryPath);
        parameters.insert(QLatin1String(kParameterEditable), true);
        parameters.insert(QLatin1String(kParameterWarning), QJsonArray{});
        parameters.insert(QLatin1String(kParameterUserOverride), false);
        parameters.insert(QStringLiteral("reset"), true);
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                parameters, true, true};
    }

    if (request.action == QLatin1String(kActionApply)) {
        if (!applyNetworkConfiguration(runCommand)) {
            return errorResponse(request, QLatin1String(kErrorApplyFailed));
        }
        return {request.requestId, QLatin1String(kGroupNetwork), request.action, {}, true, true};
    }

    throw std::runtime_error("NetworkConfiguration::handleRequest got unsupported action");
}
} // namespace NetworkConfiguration
