// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SystemdService.hpp"

#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QStringList>

#include <chrono>
#include <thread>

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
    return callUnitJob(QStringLiteral("RestartUnit"), unitName);
}

bool SystemdService::startUnit(const QString &unitName)
{
    return callUnitJob(QStringLiteral("StartUnit"), unitName);
}

bool SystemdService::stopUnit(const QString &unitName)
{
    return callUnitJob(QStringLiteral("StopUnit"), unitName);
}

bool SystemdService::enableUnit(const QString &unitName)
{
    return callEnableDisableUnitFiles(QStringLiteral("EnableUnitFiles"), unitName);
}

bool SystemdService::disableUnit(const QString &unitName)
{
    return callEnableDisableUnitFiles(QStringLiteral("DisableUnitFiles"), unitName);
}

bool SystemdService::callUnitJob(const QString &method, const QString &unitName)
{
    if (!isSystemBusAvailable()) {
        return false;
    }

    if (!isManagerInterfaceAvailable()) {
        return false;
    }

    QDBusMessage query = m_systemdManagerInterface->call(
        method,
        unitName,
        QStringLiteral("replace"));
    if (query.type() != QDBusMessage::ReplyMessage) {
        return false;
    }

    return true;
}

bool SystemdService::callEnableDisableUnitFiles(const QString &method, const QString &unitName)
{
    if (!isSystemBusAvailable()) {
        return false;
    }

    if (!isManagerInterfaceAvailable()) {
        return false;
    }

    QDBusMessage query;
    if (method == QStringLiteral("EnableUnitFiles")) {
        query = m_systemdManagerInterface->call(
            method,
            QStringList{unitName},
            false,
            true);
    } else {
        query = m_systemdManagerInterface->call(
            method,
            QStringList{unitName},
            false);
    }
    if (query.type() != QDBusMessage::ReplyMessage) {
        return false;
    }

    return true;
}

bool SystemdService::isUnitActive(const QString &unitName)
{
    bool active = false;
    return readUnitActiveState(unitName, &active) && active;
}

bool SystemdService::waitForUnitActive(const QString &unitName, bool active, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    do {
        bool currentState = false;
        if (readUnitActiveState(unitName, &currentState) && currentState == active) {
            return true;
        }
        if (timer.elapsed() < timeoutMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } while (timer.elapsed() < timeoutMs);
    return false;
}

bool SystemdService::readUnitActiveState(const QString &unitName, bool *active)
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

    const QString state = activeStateQuery.arguments().at(0).value<QDBusVariant>().variant().toString();
    if (state != QStringLiteral("active") && state != QStringLiteral("inactive") &&
        state != QStringLiteral("failed") && state != QStringLiteral("activating") &&
        state != QStringLiteral("deactivating")) {
        return false;
    }
    *active = state == QStringLiteral("active");
    return true;
}

bool SystemdService::isUnitEnabled(const QString &unitName)
{
    if (!isSystemBusAvailable()) {
        return false;
    }

    if (!isManagerInterfaceAvailable()) {
        return false;
    }

    QDBusMessage query =
        m_systemdManagerInterface->call(QStringLiteral("GetUnitFileState"), unitName);
    if (query.type() != QDBusMessage::ReplyMessage || query.arguments().isEmpty()) {
        return false;
    }

    const QString state = query.arguments().at(0).toString();
    return state == QStringLiteral("enabled") || state == QStringLiteral("static") ||
           state == QStringLiteral("indirect") || state == QStringLiteral("generated");
}
