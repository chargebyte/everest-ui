// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "BackendConfig.hpp"

#include <QDebug>
#include <QFile>
#include <QSet>

UiConfig::Snapshot readBackendConfiguration() {
    static UiConfig::Store config(UiConfig::pathsFor(UiConfig::Kind::Backend));
    return config.read();
}

QString readBackendConfigValue(const QString& configKey) {
    return readBackendConfiguration().value(configKey);
}

QMap<QString, QString> readBackendConfigValues(const QString& keyPrefix) {
    return readBackendConfiguration().values(keyPrefix);
}

QByteArray readDeviceTreeCompatibleData() {
    QFile compatibleFile(QStringLiteral("/proc/device-tree/compatible"));
    if (!compatibleFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    return compatibleFile.readAll();
}

QJsonObject backendFeatures(const UiConfig::Snapshot& config, const QByteArray& compatibleData) {
    QJsonObject features{{QStringLiteral("network"), false}, {QStringLiteral("safety"), false}};
    if (!config.valid()) {
        return features;
    }
    const QString baseKey = QStringLiteral("available_features");
    QString selectedKey = baseKey;
    for (const QByteArray& entry : compatibleData.split('\0')) {
        if (entry.isEmpty()) {
            continue;
        }
        const QString key = baseKey + QLatin1Char('.') + QString::fromUtf8(entry);
        if (config.contains(key)) {
            selectedKey = key;
            break;
        }
    }
    if (!config.contains(selectedKey)) {
        for (auto it = features.begin(); it != features.end(); ++it) {
            it.value() = true;
        }
        return features;
    }
    for (const QString& entry : config.value(selectedKey).split(QLatin1Char(','))) {
        const QString feature = entry.trimmed();
        if (features.contains(feature)) {
            features.insert(feature, true);
        }
    }
    return features;
}

QJsonObject readBackendFeatures() {
    return backendFeatures(readBackendConfiguration(), readDeviceTreeCompatibleData());
}

bool backendFeatureAvailable(const QString& feature) {
    return readBackendFeatures().value(feature).toBool();
}

QStringList backendConfigValueCandidates(const QString& baseValue, const QMap<QString, QString>& platformValues,
                                         const QByteArray& compatibleData) {
    QStringList candidates;
    QSet<QString> seenCompatibleEntries;
    for (const QByteArray& entry : compatibleData.split('\0')) {
        if (entry.isEmpty()) {
            continue;
        }
        const QString compatible = QString::fromUtf8(entry);
        if (seenCompatibleEntries.contains(compatible)) {
            continue;
        }
        seenCompatibleEntries.insert(compatible);
        const auto value = platformValues.constFind(compatible);
        if (value != platformValues.constEnd()) {
            candidates.append(value.value());
        }
    }
    if (!baseValue.isEmpty()) {
        candidates.append(baseValue);
    }
    return candidates;
}

int resolvePositiveIntegerConfigValue(const QStringList& candidates, int defaultValue, int maxValue,
                                      const QString& configKey) {
    for (const QString& candidate : candidates) {
        bool valid = false;
        const int value = candidate.toInt(&valid);
        if (valid && value > 0 && value <= maxValue) {
            return value;
        }
        qWarning().noquote() << QStringLiteral("Ignoring invalid value '%1' for backend config key '%2'; "
                                               "expected a positive integer no greater than %3")
                                    .arg(candidate, configKey)
                                    .arg(maxValue);
    }
    return defaultValue;
}
