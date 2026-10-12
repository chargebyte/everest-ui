// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QUrl>

struct ServerConfig {
    // Direct parameters from config file.
    quint16 port = 0;
    QString root;
    QString bind;
    QString wsPath;
    QString backendWs;
    int maxRequestBytes = 0;
    QString logLevel;
    QString allowOrigin;
    QString authFile;
    QString appTitle;
    int passwordResetWindowSeconds = 60;
    QString passwordResetBootStatusPath = QStringLiteral("/sys/bus/nvmem/devices/44440000.bbnsm:nvmem0/nvmem");
    QStringList allowedHosts;

    // Derived/finalized parameters for consumer components.
    QHostAddress bindAddress = QHostAddress::Any;
    QUrl backendUrl;
    bool debugLog = false;
    QString canonicalRoot;
    QString normalizedWsPath;
    bool enforceOrigin = false;
    QUrl allowOriginUrl;
    QString canonicalAuthFile;
};

bool loadAndValidateServerConfig(const QString &configPath,
                                 ServerConfig &cfg,
                                 QString &errorMessage);

#endif // SERVER_CONFIG_HPP
