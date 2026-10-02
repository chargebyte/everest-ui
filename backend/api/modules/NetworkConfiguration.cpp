// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "NetworkConfiguration.hpp"

#include "BackendConfig.hpp"
#include "NetworkInterfaceUtils.hpp"
#include "ProtocolSchema.hpp"

#include <QAbstractSocket>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QMap>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace {
constexpr char kConfigAvailableFeatures[] = "available_features";
constexpr char kConfigNetworkDeviceWhitelistPrefix[] = "network_device_whitelist.";
constexpr char kParameterAvailable[] = "available";
constexpr char kParameterInterfaces[] = "interfaces";
constexpr char kParameterInterface[] = "interface";
constexpr char kParameterExpertMode[] = "expert_mode";
constexpr char kParameterNetworkFile[] = "network_file";
constexpr char kParameterEditable[] = "editable";
constexpr char kParameterUserOverride[] = "user_override";
constexpr char kParameterResetStaged[] = "reset_staged";
constexpr char kParameterWarning[] = "warning";
constexpr char kParameterName[] = "name";
constexpr char kParameterIndex[] = "index";
constexpr char kParameterKind[] = "kind";
constexpr char kParameterOperationalState[] = "operational_state";
constexpr char kParameterSetupState[] = "setup_state";
constexpr char kParameterDriver[] = "driver";
constexpr char kParameterBridgeMember[] = "bridge_member";
constexpr char kParameterLoopback[] = "loopback";
constexpr char kParameterProbablyIsoHighLevelComms[] = "probably_iso_high_level_comms";
constexpr char kParameterDhcpIpv4[] = "dhcp_ipv4";
constexpr char kParameterDhcpIpv6[] = "dhcp_ipv6";
constexpr char kParameterIpv4Address[] = "ipv4_address";
constexpr char kParameterIpv4PrefixLength[] = "ipv4_prefix_length";
constexpr char kInternalFallbackIpv4Address[] = "_fallback_ipv4_address";
constexpr char kParameterGateway[] = "gateway";
constexpr char kParameterDns[] = "dns";
constexpr char kParameterCanBitRate[] = "can_bitrate";
constexpr char kParameterCanBitRateSource[] = "can_bitrate_source";
constexpr char kParameterCanBitRateOverride[] = "can_bitrate_override";
constexpr char kOwnedOverlayMarker[] = "# Managed by EVerest Web UI";
constexpr char kOwnedOverlayName[] = "50-everest-ui.conf";
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
constexpr char kErrorUnownedDropIn[] = "network_config_unowned_dropin";
constexpr char kNetworkFileEtc[] = "/etc/systemd/network/";
constexpr char kNetworkFileLib[] = "/lib/systemd/network/";
constexpr char kNetworkFileUsrLib[] = "/usr/lib/systemd/network/";
constexpr char kNetworkFileUsrLocalLib[] = "/usr/local/lib/systemd/network/";
constexpr char kNetworkFileRun[] = "/run/systemd/network/";
constexpr char kResetBackupSuffix[] = ".everest-ui-reset-backup";
constexpr char kResetCommittedSuffix[] = ".everest-ui-reset-committed";

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
    bool probablyIsoHighLevelComms = false;
};

struct NetworkDocument {
    QStringList lines;
};

struct NetworkFileAnalysis {
    bool supported = true;
    QString warning;
};

struct ResetApplyResult {
    bool success = false;
    bool writeFailed = false;
    bool applyFailed = false;
};

struct ResetMutation {
    QString interfaceName;
    QString path;
    bool removeEntireOverlay = false;
};

struct ResetTransactionEntry {
    QString path;
    QString backupPath;
    QString committedPath;
};

QSet<QString> g_pendingResetInterfaces;
QHash<QString, QString> g_pendingResetOverlayPaths;
QSet<QString> g_pendingCanBitRateResets;
QHash<QString, QString> g_pendingCanBitRateResetPaths;
bool g_resetRecoveryDone = false;

QString keyName(const QString &line, QString &value);
NetworkDocument readDocument(const QString &path);
bool isIpv4Cidr(const QString &value);
bool isIpv4Address(const QString &value);
bool isConfigurableNetworkInterfaceKind(const QString &kind);
bool isCanInterfaceKind(const QString &kind) {
    return kind.compare(QStringLiteral("can"), Qt::CaseInsensitive) == 0;
}

bool parseCanBitRate(const QString &text, quint64 &bitRate) {
    static const QRegularExpression pattern(QStringLiteral("^([0-9]+)([kKmM]?)$"));
    const auto match = pattern.match(text.trimmed());
    if (!match.hasMatch()) {
        return false;
    }
    bool ok = false;
    quint64 value = match.captured(1).toULongLong(&ok);
    if (!ok) {
        return false;
    }
    const QString suffix = match.captured(2).toLower();
    const quint64 multiplier = suffix == QStringLiteral("k") ? 1000ULL
                               : suffix == QStringLiteral("m") ? 1000000ULL : 1ULL;
    if (value > 4294967295ULL / multiplier) {
        return false;
    }
    value *= multiplier;
    if (value == 0 || value > 4294967295ULL) {
        return false;
    }
    bitRate = value;
    return true;
}

bool canBitRateInDocument(const NetworkDocument &document, quint64 &bitRate) {
    QString section;
    bool found = false;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        QString value;
        if (section.compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0 &&
            keyName(line, value).compare(QStringLiteral("BitRate"), Qt::CaseInsensitive) == 0) {
            if (value.isEmpty()) {
                found = false;
            } else {
                found = parseCanBitRate(value, bitRate);
            }
        }
    }
    return found;
}

bool hasCanBitRateDirective(const NetworkDocument &document) {
    QString section;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
        } else if (section.compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0) {
            QString value;
            if (keyName(line, value).compare(QStringLiteral("BitRate"), Qt::CaseInsensitive) == 0) return true;
        }
    }
    return false;
}

