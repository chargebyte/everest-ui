// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SystemdService.hpp"

#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusVariant>

SystemdService::SystemdService(QObject *parent)
    : QObject(parent)
    , m_systemdManagerInterface(nullptr)
{
    init();
}

void SystemdService::init()
{
    if (!isSystemBusAvailable()) {
        return;
    }

    m_systemdManagerInterface = new QDBusInterface(
        QStringLiteral("org.freedesktop.systemd1"),
        QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"),
        QDBusConnection::systemBus(),
        this);

    if (!isManagerInterfaceAvailable()) {
        m_systemdManagerInterface->deleteLater();
        m_systemdManagerInterface = nullptr;
    }
}

bool SystemdService::isSystemBusAvailable() const
{
    return QDBusConnection::systemBus().isConnected();
}

bool SystemdService::isManagerInterfaceAvailable() const
{
    return m_systemdManagerInterface != nullptr &&
           m_systemdManagerInterface->isValid();
}

bool SystemdService::restartUnit(const QString &unitName)
{
    if (!isSystemBusAvailable()) {
        return false;
    }

    if (!isManagerInterfaceAvailable()) {
        return false;
    }

    QDBusMessage query = m_systemdManagerInterface->call(
        QStringLiteral("RestartUnit"),
        unitName,
        QStringLiteral("replace"));
    if (query.type() != QDBusMessage::ReplyMessage) {
        return false;
    }

    return true;
}

bool SystemdService::stopUnit(const QString &unitName)
{
    if (!isSystemBusAvailable()) {
        return false;
    }

    if (!isManagerInterfaceAvailable()) {
        return false;
    }

    QDBusMessage query = m_systemdManagerInterface->call(
        QStringLiteral("StopUnit"),
        unitName,
        QStringLiteral("replace"));
    if (query.type() != QDBusMessage::ReplyMessage) {
        return false;
    }

    return true;
}

bool SystemdService::isUnitActive(const QString &unitName)
{
    if (!isSystemBusAvailable()) {
        return false;
    }

    if (!isManagerInterfaceAvailable()) {
        return false;
    }

    QDBusMessage getUnitQuery =
        m_systemdManagerInterface->call(QStringLiteral("GetUnit"), unitName);
    if (getUnitQuery.type() != QDBusMessage::ReplyMessage ||
        getUnitQuery.arguments().isEmpty()) {
        return false;
    }

    const QDBusObjectPath unitPath =
        qdbus_cast<QDBusObjectPath>(getUnitQuery.arguments().at(0));
    QDBusInterface unitInterface(QStringLiteral("org.freedesktop.systemd1"),
                                 unitPath.path(),
                                 QStringLiteral("org.freedesktop.DBus.Properties"),
                                 QDBusConnection::systemBus());
    if (!unitInterface.isValid()) {
        return false;
    }

    QDBusMessage activeStateQuery =
        unitInterface.call(QStringLiteral("Get"),
                           QStringLiteral("org.freedesktop.systemd1.Unit"),
                           QStringLiteral("ActiveState"));
    if (activeStateQuery.type() != QDBusMessage::ReplyMessage ||
        activeStateQuery.arguments().isEmpty()) {
        return false;
    }

    return activeStateQuery.arguments().at(0).value<QDBusVariant>().variant().toString() ==
           QStringLiteral("active");
}
