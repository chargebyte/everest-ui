// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "BackendConfig.hpp"
#include "InstallPaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace {
QStringList backendConfigCandidates() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    return {
        QStringLiteral(EVEREST_UI_INSTALL_BACKEND_CONFIG),
        QDir(applicationDir).filePath(QStringLiteral("../config/backend.conf")),
        QDir(applicationDir).filePath(QStringLiteral("backend.conf")),
    };
}
}

QString resolveBackendConfigPath() {
    const QStringList candidates = backendConfigCandidates();
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return candidates.constFirst();
}

QString readBackendConfigValue(const QString &configKey) {
    QFile configFile(resolveBackendConfigPath());
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream stream(&configFile);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        const QString key = line.left(separator).trimmed();
        if (key != configKey) {
            continue;
        }

        return line.mid(separator + 1).trimmed();
    }

    return QString();
}

QMap<QString, QString> readBackendConfigValues(const QString &keyPrefix) {
    QMap<QString, QString> values;
    QFile configFile(resolveBackendConfigPath());
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return values;
    }

    QTextStream stream(&configFile);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed();
        if (key.startsWith(keyPrefix) && key.size() > keyPrefix.size()) {
            values.insert(key.mid(keyPrefix.size()), line.mid(separator + 1).trimmed());
        }
    }
    return values;
}
