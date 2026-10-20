// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "RebootSettings.hpp"
#include "BackendConfig.hpp"
#include "BootIdentity.hpp"

#include <limits>

int resolveRebootWaitSeconds(const QString& base, const QMap<QString, QString>& platforms,
                             const QByteArray& compatible) {
    return resolvePositiveIntegerConfigValue(backendConfigValueCandidates(base, platforms, compatible), 30,
                                             std::numeric_limits<int>::max() / 1000,
                                             QStringLiteral("reboot_wait_timeout_seconds"));
}

QJsonObject readRebootSettings() {
    const auto config = readBackendConfiguration();
    const auto seconds = resolveRebootWaitSeconds(config.value(QStringLiteral("reboot_wait_timeout_seconds")),
                                                  config.values(QStringLiteral("reboot_wait_timeout_seconds.")),
                                                  readDeviceTreeCompatibleData());
    return {{QStringLiteral("wait_timeout_seconds"), seconds}, {QStringLiteral("boot_id"), UiRuntime::deviceBootId()}};
}