bool buildCanOverlayDocument(const NetworkDocument &existingOverlay, quint64 targetBitRate,
                             bool setBitRate, NetworkDocument &result) {
    result.lines.clear();
    bool inCanSection = false;
    bool canSectionFound = false;
    bool bitRateWritten = false;
    QStringList canSectionBody;
    auto flushCanSection = [&]() {
        if (!canSectionFound) {
            return;
        }
        QStringList retained;
        bool hasDirective = false;
        for (const QString &line : canSectionBody) {
            QString value;
            if (keyName(line, value).compare(QStringLiteral("BitRate"), Qt::CaseInsensitive) == 0) {
                if (setBitRate && !bitRateWritten) {
                    retained.append(QStringLiteral("BitRate=") + QString::number(targetBitRate));
                    bitRateWritten = true;
                }
            } else {
                retained.append(line);
                const QString trimmed = line.trimmed();
                hasDirective = hasDirective || (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')) &&
                                                  !trimmed.startsWith(QLatin1Char(';')));
            }
        }
        if (setBitRate && !bitRateWritten) {
            retained.append(QStringLiteral("BitRate=") + QString::number(targetBitRate));
            bitRateWritten = true;
        }
        if (hasDirective || !retained.isEmpty()) {
            result.lines.append(QStringLiteral("[CAN]"));
            result.lines.append(retained);
        }
        canSectionBody.clear();
    };

    for (const QString &line : existingOverlay.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            if (inCanSection) {
                flushCanSection();
            }
            const QString section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            inCanSection = section.compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0;
            canSectionFound = inCanSection;
            if (!inCanSection) {
                result.lines.append(line);
            }
            continue;
        }
        if (inCanSection) {
            canSectionBody.append(line);
        } else {
            result.lines.append(line);
        }
    }
    if (inCanSection) {
        flushCanSection();
    }
    if (setBitRate && !bitRateWritten) {
        result.lines.append(QStringLiteral("[CAN]"));
        result.lines.append(QStringLiteral("BitRate=") + QString::number(targetBitRate));
    }

    while (!result.lines.isEmpty() && result.lines.constLast().trimmed().isEmpty()) {
        result.lines.removeLast();
    }
    bool hasContent = false;
    for (const QString &line : result.lines) {
        const QString trimmed = line.trimmed();
        hasContent = hasContent || (!trimmed.isEmpty() && trimmed != QLatin1String(kOwnedOverlayMarker) &&
                                    !(trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))));
    }
    if (!hasContent) {
        result.lines.clear();
    } else if (result.lines.isEmpty() || result.lines.first().trimmed() != QLatin1String(kOwnedOverlayMarker)) {
        result.lines.prepend(QLatin1String(kOwnedOverlayMarker));
    }
    return true;
}

struct StructuredAddressInfo {
    int sectionCount = 0;
    int addressCount = 0;
    int networkAddressCount = 0;
    int gatewayCount = 0;
    int fallbackSectionCount = 0;
    bool currentSectionFallback = false;
    bool invalid = false;
};

StructuredAddressInfo inspectStructuredAddresses(const NetworkDocument &document) {
    StructuredAddressInfo info;
    QString section;
    bool addressEntryInSection = false;
    auto finishSection = [&]() {
        if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
            !addressEntryInSection) {
            info.invalid = true;
        }
        if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
            info.currentSectionFallback) {
            ++info.fallbackSectionCount;
        }
    };

    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            finishSection();
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            addressEntryInSection = false;
            info.currentSectionFallback = false;
            if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) {
                ++info.sectionCount;
            }
            continue;
        }

        QString value;
        const QString key = keyName(line, value);
        if ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
             section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) &&
            key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 && value.isEmpty()) {
            info.addressCount = 0;
            info.networkAddressCount = 0;
            continue;
        }
        if (key.compare(QStringLiteral("Label"), Qt::CaseInsensitive) == 0 &&
            section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
            value.endsWith(QStringLiteral(":fallback"), Qt::CaseInsensitive)) {
            info.currentSectionFallback = true;
        }
        if (key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) != 0) {
            if (key.compare(QStringLiteral("Gateway"), Qt::CaseInsensitive) == 0 &&
                (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                 section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0)) {
                if (value.isEmpty()) {
                    info.gatewayCount = 0;
                } else if (isIpv4Address(value)) {
                    ++info.gatewayCount;
                }
            }
            continue;
        }
        if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) {
            addressEntryInSection = true;
            ++info.addressCount;
            if (!isIpv4Cidr(value)) {
                info.invalid = true;
            }
        } else if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0) {
            if (isIpv4Cidr(value)) {
                ++info.networkAddressCount;
            }
        }
    }
    finishSection();
    return info;
}

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

bool kernelCanBitRate(const QString &interfaceName, quint64 &bitRate) {
    const CommandResult result = runCommand(QStringLiteral("ip"),
                                             {QStringLiteral("-details"), QStringLiteral("-json"),
                                              QStringLiteral("link"), QStringLiteral("show"),
                                              QStringLiteral("dev"), interfaceName});
    if (!result.started || result.exitCode != 0) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(result.output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isArray() || json.array().isEmpty()) {
        return false;
    }
    const QJsonObject linkInfo = json.array().first().toObject().value(QStringLiteral("linkinfo")).toObject();
    const QJsonObject infoData = linkInfo.value(QStringLiteral("info_data")).toObject();
    const QJsonObject timing = infoData.value(QStringLiteral("bittiming")).toObject();
    const QJsonValue value = timing.contains(QStringLiteral("bitrate"))
                                 ? timing.value(QStringLiteral("bitrate"))
                                 : infoData.value(QStringLiteral("bittiming_bitrate"));
    if (!value.isDouble()) {
        return false;
    }
    const double numeric = value.toDouble();
    if (numeric < 1 || numeric > 4294967295.0 || std::floor(numeric) != numeric) {
        return false;
    }
    bitRate = static_cast<quint64>(numeric);
    return true;
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

QSet<QString> networkDeviceWhitelistForCompatibleData(const QByteArray &compatibleData,
                                                       const QMap<QString, QString> &configured,
                                                       bool &applies) {
    applies = false;
    QSet<QString> devices;
    const QList<QByteArray> compatibleEntries = compatibleData.split('\0');
    for (const QByteArray &entry : compatibleEntries) {
        if (entry.isEmpty()) {
            continue;
        }
        const QString compatible = QString::fromUtf8(entry);
        const auto match = configured.constFind(compatible);
        if (match == configured.constEnd()) {
            continue;
        }
        applies = true;
        for (const QString &device : match.value().split(QLatin1Char(','), kSkipEmptyParts)) {
            const QString trimmedDevice = device.trimmed();
            if (!trimmedDevice.isEmpty()) {
                devices.insert(trimmedDevice);
            }
        }
    }
    return devices;
}

QSet<QString> configuredNetworkDeviceWhitelist(bool &applies) {
    QFile compatibleFile(QStringLiteral("/proc/device-tree/compatible"));
    if (!compatibleFile.open(QIODevice::ReadOnly)) {
        applies = false;
        return {};
    }
    return networkDeviceWhitelistForCompatibleData(
        compatibleFile.readAll(),
        readBackendConfigValues(QLatin1String(kConfigNetworkDeviceWhitelistPrefix)), applies);
}

bool networkDeviceAllowed(const QString &interfaceName, bool expertMode) {
    if (expertMode) {
        return true;
    }
    bool whitelistApplies = false;
    const QSet<QString> devices = configuredNetworkDeviceWhitelist(whitelistApplies);
    return !whitelistApplies || devices.contains(interfaceName);
}

bool validInterfaceNameSyntax(const QString &name) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.:@-]{1,15}$"));
    return pattern.match(name).hasMatch();
}

bool validInterfaceName(const QString &name) {
    return validInterfaceNameSyntax(name) && QNetworkInterface::interfaceFromName(name).isValid();
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
           cleanPath.startsWith(QLatin1String(kNetworkFileUsrLib)) ||
           cleanPath.startsWith(QLatin1String(kNetworkFileUsrLocalLib)) ||
           cleanPath.startsWith(QLatin1String(kNetworkFileRun));
}

