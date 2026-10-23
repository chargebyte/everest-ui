// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "SystemdService.hpp"

#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QStringList>
#include <QTimer>

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

bool SystemdService::reloadManager()
{
    if (!isSystemBusAvailable() || !isManagerInterfaceAvailable()) {
        return false;
    }

    return m_systemdManagerInterface->call(QStringLiteral("Reload")).type() ==
           QDBusMessage::ReplyMessage;
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
    bool currentState = false;
    if (readUnitActiveState(unitName, &currentState) && currentState == active) {
        return true;
    }

    bool reachedRequestedState = false;
    bool waitTimedOut = false;
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;

    pollTimer.setInterval(100);
    pollTimer.setSingleShot(false);
    timeoutTimer.setInterval(timeoutMs);
    timeoutTimer.setSingleShot(true);

    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        bool polledState = false;
        if (!readUnitActiveState(unitName, &polledState) || polledState != active) {
            return;
        }

        reachedRequestedState = true;
        waitLoop.quit();
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &waitLoop, [&]() {
        waitTimedOut = true;
        waitLoop.quit();
    });

    pollTimer.start();
    timeoutTimer.start();
    waitLoop.exec(QEventLoop::ExcludeUserInputEvents);

    pollTimer.stop();
    timeoutTimer.stop();

    return !waitTimedOut && reachedRequestedState;
}

bool SystemdService::readUnitActiveState(const QString& unitName, bool* active) {
    QString state;
    if (!readUnitState(unitName, &state)) {
        return false;
    }
    if (state != QStringLiteral("active") && state != QStringLiteral("inactive") && state != QStringLiteral("failed") &&
        state != QStringLiteral("activating") && state != QStringLiteral("deactivating")) {
        return false;
    }
    *active = state == QStringLiteral("active");
    return true;
}

bool SystemdService::waitForUnitStopped(const QString& unitName, int timeoutMs) {
    // Non-active is insufficient: the unit may still be shutting down, or its state may be unreadable.
    const auto stopped = [&]() {
        QString state;
        return readUnitState(unitName, &state) && state == QStringLiteral("inactive");
    };
    if (stopped()) {
        return true;
    }
    QEventLoop waitLoop;
    QTimer pollTimer;
    QTimer timeoutTimer;
    bool reachedRequestedState = false;
    pollTimer.setInterval(100);
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&pollTimer, &QTimer::timeout, &waitLoop, [&]() {
        if (stopped()) {
            reachedRequestedState = true;
            waitLoop.quit();
        }
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
    pollTimer.start();
    timeoutTimer.start(timeoutMs);
    waitLoop.exec(QEventLoop::ExcludeUserInputEvents);
    return reachedRequestedState;
}

bool SystemdService::readUnitState(const QString& unitName, QString* state) {
    const QString path = unitPath(unitName);
    if (path.isEmpty()) {
        return false;
    }
    QDBusInterface unitInterface(QStringLiteral("org.freedesktop.systemd1"), path,
                                 QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::systemBus());
    if (!unitInterface.isValid()) {
        return false;
    }

    QDBusMessage activeStateQuery = unitInterface.call(
        QStringLiteral("Get"), QStringLiteral("org.freedesktop.systemd1.Unit"), QStringLiteral("ActiveState"));
    if (activeStateQuery.type() != QDBusMessage::ReplyMessage || activeStateQuery.arguments().isEmpty()) {
        return false;
    }

    *state = activeStateQuery.arguments().at(0).value<QDBusVariant>().variant().toString();
    return true;
}

QString SystemdService::unitPath(const QString& unitName) const {
    if (!isSystemBusAvailable()) {
        return {};
    }

    if (!isManagerInterfaceAvailable()) {
        return {};
    }

    QDBusMessage getUnitQuery = m_systemdManagerInterface->call(QStringLiteral("GetUnit"), unitName);
    if (getUnitQuery.type() != QDBusMessage::ReplyMessage || getUnitQuery.arguments().isEmpty()) {
        return {};
    }

    return qdbus_cast<QDBusObjectPath>(getUnitQuery.arguments().at(0)).path();
}

bool SystemdService::readServiceProcessIds(const QString& unitName, quint32* mainPid, quint32* controlPid) {
    const QString path = unitPath(unitName);
    if (path.isEmpty()) {
        return false;
    }
    QDBusInterface unitInterface(QStringLiteral("org.freedesktop.systemd1"), path,
                                 QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::systemBus());
    if (!unitInterface.isValid()) {
        return false;
    }

    const QDBusReply<QVariantMap> properties =
        unitInterface.call(QStringLiteral("GetAll"), QStringLiteral("org.freedesktop.systemd1.Service"));
    if (!properties.isValid()) {
        return false;
    }
    const QVariant main = properties.value().value(QStringLiteral("MainPID"));
    const QVariant control = properties.value().value(QStringLiteral("ControlPID"));
    if (main.userType() != QMetaType::UInt || control.userType() != QMetaType::UInt) {
        return false;
    }
    *mainPid = main.toUInt();
    *controlPid = control.toUInt();
    return true;
}

bool SystemdService::isServiceStoppedForRecovery(const QString& unitName) {
    QString state;
    quint32 mainPid = 0;
    quint32 controlPid = 0;
    if (!readUnitState(unitName, &state) ||
        (state != QStringLiteral("inactive") && state != QStringLiteral("failed")) ||
        !readServiceProcessIds(unitName, &mainPid, &controlPid) || mainPid != 0 || controlPid != 0) {
        return false;
    }
    // Reject a transition during the separate property reads; external restarts are not atomic with this check.
    QString finalState;
    return readUnitState(unitName, &finalState) && finalState == state;
}

bool SystemdService::isUnitEnabled(const QString& unitName) {
    QString state;
    return readUnitFileState(unitName, &state) &&
           (state == QStringLiteral("enabled") || state == QStringLiteral("enabled-runtime"));
}

bool SystemdService::isUnitAvailable(const QString& unitName) {
    QString state;
    return readUnitFileState(unitName, &state);
}

bool SystemdService::readUnitFileState(const QString &unitName, QString *state)
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

    *state = query.arguments().at(0).toString();
    return true;
}
