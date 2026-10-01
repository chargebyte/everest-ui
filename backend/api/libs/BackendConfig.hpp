// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef BACKEND_CONFIG_HPP
#define BACKEND_CONFIG_HPP

#include <QMap>
#include <QString>

QString resolveBackendConfigPath();
QString readBackendConfigValue(const QString &configKey);
QMap<QString, QString> readBackendConfigValues(const QString &keyPrefix);

#endif // BACKEND_CONFIG_HPP
