// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "BackendConfig.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace {
QStringList backendConfigCandidates() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    return {
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

        const QStringList parts = line.split(QLatin1Char('='));
        if (parts.size() != 2) {
            continue;
        }

        const QString key = parts.at(0).trimmed();
        if (key != configKey) {
            continue;
        }

        return parts.at(1).trimmed();
    }

    return QString();
}