bool isWithinRoots(const QString &path, const QStringList &roots) {
    const QString cleanPath = canonicalNetworkFilePath(path);
    if (cleanPath.isEmpty()) {
        return false;
    }
    for (const QString &root : roots) {
        const QString cleanRoot = QFileInfo(root).canonicalFilePath();
        if (!cleanRoot.isEmpty() && cleanPath.startsWith(cleanRoot + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

QString userNetworkDropInDirectory(const QString &networkFile) {
    const QFileInfo fileInfo(networkFile);
    if (!fileInfo.fileName().endsWith(QStringLiteral(".network"))) {
        return {};
    }
    return QString::fromLatin1(kNetworkFileEtc) + fileInfo.fileName() + QStringLiteral(".d");
}

QString userNetworkOverlayPath(const QString &networkFile) {
    const QString directory = userNetworkDropInDirectory(networkFile);
    return directory.isEmpty() ? QString()
                               : directory + QLatin1Char('/') + QLatin1String(kOwnedOverlayName);
}

bool lowerPriorityOverlayConflict(const QString &networkFile, const QString &targetPath,
                                 const QStringList &lowerPriorityRoots) {
    if (QFile::exists(targetPath)) {
        return false;
    }
    const QString dropInDirectoryName = QFileInfo(networkFile).fileName() + QStringLiteral(".d");
    for (const QString &root : lowerPriorityRoots) {
        const QString path = QDir(QDir(root).filePath(dropInDirectoryName))
                                 .filePath(QLatin1String(kOwnedOverlayName));
        if (QFile::exists(path)) {
            return true;
        }
    }
    return false;
}

bool isUiOwnedOverlay(const QString &path) {
    if (QFileInfo(path).isSymLink()) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    return QString::fromUtf8(file.readLine()).trimmed() == QLatin1String(kOwnedOverlayMarker);
}

QString resetOverlayOwnershipError(const QString &path) {
    return QFile::exists(path) && !isUiOwnedOverlay(path)
               ? QLatin1String(kErrorUnownedDropIn)
               : QString();
}

bool prepareOverlayDirectory(const QString &path) {
    const QFileInfo targetInfo(path);
    if (!targetInfo.dir().exists() && !QDir().mkpath(targetInfo.dir().absolutePath())) {
        return false;
    }
    const QString root = QFileInfo(QString::fromLatin1(kNetworkFileEtc)).canonicalFilePath();
    const QString directory = QFileInfo(targetInfo.dir().absolutePath()).canonicalFilePath();
    return !root.isEmpty() && !directory.isEmpty() &&
           (directory == root || directory.startsWith(root + QLatin1Char('/')));
}

bool writeOverlay(const QString &path, const NetworkDocument &document) {
    if (!prepareOverlayDirectory(path)) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    stream << document.lines.join(QLatin1Char('\n')) << QLatin1Char('\n');
    return stream.status() == QTextStream::Ok && file.commit();
}

QString resetBackupPath(const QString &path) {
    return path + QLatin1String(kResetBackupSuffix);
}

QString resetCommittedPath(const QString &path) {
    return path + QLatin1String(kResetCommittedSuffix);
}

bool recoverResetBackupsInDirectory(const QString &root,
                                    const std::function<bool(const QString &)> &interfaceAvailable) {
    bool recovered = true;
    const QDir directory(root);
    const QStringList committed = directory.entryList(
        QStringList() << QStringLiteral("*.network") + QLatin1String(kResetCommittedSuffix), QDir::Files);
    for (const QString &committedName : committed) {
        const QString committedPath = directory.filePath(committedName);
        if (!QFile::remove(committedPath)) {
            qWarning() << "Unable to remove committed network reset backup" << committedPath;
            recovered = false;
        }
    }

    const QStringList backups = directory.entryList(
        QStringList() << QStringLiteral("*.network") + QLatin1String(kResetBackupSuffix), QDir::Files);
    for (const QString &backupName : backups) {
        const int suffixLength = QLatin1String(kResetBackupSuffix).size();
        const QString originalName = backupName.left(backupName.size() - suffixLength);
        if (!originalName.endsWith(QStringLiteral(".network"))) {
            continue;
        }
        const QString interfaceName = originalName.left(originalName.size() - QStringLiteral(".network").size());
        if (!validInterfaceNameSyntax(interfaceName)) {
            qWarning() << "Ignoring reset backup for invalid interface" << interfaceName;
            continue;
        }
        if (!interfaceAvailable(interfaceName)) {
            qWarning() << "Deferring reset backup recovery for unavailable interface" << interfaceName;
            recovered = false;
            continue;
        }
        const QString originalPath = directory.filePath(originalName);
        const QString backupPath = directory.filePath(backupName);
        if (QFile::exists(originalPath)) {
            qWarning() << "Keeping reset backup because network override already exists" << backupPath;
            continue;
        }
        if (!QFile::rename(backupPath, originalPath)) {
            qCritical() << "Unable to recover network override from" << backupPath;
            recovered = false;
        }
    }
    return recovered;
}

bool recoverOverlayResetBackupsInDirectory(const QString &root) {
    bool recovered = true;
    QDir rootDirectory(root);
    const QStringList dropInDirectories = rootDirectory.entryList(
        QStringList() << QStringLiteral("*.network.d"), QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &directoryName : dropInDirectories) {
        const QString directoryPath = rootDirectory.filePath(directoryName);
        QDir directory(directoryPath);
        const QStringList committed = directory.entryList(
            QStringList() << QStringLiteral("*.conf") + QLatin1String(kResetCommittedSuffix), QDir::Files);
        for (const QString &fileName : committed) {
            if (!QFile::remove(directory.filePath(fileName))) {
                recovered = false;
            }
        }
        const QStringList backups = directory.entryList(
            QStringList() << QStringLiteral("*.conf") + QLatin1String(kResetBackupSuffix), QDir::Files);
        for (const QString &backupName : backups) {
            const QString originalName = backupName.left(
                backupName.size() - QLatin1String(kResetBackupSuffix).size());
            const QString originalPath = directory.filePath(originalName);
            if (QFile::exists(originalPath)) {
                continue;
            }
            if (!QFile::rename(directory.filePath(backupName), originalPath)) {
                recovered = false;
            }
        }
    }
    return recovered;
}

void recoverResetBackups() {
    if (g_resetRecoveryDone) {
        return;
    }
    const bool legacyRecovered = recoverResetBackupsInDirectory(
        QString::fromLatin1(kNetworkFileEtc),
        [](const QString &interfaceName) { return validInterfaceName(interfaceName); });
    const bool overlaysRecovered =
        recoverOverlayResetBackupsInDirectory(QString::fromLatin1(kNetworkFileEtc));
    g_resetRecoveryDone = legacyRecovered && overlaysRecovered;
}

bool restoreResetBackups(const QList<QPair<QString, QString>> &backups) {
    bool restored = true;
    for (auto it = backups.crbegin(); it != backups.crend(); ++it) {
        if (!QFile::exists(it->second)) {
            continue;
        }
        if (QFile::exists(it->first) || !QFile::rename(it->second, it->first)) {
            qCritical() << "Unable to restore network override" << it->first;
            restored = false;
        }
    }
    return restored;
}

bool revertCommittedBackups(const QList<QPair<QString, QString>> &backups) {
    bool reverted = true;
    for (auto it = backups.crbegin(); it != backups.crend(); ++it) {
        const QString committedPath = resetCommittedPath(it->first);
        if (!QFile::exists(committedPath)) {
            continue;
        }
        if (QFile::exists(it->second) || !QFile::rename(committedPath, it->second)) {
            qCritical() << "Unable to revert committed network reset backup" << committedPath;
            reverted = false;
        }
    }
    return reverted;
}

using OverlayWriter = std::function<bool(const QString &, const NetworkDocument &)>;

ResetApplyResult applyResetTransaction(const QList<ResetMutation> &mutations, const CommandRunner &run,
                                       const OverlayWriter &write = writeOverlay) {
    QMap<QString, ResetMutation> mutationsByPath;
    for (const ResetMutation &mutation : mutations) {
        if (mutation.path.isEmpty()) {
            continue;
        }
        ResetMutation &existing = mutationsByPath[mutation.path];
        existing.path = mutation.path;
        existing.removeEntireOverlay = existing.removeEntireOverlay || mutation.removeEntireOverlay;
    }

    QList<ResetTransactionEntry> staged;
    const auto rollbackStaged = [&staged]() {
        bool restored = true;
        for (auto it = staged.crbegin(); it != staged.crend(); ++it) {
            if (QFile::exists(it->path) && !QFile::remove(it->path)) {
                qCritical() << "Unable to remove staged network reset" << it->path;
                restored = false;
            }
        }
        QList<QPair<QString, QString>> backups;
        for (const ResetTransactionEntry &entry : staged) {
            backups.append(qMakePair(entry.path, entry.backupPath));
        }
        return restoreResetBackups(backups) && restored;
    };

    for (const ResetMutation &mutation : mutationsByPath) {
        if (QFile::exists(mutation.path) && !isUiOwnedOverlay(mutation.path)) {
            rollbackStaged();
            return {false, true, false};
        }
    }

    for (const ResetMutation &mutation : mutationsByPath) {
        if (!QFile::exists(mutation.path)) {
            continue;
        }
        ResetTransactionEntry entry{mutation.path, resetBackupPath(mutation.path),
                                    resetCommittedPath(mutation.path)};
        if (QFile::exists(entry.backupPath) || !QFile::rename(entry.path, entry.backupPath)) {
            rollbackStaged();
            return {false, true, false};
        }
        staged.append(entry);
        if (!mutation.removeEntireOverlay) {
            NetworkDocument updated;
            if (!buildCanOverlayDocument(readDocument(entry.backupPath), 0, false, updated) ||
                (!updated.lines.isEmpty() && !write(entry.path, updated))) {
                rollbackStaged();
                return {false, true, false};
            }
        }
    }

    if (!applyNetworkConfiguration(run)) {
        const bool restored = rollbackStaged();
        if (restored) {
            applyNetworkConfiguration(run);
        }
        return {false, false, true};
    }

    QList<QPair<QString, QString>> committedBackups;
    for (const ResetTransactionEntry &entry : staged) {
        if (QFile::exists(entry.committedPath) || !QFile::rename(entry.backupPath, entry.committedPath)) {
            bool reverted = true;
            for (auto it = committedBackups.crbegin(); it != committedBackups.crend(); ++it) {
                if (QFile::exists(it->second) || !QFile::rename(it->first, it->second)) {
                    qCritical() << "Unable to revert committed network reset backup" << it->first;
                    reverted = false;
                }
            }
            const bool filesRestored = rollbackStaged();
            const bool restored = reverted && filesRestored;
            if (restored) {
                applyNetworkConfiguration(run);
            }
            return {false, false, true};
        }
        committedBackups.append(qMakePair(entry.committedPath, entry.backupPath));
    }

    for (const ResetTransactionEntry &entry : staged) {
        if (QFile::exists(entry.committedPath) && !QFile::remove(entry.committedPath)) {
            qWarning() << "Unable to remove committed network reset backup" << entry.committedPath;
        }
    }
    return {true, false, false};
}

ResetApplyResult applyPendingResets(QSet<QString> &pendingInterfaces,
                                    const std::function<QString(const QString &)> &pathForInterface,
                                    const CommandRunner &run) {
    QList<ResetMutation> mutations;
    for (const QString &interfaceName : pendingInterfaces) {
        mutations.append({interfaceName, pathForInterface(interfaceName), true});
    }
    const ResetApplyResult result = applyResetTransaction(mutations, run, writeOverlay);
    if (result.success) {
        pendingInterfaces.clear();
    }
    return result;
}

ResetApplyResult applyPendingNetworkResets(const CommandRunner &run,
                                          const OverlayWriter &write = writeOverlay) {
    QList<ResetMutation> mutations;
    for (const QString &interfaceName : g_pendingResetInterfaces) {
        mutations.append({interfaceName, g_pendingResetOverlayPaths.value(interfaceName), true});
    }
    for (const QString &interfaceName : g_pendingCanBitRateResets) {
        mutations.append({interfaceName, g_pendingCanBitRateResetPaths.value(interfaceName), false});
    }
    const ResetApplyResult result = applyResetTransaction(mutations, run, write);
    if (result.success) {
        g_pendingResetInterfaces.clear();
        g_pendingResetOverlayPaths.clear();
        g_pendingCanBitRateResets.clear();
        g_pendingCanBitRateResetPaths.clear();
    }
    return result;
}

NetworkFileAnalysis analyzeNetworkDocument(const NetworkDocument &document) {
    const StructuredAddressInfo addressInfo = inspectStructuredAddresses(document);
    if (addressInfo.networkAddressCount > 1 || addressInfo.gatewayCount > 1 ||
        (addressInfo.sectionCount > 0 &&
         (addressInfo.invalid || addressInfo.sectionCount != 1 || addressInfo.addressCount != 1 ||
          addressInfo.fallbackSectionCount > 1 ||
          (addressInfo.networkAddressCount > 0 && addressInfo.fallbackSectionCount == 0)))) {
        return {false, QStringLiteral("This network file contains multiple or ambiguous structured Address settings that the Web UI cannot safely edit.")};
    }
    QString section;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        if (section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0) {
            QString value;
            const QString key = keyName(line, value);
            const bool allowed = key.compare(QStringLiteral("Gateway"), Qt::CaseInsensitive) == 0;
            if (!key.isEmpty() && !allowed) {
                return {false, QStringLiteral("This network file contains structured Address or Route settings that the Web UI cannot safely edit.")};
            }
        }
    }
    return {};
}

NetworkFileAnalysis analyzeNetworkFile(const QString &path) {
    return path.isEmpty() ? NetworkFileAnalysis{} : analyzeNetworkDocument(readDocument(path));
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

NetworkDocument readEffectiveNetworkDocumentFromRoots(const QString &networkFile,
                                                       bool includeUiOverlay, bool &ok,
                                                       const QStringList &roots,
                                                       const QString &overlayPath) {
    ok = false;
    if (networkFile.isEmpty()) {
        ok = true;
        return {};
    }
    const QString fileName = QFileInfo(networkFile).fileName();
    if (!fileName.endsWith(QStringLiteral(".network"))) {
        return {};
    }

    NetworkDocument effective = readDocument(networkFile);
    if (effective.lines.isEmpty()) {
        return {};
    }

    const QString dropInDirectoryName = fileName + QStringLiteral(".d");
    QMap<QString, QString> selectedDropIns;
    for (const QString &root : roots) {
        const QDir directory(QDir(root).filePath(dropInDirectoryName));
        const QStringList fileNames = directory.entryList(
            QStringList() << QStringLiteral("*.conf"), QDir::Files, QDir::Name);
        for (const QString &name : fileNames) {
            const QString path = directory.filePath(name);
            if (!includeUiOverlay &&
                QFileInfo(path).absoluteFilePath() == QFileInfo(overlayPath).absoluteFilePath()) {
                continue;
            }
            if (!isWithinRoots(path, roots)) {
                return {};
            }
            if (!selectedDropIns.contains(name)) {
                selectedDropIns.insert(name, path);
            }
        }
    }

    for (auto it = selectedDropIns.cbegin(); it != selectedDropIns.cend(); ++it) {
        const NetworkDocument dropIn = readDocument(it.value());
        if (dropIn.lines.isEmpty()) {
            return {};
        }
        effective.lines.append(dropIn.lines);
    }
    ok = true;
    return effective;
}

NetworkDocument readEffectiveNetworkDocument(const QString &networkFile, bool includeUiOverlay,
                                             bool &ok) {
    return readEffectiveNetworkDocumentFromRoots(
        networkFile, includeUiOverlay, ok,
        {QLatin1String(kNetworkFileEtc), QLatin1String(kNetworkFileRun),
         QLatin1String(kNetworkFileUsrLocalLib), QLatin1String(kNetworkFileUsrLib),
         QLatin1String(kNetworkFileLib)},
        userNetworkOverlayPath(networkFile));
}

QJsonObject parseDocument(const NetworkDocument &document, const QString &name, const QString &path) {
    QString section;
    QString dhcp;
    QString gateway;
    QStringList networkAddresses;
    QString primaryAddress;
    QString fallbackAddress;
    bool structuredFallback = false;
    QStringList dns;

    QString structuredLabel;
    QString scanSection;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            scanSection = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        QString value;
        if (scanSection.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
            keyName(line, value).compare(QStringLiteral("Label"), Qt::CaseInsensitive) == 0) {
            structuredLabel = value;
        }
    }
    const bool structuredLabelIsFallback = structuredLabel.endsWith(QStringLiteral(":fallback"), Qt::CaseInsensitive);

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
        } else if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 &&
                   key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 && value.isEmpty()) {
            networkAddresses.clear();
        } else if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
                   key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 && value.isEmpty()) {
            primaryAddress.clear();
            fallbackAddress.clear();
            structuredFallback = false;
        } else if ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                    section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) &&
                   key.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
                   isIpv4Cidr(value)) {
            if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0) {
                if (structuredLabelIsFallback) {
                    fallbackAddress = value;
                    structuredFallback = true;
                } else {
                    primaryAddress = value;
                }
            } else {
                networkAddresses.append(value);
            }
        } else if (section.compare(QStringLiteral("Address"), Qt::CaseInsensitive) == 0 &&
                   key.compare(QStringLiteral("Label"), Qt::CaseInsensitive) == 0) {
            structuredLabel = value;
        } else if ((section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 ||
                    section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0) &&
                   key.compare(QStringLiteral("Gateway"), Qt::CaseInsensitive) == 0) {
            if (value.isEmpty()) {
                gateway.clear();
            } else if (isIpv4Address(value)) {
                gateway = value;
            }
        } else if (section.compare(QStringLiteral("Network"), Qt::CaseInsensitive) == 0 &&
                   key.compare(QStringLiteral("DNS"), Qt::CaseInsensitive) == 0) {
            if (value.isEmpty()) {
                dns.clear();
            } else if (isIpv4Address(value)) {
                dns.append(value);
            }
        }
    }

    QString effectivePrimary;
    if (!networkAddresses.isEmpty()) {
        effectivePrimary = networkAddresses.first();
        if (networkAddresses.size() > 1 && fallbackAddress.isEmpty()) {
            fallbackAddress = networkAddresses.at(1);
        }
    } else if (!primaryAddress.isEmpty() && !structuredFallback) {
        effectivePrimary = primaryAddress;
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
    const int slash = effectivePrimary.indexOf(QLatin1Char('/'));
    bool prefixOk = false;
    const int prefixLength = slash < 0 ? 24 : effectivePrimary.mid(slash + 1).toInt(&prefixOk);

    return {
        {QLatin1String(kParameterInterface), name},
        {QLatin1String(kParameterNetworkFile), path},
        {QLatin1String(kParameterDhcpIpv4), dhcpIpv4},
        {QLatin1String(kParameterDhcpIpv6), dhcpIpv6},
        {QLatin1String(kParameterIpv4Address), slash < 0 ? effectivePrimary : effectivePrimary.left(slash)},
        {QLatin1String(kParameterIpv4PrefixLength), prefixOk ? prefixLength : 24},
        {QLatin1String(kInternalFallbackIpv4Address), fallbackAddress},
        {QLatin1String(kParameterGateway), gateway},
        {QLatin1String(kParameterDns), dnsArray},
    };
}

