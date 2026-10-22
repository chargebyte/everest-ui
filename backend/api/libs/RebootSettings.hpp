// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_REBOOT_SETTINGS_HPP
#define EVEREST_UI_REBOOT_SETTINGS_HPP

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>

int resolveRebootWaitSeconds(const QString& base, const QMap<QString, QString>& platforms,
                             const QByteArray& compatible);
QJsonObject readRebootSettings();

#endif
