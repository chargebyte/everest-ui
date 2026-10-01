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

QSet<QString> g_pendingResetInterfaces;
QHash<QString, QString> g_pendingResetOverlayPaths;
bool g_resetRecoveryDone = false;

QString keyName(const QString &line, QString &value);
NetworkDocument readDocument(const QString &path);
bool isIpv4Cidr(const QString &value);
bool isIpv4Address(const QString &value);

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

ResetApplyResult applyPendingResets(QSet<QString> &pendingInterfaces,
                                    const std::function<QString(const QString &)> &pathForInterface,
                                    const CommandRunner &run) {
    QList<QPair<QString, QString>> backups;
    QList<QPair<QString, QString>> committedBackups;
    QStringList interfaces = pendingInterfaces.values();
    std::sort(interfaces.begin(), interfaces.end());

    for (const QString &interfaceName : interfaces) {
        const QString originalPath = pathForInterface(interfaceName);
        if (!QFile::exists(originalPath)) {
            continue;
        }
        if (!isUiOwnedOverlay(originalPath)) {
            restoreResetBackups(backups);
            return {false, true, false};
        }
        const QString backupPath = resetBackupPath(originalPath);
        if (QFile::exists(backupPath) || !QFile::rename(originalPath, backupPath)) {
            restoreResetBackups(backups);
            return {false, true, false};
        }
        backups.append(qMakePair(originalPath, backupPath));
    }

    if (!applyNetworkConfiguration(run)) {
        const bool restored = restoreResetBackups(backups);
        if (restored) {
            applyNetworkConfiguration(run);
        }
        return {false, false, true};
    }

    for (auto it = backups.begin(); it != backups.end(); ++it) {
        const QString committedPath = resetCommittedPath(it->first);
        if (QFile::exists(committedPath) || !QFile::rename(it->second, committedPath)) {
            const bool reverted = revertCommittedBackups(committedBackups);
            const bool restored = reverted && restoreResetBackups(backups);
            if (restored) {
                applyNetworkConfiguration(run);
            }
            return {false, false, true};
        }
        committedBackups.append(*it);
    }

    for (const auto &backup : backups) {
        const QString committedPath = resetCommittedPath(backup.first);
        if (QFile::exists(committedPath) && !QFile::remove(committedPath)) {
            qWarning() << "Unable to remove committed network reset backup" << committedPath;
        }
    }
    pendingInterfaces.clear();
    return {true, false, false};
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
        {QLatin1String(kParameterProbablyIsoHighLevelComms), info.probablyIsoHighLevelComms},
        {QLatin1String(kParameterEditable), !special && info.kind.compare(QStringLiteral("ether"), Qt::CaseInsensitive) == 0},
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

bool isStandardEtherInterface(const QString &interfaceName) {
    bool success = false;
    const QList<InterfaceInfo> interfaces = readInterfaces(success);
    if (!success) {
        return false;
    }
    for (const InterfaceInfo &info : interfaces) {
        if (info.name == interfaceName) {
            return info.kind.compare(QStringLiteral("ether"), Qt::CaseInsensitive) == 0 && !info.loopback;
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
        const NetworkFileAnalysis analysis = analyzeNetworkDocument(underlay);
        if (!analysis.supported) {
            qWarning() << analysis.warning << "for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        const QString overlayPath = userNetworkOverlayPath(networkFile);
        if (!overlayPath.isEmpty() && QFile::exists(overlayPath) && !isUiOwnedOverlay(overlayPath) &&
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
        const bool standardEther = isStandardEtherInterface(interfaceName);
        parameters.insert(QLatin1String(kParameterEditable), !networkFile.isEmpty() && standardEther);
        parameters.insert(QLatin1String(kParameterWarning), QJsonArray{});
        parameters.insert(QLatin1String(kParameterUserOverride),
                           !overlayPath.isEmpty() && isUiOwnedOverlay(overlayPath));
        parameters.insert(QLatin1String(kParameterResetStaged), g_pendingResetInterfaces.contains(interfaceName));
        parameters = publicNetworkSettings(parameters);
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                parameters, true, true};
    }

    if (request.action == QLatin1String(kActionWriteSettings)) {
        if (!isStandardEtherInterface(interfaceName)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        QString validationError;
        if (!validateSettings(request.parameters, validationError)) {
            return errorResponse(request, QLatin1String(kErrorInvalidSettings) + QStringLiteral(": ") + validationError);
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
        if (!analysis.supported) {
            qWarning() << analysis.warning << "for" << interfaceName;
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        qInfo().noquote() << "Writing network settings for" << interfaceName
                          << "source=" << sourcePath
                          << "target=" << targetPath;

        NetworkDocument overlay;
        if (!buildOverlayDocument(underlay, request.parameters, overlay, &effective)) {
            return errorResponse(request, QLatin1String(kErrorUnsupportedConfiguration));
        }
        if (overlay.lines.isEmpty()) {
            if (isUiOwnedOverlay(targetPath) && !QFile::remove(targetPath)) {
                return errorResponse(request, QLatin1String(kErrorWriteFailed));
            }
        } else if (!writeOverlay(targetPath, overlay)) {
            return errorResponse(request, QLatin1String(kErrorWriteFailed));
        }
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterNetworkFile), sourcePath},
                 {QStringLiteral("overlay_file"), targetPath},
                 {QLatin1String(kParameterUserOverride), isUiOwnedOverlay(targetPath)}},
                true, true};
    }

    if (request.action == QLatin1String(kActionResetSettings)) {
        if (!isStandardEtherInterface(interfaceName)) {
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
        if (QFile::exists(overlayPath) && !isUiOwnedOverlay(overlayPath)) {
            return errorResponse(request, QLatin1String(kErrorUnownedDropIn));
        }
        g_pendingResetInterfaces.insert(interfaceName);
        g_pendingResetOverlayPaths.insert(interfaceName, overlayPath);
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterInterface), interfaceName},
                 {QLatin1String(kParameterUserOverride), isUiOwnedOverlay(overlayPath)},
                 {QLatin1String(kParameterResetStaged), true}},
                true, true};
    }

    if (request.action == QLatin1String(kActionCancelResetSettings)) {
        g_pendingResetInterfaces.remove(interfaceName);
        g_pendingResetOverlayPaths.remove(interfaceName);
        bool statusOk = false;
        const QString networkFile = networkFileFromStatus(interfaceName, statusOk);
        const QString overlayPath = statusOk ? userNetworkOverlayPath(networkFile) : QString();
        return {request.requestId, QLatin1String(kGroupNetwork), request.action,
                {{QLatin1String(kParameterInterface), interfaceName},
                 {QLatin1String(kParameterUserOverride),
                  !overlayPath.isEmpty() && isUiOwnedOverlay(overlayPath)},
                 {QLatin1String(kParameterResetStaged), false}},
                true, true};
    }

    if (request.action == QLatin1String(kActionApply)) {
        if (!expertMode) {
            for (const QString &pendingInterface : g_pendingResetInterfaces) {
                if (!networkDeviceAllowed(pendingInterface, false)) {
                    return errorResponse(request, QLatin1String(kErrorInvalidInterface));
                }
            }
        }
        const ResetApplyResult resetResult = applyPendingResets(
            g_pendingResetInterfaces,
            [](const QString &pendingInterface) {
                return g_pendingResetOverlayPaths.value(pendingInterface);
            },
            runCommand);
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
        g_pendingResetOverlayPaths.clear();
        return {request.requestId, QLatin1String(kGroupNetwork), request.action, {}, true, true};
    }

    throw std::runtime_error("NetworkConfiguration::handleRequest got unsupported action");
}
} // namespace NetworkConfiguration
