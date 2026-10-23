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

    bool restartUnit(const QString& unitName);
    bool startUnit(const QString& unitName);
    bool stopUnit(const QString& unitName);
    bool enableUnit(const QString& unitName);
    bool disableUnit(const QString& unitName);
    bool reloadManager();
    bool isUnitActive(const QString& unitName);
    bool waitForUnitActive(const QString& unitName, bool active, int timeoutMs);
    bool waitForUnitStopped(const QString& unitName, int timeoutMs);
    bool isServiceStoppedForRecovery(const QString& unitName);
    bool isUnitAvailable(const QString& unitName);
    bool isUnitEnabled(const QString& unitName);

protected:
    virtual bool readUnitState(const QString& unitName, QString* state);
    virtual bool readServiceProcessIds(const QString& unitName, quint32* mainPid, quint32* controlPid);

private:
    void init();
    bool isSystemBusAvailable() const;
    bool isManagerInterfaceAvailable() const;
    bool callUnitJob(const QString& method, const QString& unitName);
    bool callEnableDisableUnitFiles(const QString &method, const QString &unitName);
    bool readUnitActiveState(const QString &unitName, bool *active);
    bool readUnitFileState(const QString &unitName, QString *state);
    QString unitPath(const QString& unitName) const;

    QDBusInterface *m_systemdManagerInterface;
};

#endif // SYSTEMD_SERVICE_HPP
