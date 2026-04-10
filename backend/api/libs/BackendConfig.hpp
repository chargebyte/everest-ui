// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef BACKEND_CONFIG_HPP
#define BACKEND_CONFIG_HPP

#include <QString>

QString resolveBackendConfigPath();
QString readBackendConfigValue(const QString &configKey);

#endif // BACKEND_CONFIG_HPP
