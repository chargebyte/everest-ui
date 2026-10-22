// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_BOOT_IDENTITY_HPP
#define EVEREST_UI_BOOT_IDENTITY_HPP

#include <QFile>
#include <QRegularExpression>
#include <QString>

namespace UiRuntime {
inline QString deviceBootId() {
    static const QString id = []() {
        QFile file(QStringLiteral("/proc/sys/kernel/random/boot_id"));
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        const auto value = QString::fromLatin1(file.readAll()).trimmed().toLower();
        const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$"));
        return uuid.match(value).hasMatch() ? value : QString();
    }();
    return id;
}
} // namespace UiRuntime

#endif
