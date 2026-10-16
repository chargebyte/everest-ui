// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef BACKEND_CONFIG_HPP
#define BACKEND_CONFIG_HPP

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

QString resolveBackendConfigPath();
QString readBackendConfigValue(const QString& configKey);
QMap<QString, QString> readBackendConfigValues(const QString& keyPrefix);
QByteArray readDeviceTreeCompatibleData();
QStringList backendConfigValueCandidates(const QString& baseValue, const QMap<QString, QString>& platformValues,
                                         const QByteArray& compatibleData);
int resolvePositiveIntegerConfigValue(const QStringList& candidates, int defaultValue, int maxValue,
                                      const QString& configKey);

#endif // BACKEND_CONFIG_HPP
