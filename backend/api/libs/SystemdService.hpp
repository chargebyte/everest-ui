// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SYSTEMD_SERVICE_HPP
#define SYSTEMD_SERVICE_HPP

#include <QObject>
#include <QDBusInterface>

class SystemdService : public QObject
{
    Q_OBJECT
public:
    explicit SystemdService(QObject *parent = nullptr);

    bool restartUnit(const QString &unitName);
    bool stopUnit(const QString &unitName);
    bool isUnitActive(const QString &unitName);

private:
    void init();
    bool isSystemBusAvailable() const;
    bool isManagerInterfaceAvailable() const;

    QDBusInterface *m_systemdManagerInterface;
};

#endif // SYSTEMD_SERVICE_HPP