QJsonObject publicNetworkSettings(QJsonObject settings) {
    settings.remove(QLatin1String(kInternalFallbackIpv4Address));
    return settings;
}

QStringList valuesForKey(const NetworkDocument &document, const QStringList &sections,
                         const QString &keyNameToFind, bool clearOnEmpty) {
    QStringList values;
    QString section;
    for (const QString &line : document.lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        if (!sections.contains(section, Qt::CaseInsensitive)) {
            continue;
        }
        QString value;
        if (keyName(line, value).compare(keyNameToFind, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (value.isEmpty() && clearOnEmpty) {
            values.clear();
        } else if (!value.isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

bool buildOverlayDocument(const NetworkDocument &underlay, const QJsonObject &settings,
                          NetworkDocument &overlay, const NetworkDocument *effectiveDocument = nullptr) {
    const QJsonObject current = parseDocument(underlay, QString(), QString());
    const QJsonObject effective = parseDocument(effectiveDocument ? *effectiveDocument : underlay,
                                                QString(), QString());
    const bool dhcpIpv4 = settings.value(QLatin1String(kParameterDhcpIpv4)).toBool();
    const bool dhcpIpv6 = settings.value(QLatin1String(kParameterDhcpIpv6)).toBool();
    QString targetAddress;
    if (!dhcpIpv4) {
        targetAddress = settings.value(QLatin1String(kParameterIpv4Address)).toString() +
                        QLatin1Char('/') +
                        QString::number(settings.value(QLatin1String(kParameterIpv4PrefixLength)).toInt());
    }

    QStringList directives;
    const bool currentDhcpIpv4 = current.value(QLatin1String(kParameterDhcpIpv4)).toBool();
    const bool currentDhcpIpv6 = current.value(QLatin1String(kParameterDhcpIpv6)).toBool();
    if (dhcpIpv4 != currentDhcpIpv4 || dhcpIpv6 != currentDhcpIpv6) {
        QString value = QStringLiteral("no");
        if (dhcpIpv4 && dhcpIpv6) {
            value = QStringLiteral("yes");
        } else if (dhcpIpv4) {
            value = QStringLiteral("ipv4");
        } else if (dhcpIpv6) {
            value = QStringLiteral("ipv6");
        }
        directives.append(QStringLiteral("DHCP=") + value);
    }

    const QString currentBareAddress = current.value(QLatin1String(kParameterIpv4Address)).toString();
    const QString currentAddress = currentBareAddress.isEmpty()
                                       ? QString()
                                       : currentBareAddress + QLatin1Char('/') +
                                             QString::number(current.value(QLatin1String(kParameterIpv4PrefixLength)).toInt());
    const QString fallbackAddress = effective.value(QLatin1String(kInternalFallbackIpv4Address)).toString();
    const QString underlayFallbackAddress = current.value(QLatin1String(kInternalFallbackIpv4Address)).toString();
    if (!underlayFallbackAddress.isEmpty() && !fallbackAddress.isEmpty() &&
        fallbackAddress != underlayFallbackAddress) {
        return false;
    }
    if (targetAddress != currentAddress) {
        const StructuredAddressInfo addressInfo = inspectStructuredAddresses(underlay);
        if (addressInfo.sectionCount > 0 && addressInfo.fallbackSectionCount != addressInfo.sectionCount) {
            return false;
        }
        const QStringList priorAddresses = valuesForKey(
            underlay, {QStringLiteral("Network")},
            QStringLiteral("Address"), true);
        for (const QString &address : priorAddresses) {
            if (!isIpv4Cidr(address)) {
                return false;
            }
        }
        if (priorAddresses.size() > 1) {
            return false;
        }
        if (!priorAddresses.isEmpty()) {
            directives.append(QStringLiteral("Address="));
        }
        if (!targetAddress.isEmpty()) {
            directives.append(QStringLiteral("Address=") + targetAddress);
        }
    }
    const bool emitFallback = !fallbackAddress.isEmpty() && fallbackAddress != underlayFallbackAddress;

    const QString targetGateway = dhcpIpv4 ? QString()
                                           : settings.value(QLatin1String(kParameterGateway)).toString();
    if (targetGateway != current.value(QLatin1String(kParameterGateway)).toString()) {
        QString section;
        for (const QString &line : underlay.lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
                section = trimmed.mid(1, trimmed.size() - 2).trimmed();
                if (section.compare(QStringLiteral("Route"), Qt::CaseInsensitive) == 0) {
                    return false;
                }
            }
        }
        const QStringList priorGateways = valuesForKey(
            underlay, {QStringLiteral("Network"), QStringLiteral("Route")},
            QStringLiteral("Gateway"), true);
        for (const QString &gateway : priorGateways) {
            if (!isIpv4Address(gateway)) {
                return false;
            }
        }
        if (!priorGateways.isEmpty()) {
            directives.append(QStringLiteral("Gateway="));
        }
        if (!targetGateway.isEmpty()) {
            directives.append(QStringLiteral("Gateway=") + targetGateway);
        }
    }

    const QJsonArray targetDns = dhcpIpv4 ? QJsonArray{}
                                          : settings.value(QLatin1String(kParameterDns)).toArray();
    if (targetDns != current.value(QLatin1String(kParameterDns)).toArray()) {
        const QStringList priorDns = valuesForKey(
            underlay, {QStringLiteral("Network")}, QStringLiteral("DNS"), true);
        if (!priorDns.isEmpty()) {
            directives.append(QStringLiteral("DNS="));
        }
        for (const QString &server : priorDns) {
            if (!isIpv4Address(server)) {
                directives.append(QStringLiteral("DNS=") + server);
            }
        }
        for (const QJsonValue &server : targetDns) {
            directives.append(QStringLiteral("DNS=") + server.toString());
        }
    }

    overlay.lines.clear();
    if (directives.isEmpty() && !emitFallback) {
        return true;
    }
    overlay.lines.append(QLatin1String(kOwnedOverlayMarker));
    if (!directives.isEmpty()) {
        overlay.lines.append(QStringLiteral("[Network]"));
        overlay.lines.append(directives);
    }
    if (emitFallback) {
        const QString interfaceName = settings.value(QLatin1String(kParameterInterface)).toString();
        if (!validInterfaceNameSyntax(interfaceName)) {
            return false;
        }
        overlay.lines.append(QStringLiteral("[Address]"));
        overlay.lines.append(QStringLiteral("Address=") + fallbackAddress);
        overlay.lines.append(QStringLiteral("Label=") + interfaceName + QStringLiteral(":fallback"));
    }
    return true;
}

bool validateSettings(const QJsonObject &settings, QString &error) {
    if (settings.contains(QStringLiteral("dhcp_ipv4_static")) ||
        settings.contains(QStringLiteral("ipv4_addresses")) ||
        settings.contains(QStringLiteral("fallback_ipv4_address"))) {
        error = QStringLiteral("legacy mixed-mode or fallback address fields are no longer supported");
        return false;
    }
    if (!settings.value(QLatin1String(kParameterDhcpIpv4)).isBool()) {
        error = QStringLiteral("dhcp_ipv4 must be boolean");
        return false;
    }
    if (!settings.value(QLatin1String(kParameterDhcpIpv6)).isBool()) {
        error = QStringLiteral("dhcp_ipv6 must be boolean");
        return false;
    }
    const QJsonValue addressValue = settings.value(QLatin1String(kParameterIpv4Address));
    const QJsonValue prefixValue = settings.value(QLatin1String(kParameterIpv4PrefixLength));
    if (!addressValue.isString() || !prefixValue.isDouble() ||
        std::floor(prefixValue.toDouble()) != prefixValue.toDouble()) {
        error = QStringLiteral("ipv4_address must be a string and ipv4_prefix_length an integer");
        return false;
    }
    const QString address = addressValue.toString();
    const int prefix = prefixValue.toInt();
    if (prefix < 0 || prefix > 32 ||
        (!settings.value(QLatin1String(kParameterDhcpIpv4)).toBool() && !isIpv4Address(address))) {
        error = QStringLiteral("invalid IPv4 address or prefix length");
        return false;
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
    return true;
}

QJsonObject interfaceObject(const InterfaceInfo &info) {
    QStringList warnings;
    if (info.loopback) {
        warnings.append(QStringLiteral("Loopback traffic is local to the target and is normally not a management interface."));
    }
    if (info.probablyIsoHighLevelComms) {
        warnings.append(QStringLiteral("This interface is likely used for ISO high level communications (PLC/HomePlug)."));
    }
    if (info.bridgeMember) {
        warnings.append(QStringLiteral("This interface has a network master; configure the master interface when appropriate."));
    }
    if (info.kind.compare(QStringLiteral("can"), Qt::CaseInsensitive) == 0) {
        warnings.append(QStringLiteral("Only CAN bitrate is configurable for this interface."));
    }
    if (!info.networkFile.isEmpty() && !isAllowedReadPath(info.networkFile)) {
        warnings.append(QStringLiteral("The effective network file is outside the locations supported by the Web UI."));
    }

    QJsonArray warningArray;
    for (const QString &warning : warnings) {
        warningArray.append(warning);
    }

    const bool special = info.loopback ||
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
        {QLatin1String(kParameterProbablyIsoHighLevelComms), info.probablyIsoHighLevelComms},
        {QLatin1String(kParameterEditable), !special && isConfigurableNetworkInterfaceKind(info.kind)},
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

    const QStringList isoHighLevelCommsDrivers =
        NetworkInterfaceUtils::configuredIsoHighLevelCommsDrivers();
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
        info.driver = NetworkInterfaceUtils::driverName(info.name);
        info.bridgeMember = NetworkInterfaceUtils::hasBridgeMaster(info.name);
        info.loopback = info.name == QStringLiteral("lo");
        info.probablyIsoHighLevelComms = NetworkInterfaceUtils::isIsoHighLevelCommsDriver(
            info.driver, isoHighLevelCommsDrivers);
        interfaces.append(info);
    }
    return interfaces;
}

QString networkInterfaceKind(const QString &interfaceName) {
    bool success = false;
    const QList<InterfaceInfo> interfaces = readInterfaces(success);
    if (success) {
        for (const InterfaceInfo &info : interfaces) {
            if (info.name == interfaceName) return info.kind;
        }
    }
    return {};
}

bool isConfigurableNetworkInterfaceKind(const QString &kind) {
    return kind.compare(QStringLiteral("ether"), Qt::CaseInsensitive) == 0 ||
           kind.compare(QStringLiteral("bridge"), Qt::CaseInsensitive) == 0 || isCanInterfaceKind(kind);
}

bool isConfigurableNetworkInterface(const QString &interfaceName) {
    bool success = false;
    const QList<InterfaceInfo> interfaces = readInterfaces(success);
    if (!success) {
        return false;
    }
    for (const InterfaceInfo &info : interfaces) {
        if (info.name == interfaceName) {
            return isConfigurableNetworkInterfaceKind(info.kind) && !info.loopback;
        }
    }
    return false;
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
    recoverResetBackups();
    const bool expertMode = request.parameters.value(QLatin1String(kParameterExpertMode)).toBool();
    if (request.action == QLatin1String(kActionReadInterfaces)) {
        if (!featureAvailable(QStringLiteral("network"))) {
            return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                    {{QLatin1String(kParameterAvailable), false},
                     {QLatin1String(kParameterInterfaces), QJsonArray{}}},
                    true, true};
        }
        bool listSucceeded = false;
        QList<InterfaceInfo> interfaces = readInterfaces(listSucceeded);
        if (!listSucceeded) {
            return errorResponse(request, QLatin1String(kErrorListFailed));
        }
        if (!expertMode) {
            bool whitelistApplies = false;
            const QSet<QString> allowedDevices = configuredNetworkDeviceWhitelist(whitelistApplies);
            if (whitelistApplies) {
                interfaces.erase(std::remove_if(interfaces.begin(), interfaces.end(),
                                                [&allowedDevices](const InterfaceInfo &info) {
                                                    return !allowedDevices.contains(info.name);
                                                }),
                                 interfaces.end());
            }
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
    if (!networkDeviceAllowed(interfaceName, expertMode)) {
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
        bool underlayOk = false;
        bool effectiveOk = false;
        const NetworkDocument underlay = readEffectiveNetworkDocument(networkFile, false, underlayOk);
        const NetworkDocument document = readEffectiveNetworkDocument(networkFile, true, effectiveOk);
        if (!networkFile.isEmpty() && (!underlayOk || !effectiveOk)) {
            qWarning() << "Unable to read effective network configuration for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        const bool canInterface = isCanInterfaceKind(networkInterfaceKind(interfaceName));
        const NetworkFileAnalysis analysis = analyzeNetworkDocument(underlay);
        if (!canInterface && !analysis.supported) {
            qWarning() << analysis.warning << "for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        const QString overlayPath = userNetworkOverlayPath(networkFile);
        if (!canInterface && !overlayPath.isEmpty() && QFile::exists(overlayPath) && !isUiOwnedOverlay(overlayPath) &&
            !analyzeNetworkDocument(readDocument(overlayPath)).supported) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        QJsonObject parameters = document.lines.isEmpty()
                                           ? QJsonObject{
                                                 {QLatin1String(kParameterInterface), interfaceName},
                                                 {QLatin1String(kParameterNetworkFile), QString()},
                                                 {QLatin1String(kParameterDhcpIpv4), false},
                                                 {QLatin1String(kParameterDhcpIpv6), false},
                                                 {QLatin1String(kParameterIpv4Address), QString()},
                                                 {QLatin1String(kParameterIpv4PrefixLength), 24},
                                                 {QLatin1String(kInternalFallbackIpv4Address), QString()},
                                                 {QLatin1String(kParameterGateway), QString()},
                                                 {QLatin1String(kParameterDns), QJsonArray{}}}
                                           : parseDocument(document, interfaceName, networkFile);
        const bool configurableInterface = isConfigurableNetworkInterface(interfaceName);
        parameters.insert(QLatin1String(kParameterEditable), !networkFile.isEmpty() && configurableInterface);
        parameters.insert(QLatin1String(kParameterWarning), QJsonArray{});
        parameters.insert(QLatin1String(kParameterUserOverride),
                           !overlayPath.isEmpty() && isUiOwnedOverlay(overlayPath));
        const bool canResetStaged = g_pendingCanBitRateResets.contains(interfaceName);
        parameters.insert(QLatin1String(kParameterResetStaged),
                           g_pendingResetInterfaces.contains(interfaceName) || canResetStaged);
        if (canInterface) {
            quint64 configuredBitRate = 0;
            const bool hasConfiguredBitRate = canBitRateInDocument(document, configuredBitRate);
            quint64 liveBitRate = 0;
            const bool hasLiveBitRate = !hasConfiguredBitRate && kernelCanBitRate(interfaceName, liveBitRate);
            const quint64 effectiveBitRate = hasConfiguredBitRate ? configuredBitRate : liveBitRate;
            parameters.insert(QLatin1String(kParameterCanBitRate),
                              hasConfiguredBitRate || hasLiveBitRate
                                  ? QJsonValue(static_cast<double>(effectiveBitRate)) : QJsonValue(QJsonValue::Null));
            parameters.insert(QLatin1String(kParameterCanBitRateSource),
                              hasConfiguredBitRate ? QStringLiteral("networkd")
                              : hasLiveBitRate ? QStringLiteral("kernel") : QStringLiteral("unknown"));
            const bool hasOverlayRate = !overlayPath.isEmpty() && QFile::exists(overlayPath) &&
                                        hasCanBitRateDirective(readDocument(overlayPath));
            parameters.insert(QLatin1String(kParameterCanBitRateOverride), hasOverlayRate && isUiOwnedOverlay(overlayPath));
            parameters.remove(QLatin1String(kParameterDhcpIpv4));
            parameters.remove(QLatin1String(kParameterDhcpIpv6));
            parameters.remove(QLatin1String(kParameterIpv4Address));
            parameters.remove(QLatin1String(kParameterIpv4PrefixLength));
            parameters.remove(QLatin1String(kParameterGateway));
            parameters.remove(QLatin1String(kParameterDns));
        }
        parameters = publicNetworkSettings(parameters);
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                parameters, true, true};
    }

    if (request.action == QLatin1String(kActionWriteSettings)) {
        if (!isConfigurableNetworkInterface(interfaceName)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        const QString interfaceKind = networkInterfaceKind(interfaceName);
        const bool canInterface = isCanInterfaceKind(interfaceKind);
        quint64 requestedCanBitRate = 0;
        if (canInterface) {
            const QJsonValue value = request.parameters.value(QLatin1String(kParameterCanBitRate));
            if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble() ||
                !parseCanBitRate(QString::number(value.toDouble(), 'f', 0), requestedCanBitRate)) {
                return errorResponse(request, QLatin1String(kErrorInvalidSettings) + QStringLiteral(": invalid CAN bitrate"));
            }
        } else {
            QString validationError;
            if (!validateSettings(request.parameters, validationError)) {
                return errorResponse(request, QLatin1String(kErrorInvalidSettings) + QStringLiteral(": ") + validationError);
            }
        }

        bool statusOk = false;
        const QString sourcePath = networkFileFromStatus(interfaceName, statusOk);
        if (!statusOk) {
            return errorResponse(request, QLatin1String(kErrorStatusFailed));
        }
        if (sourcePath.isEmpty()) {
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        if (!isAllowedReadPath(sourcePath)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedNetworkFile));
        }
        const QString targetPath = userNetworkOverlayPath(sourcePath);
        if (targetPath.isEmpty()) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedNetworkFile));
        }
        if (QFileInfo(targetPath).isSymLink()) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        if (lowerPriorityOverlayConflict(
                sourcePath, targetPath,
                {QLatin1String(kNetworkFileRun), QLatin1String(kNetworkFileUsrLocalLib),
                 QLatin1String(kNetworkFileUsrLib), QLatin1String(kNetworkFileLib)})) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }

        bool underlayOk = false;
        const NetworkDocument underlay = readEffectiveNetworkDocument(sourcePath, false, underlayOk);
        if (!underlayOk) {
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        bool effectiveOk = false;
        const NetworkDocument effective = readEffectiveNetworkDocument(sourcePath, true, effectiveOk);
        if (!effectiveOk) {
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        const NetworkFileAnalysis analysis = analyzeNetworkDocument(underlay);
        if (!canInterface && !analysis.supported) {
            qWarning() << analysis.warning << "for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        qInfo().noquote() << "Writing network settings for" << interfaceName
                          << "source=" << sourcePath
                          << "target=" << targetPath;

        NetworkDocument overlay;
        if (canInterface) {
            quint64 baselineRate = 0;
            bool hasBaseline = canBitRateInDocument(underlay, baselineRate);
            if (!hasBaseline) hasBaseline = kernelCanBitRate(interfaceName, baselineRate);
            const bool needsOverride = !hasBaseline || requestedCanBitRate != baselineRate;
            const NetworkDocument currentOverlay = QFile::exists(targetPath) ? readDocument(targetPath) : NetworkDocument{};
            if (!buildCanOverlayDocument(currentOverlay, requestedCanBitRate, needsOverride, overlay)) {
                return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
            }
        } else if (!buildOverlayDocument(underlay, request.parameters, overlay, &effective)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        if (overlay.lines.isEmpty()) {
            if (isUiOwnedOverlay(targetPath) && !QFile::remove(targetPath)) {
                return errorResponse(request, QLatin1String(kErrorWriteFailed));
            }
        } else if (!writeOverlay(targetPath, overlay)) {
            return errorResponse(request, QLatin1String(kErrorWriteFailed));
        }
        const bool canBitRateOverride = canInterface && hasCanBitRateDirective(overlay);
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterNetworkFile), sourcePath},
                 {QStringLiteral("overlay_file"), targetPath},
                 {QLatin1String(kParameterUserOverride), isUiOwnedOverlay(targetPath)},
                 {QLatin1String(kParameterCanBitRateOverride), canBitRateOverride}},
                true, true};
    }

    if (request.action == QLatin1String(kActionResetSettings)) {
        if (!isConfigurableNetworkInterface(interfaceName)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        bool statusOk = false;
        const QString networkFile = networkFileFromStatus(interfaceName, statusOk);
        if (!statusOk) {
            return errorResponse(request, QLatin1String(kErrorStatusFailed));
        }
        const QString overlayPath = userNetworkOverlayPath(networkFile);
        if (overlayPath.isEmpty()) {
            return errorResponse(request, QLatin1String(kErrorNetworkFileNotFound));
        }
        const QString ownershipError = resetOverlayOwnershipError(overlayPath);
        if (!ownershipError.isEmpty()) {
            return errorResponse(request, ownershipError);
        }
        const QString interfaceKind = networkInterfaceKind(interfaceName);
        if (isCanInterfaceKind(interfaceKind)) {
            g_pendingCanBitRateResets.insert(interfaceName);
            g_pendingCanBitRateResetPaths.insert(interfaceName, overlayPath);
        } else {
            g_pendingResetInterfaces.insert(interfaceName);
            g_pendingResetOverlayPaths.insert(interfaceName, overlayPath);
        }
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterInterface), interfaceName},
                 {QLatin1String(kParameterUserOverride), isUiOwnedOverlay(overlayPath)},
                 {QLatin1String(kParameterCanBitRateOverride),
                  [&overlayPath]() {
                      return !overlayPath.isEmpty() && QFile::exists(overlayPath) &&
                             hasCanBitRateDirective(readDocument(overlayPath));
                  }()},
                 {QLatin1String(kParameterResetStaged), true}},
                true, true};
    }

    if (request.action == QLatin1String(kActionCancelResetSettings)) {
        g_pendingResetInterfaces.remove(interfaceName);
        g_pendingResetOverlayPaths.remove(interfaceName);
        g_pendingCanBitRateResets.remove(interfaceName);
        g_pendingCanBitRateResetPaths.remove(interfaceName);
        bool statusOk = false;
        const QString networkFile = networkFileFromStatus(interfaceName, statusOk);
        const QString overlayPath = statusOk ? userNetworkOverlayPath(networkFile) : QString();
        const bool currentCanOverride = !overlayPath.isEmpty() && QFile::exists(overlayPath) &&
                                        hasCanBitRateDirective(readDocument(overlayPath));
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterInterface), interfaceName},
                 {QLatin1String(kParameterUserOverride),
                  !overlayPath.isEmpty() && isUiOwnedOverlay(overlayPath)},
                 {QLatin1String(kParameterCanBitRateOverride), currentCanOverride},
                 {QLatin1String(kParameterResetStaged), false}},
                true, true};
    }

    if (request.action == QLatin1String(kActionApply)) {
        if (!expertMode) {
            QSet<QString> pendingInterfaces = g_pendingResetInterfaces;
            pendingInterfaces.unite(g_pendingCanBitRateResets);
            for (const QString &pendingInterface : pendingInterfaces) {
                if (!networkDeviceAllowed(pendingInterface, false)) {
                    return errorResponse(request, QLatin1String(kErrorInvalidInterface));
                }
            }
        }
        const ResetApplyResult resetResult = applyPendingNetworkResets(runCommand);
        if (resetResult.writeFailed) {
            return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                    {{QLatin1String(kError), QLatin1String(kErrorWriteFailed)},
                     {QLatin1String(kParameterResetStaged), true}}, false, true};
        }
        if (resetResult.applyFailed) {
            return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                    {{QLatin1String(kError), QLatin1String(kErrorApplyFailed)},
                     {QLatin1String(kParameterResetStaged), true}}, false, true};
        }
        return {request.requestId, QLatin1String(kGroupNetwork), request.action, {}, true, true};
    }

    throw std::runtime_error("NetworkConfiguration::handleRequest got unsupported action");
}
} // namespace NetworkConfiguration
