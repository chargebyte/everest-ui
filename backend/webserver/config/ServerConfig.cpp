// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "ServerConfig.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTextStream>

namespace {
using RawConfigMap = QHash<QString, QString>;

bool resolveBindAddress(const QString &bind, QHostAddress &address) {
    if (bind == QLatin1String("0.0.0.0")) {
        address = QHostAddress::Any;
        return true;
    }
    if (bind == QLatin1String("::")) {
        address = QHostAddress::AnyIPv6;
        return true;
    }
    return address.setAddress(bind);
}

bool isValidBackendWsUrl(const QUrl &url) {
    if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("ws") || scheme == QLatin1String("wss");
}

bool isValidOriginUrl(const QUrl &url) {
    if (!url.isValid() || url.host().isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

bool validateAndReadConfigLines(const QString &path,
                                RawConfigMap &rawParams,
                                QString &baseDir,
                                QString &errorMessage) {
    static const QSet<QString> kAllowedKeys = {
        QStringLiteral("port"),
        QStringLiteral("root"),
        QStringLiteral("bind"),
        QStringLiteral("ws_path"),
        QStringLiteral("backend_ws"),
        QStringLiteral("max_request_bytes"),
        QStringLiteral("log_level"),
        QStringLiteral("allow_origin"),
    };

    rawParams.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = QStringLiteral("Cannot open config file: %1").arg(path);
        return false;
    }

    baseDir = QFileInfo(path).absolutePath();
    QTextStream in(&file);
    int lineNumber = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++lineNumber;
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }

        const int firstEq = trimmed.indexOf('=');
        const int lastEq = trimmed.lastIndexOf('=');
        if (firstEq <= 0 || firstEq != lastEq) {
            errorMessage = QStringLiteral("Invalid config format at line %1: expected key=value")
                               .arg(lineNumber);
            return false;
        }

        const QString key = trimmed.left(firstEq).trimmed();
        const QString value = trimmed.mid(firstEq + 1).trimmed();
        if (!kAllowedKeys.contains(key)) {
            errorMessage = QStringLiteral("Unknown config key at line %1: %2")
                               .arg(lineNumber)
                               .arg(key);
            return false;
        }

        rawParams.insert(key, value);
    }

    return true;
}

bool applyDirectParameters(const RawConfigMap &rawParams,
                           const QString &baseDir,
                           ServerConfig &cfg,
                           QString &errorMessage) {
    static const QSet<QString> kRequiredKeys = {
        QStringLiteral("port"),
        QStringLiteral("root"),
        QStringLiteral("bind"),
        QStringLiteral("ws_path"),
        QStringLiteral("backend_ws"),
        QStringLiteral("max_request_bytes"),
        QStringLiteral("log_level"),
    };

    cfg = ServerConfig{};
    for (const QString &key : kRequiredKeys) {
        if (!rawParams.contains(key)) {
            errorMessage = QStringLiteral("Missing required config key: %1").arg(key);
            return false;
        }
    }

    bool ok = false;
    const uint parsedPort = rawParams.value(QStringLiteral("port")).toUInt(&ok);
    if (!ok || parsedPort == 0 || parsedPort > 65535) {
        errorMessage = QStringLiteral("Invalid port: %1")
                           .arg(rawParams.value(QStringLiteral("port")));
        return false;
    }
    cfg.port = static_cast<quint16>(parsedPort);

    cfg.root = rawParams.value(QStringLiteral("root"));
    if (cfg.root.isEmpty()) {
        errorMessage = QStringLiteral("root must not be empty");
        return false;
    }
    if (QDir::isRelativePath(cfg.root)) {
        cfg.root = QDir(baseDir).absoluteFilePath(cfg.root);
    }

    cfg.bind = rawParams.value(QStringLiteral("bind"));
    if (cfg.bind.isEmpty()) {
        errorMessage = QStringLiteral("bind must not be empty");
        return false;
    }

    cfg.wsPath = rawParams.value(QStringLiteral("ws_path"));
    if (cfg.wsPath.isEmpty()) {
        errorMessage = QStringLiteral("ws_path must not be empty");
        return false;
    }

    cfg.backendWs = rawParams.value(QStringLiteral("backend_ws"));
    if (cfg.backendWs.isEmpty()) {
        errorMessage = QStringLiteral("backend_ws must not be empty");
        return false;
    }

    cfg.maxRequestBytes = rawParams.value(QStringLiteral("max_request_bytes")).toInt(&ok);
    if (!ok || cfg.maxRequestBytes <= 0) {
        errorMessage = QStringLiteral("Invalid max_request_bytes: %1")
                           .arg(rawParams.value(QStringLiteral("max_request_bytes")));
        return false;
    }

    cfg.logLevel = rawParams.value(QStringLiteral("log_level"));
    if (cfg.logLevel.isEmpty()) {
        errorMessage = QStringLiteral("log_level must not be empty");
        return false;
    }

    if (rawParams.contains(QStringLiteral("allow_origin"))) {
        cfg.allowOrigin = rawParams.value(QStringLiteral("allow_origin"));
        if (!cfg.allowOrigin.trimmed().isEmpty()) {
            const QUrl originUrl(cfg.allowOrigin.trimmed());
            if (!isValidOriginUrl(originUrl)) {
                errorMessage = QStringLiteral("Invalid allow_origin URL: %1").arg(cfg.allowOrigin);
                return false;
            }
        }
    }

    return true;
}

bool deriveFinalParameters(ServerConfig &cfg, QString &errorMessage) {
    cfg.backendUrl = QUrl(cfg.backendWs);
    if (!isValidBackendWsUrl(cfg.backendUrl)) {
        errorMessage = QStringLiteral("Invalid backend_ws URL: %1").arg(cfg.backendWs);
        return false;
    }

    if (!resolveBindAddress(cfg.bind, cfg.bindAddress)) {
        errorMessage = QStringLiteral("Invalid bind address: %1").arg(cfg.bind);
        return false;
    }

    cfg.canonicalRoot = QFileInfo(cfg.root).canonicalFilePath();
    if (cfg.canonicalRoot.isEmpty()) {
        cfg.canonicalRoot = QFileInfo(cfg.root).absoluteFilePath();
    }
    if (!cfg.canonicalRoot.endsWith('/')) {
        cfg.canonicalRoot += '/';
    }

    cfg.normalizedWsPath = cfg.wsPath.trimmed();
    if (cfg.normalizedWsPath.isEmpty()) {
        errorMessage = QStringLiteral("ws_path must not be empty");
        return false;
    }
    if (!cfg.normalizedWsPath.startsWith('/')) {
        cfg.normalizedWsPath.prepend('/');
    }

    cfg.enforceOrigin = false;
    cfg.allowOriginUrl = QUrl();
    if (!cfg.allowOrigin.trimmed().isEmpty()) {
        cfg.allowOriginUrl = QUrl(cfg.allowOrigin.trimmed());
        cfg.enforceOrigin = true;
    }

    cfg.debugLog = cfg.logLevel.compare(QStringLiteral("debug"), Qt::CaseInsensitive) == 0;
    return true;
}
} // namespace

bool loadAndValidateServerConfig(const QString &configPath,
                                 ServerConfig &cfg,
                                 QString &errorMessage) {
    RawConfigMap rawParams;
    QString baseDir;
    if (!validateAndReadConfigLines(configPath, rawParams, baseDir, errorMessage)) {
        return false;
    }
    if (!applyDirectParameters(rawParams, baseDir, cfg, errorMessage)) {
        return false;
    }
    if (!deriveFinalParameters(cfg, errorMessage)) {
        return false;
    }
    return true;
}
