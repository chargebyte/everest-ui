// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <QHostAddress>
#include <QString>
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

    // Derived/finalized parameters for consumer components.
    QHostAddress bindAddress = QHostAddress::Any;
    QUrl backendUrl;
    bool debugLog = false;
    QString canonicalRoot;
    QString normalizedWsPath;
    bool enforceOrigin = false;
    QUrl allowOriginUrl;
};

bool loadAndValidateServerConfig(const QString &configPath,
                                 ServerConfig &cfg,
                                 QString &errorMessage);

#endif // SERVER_CONFIG_HPP
