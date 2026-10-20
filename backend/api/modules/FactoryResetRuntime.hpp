// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_FACTORY_RESET_RUNTIME_HPP
#define EVEREST_UI_FACTORY_RESET_RUNTIME_HPP

#include "RequestResponseTypes.hpp"

#include <QList>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include <functional>

class FactoryResetRuntime final : public QObject {
    Q_OBJECT
public:
    struct Actions {
        std::function<QString(const QString&)> config;
        std::function<QJsonObject()> effectiveConfig;
        std::function<bool(const QList<int>&)> unplugged;
    };
    explicit FactoryResetRuntime(Actions actions, QObject* parent = nullptr);
    ~FactoryResetRuntime() override;
    ModuleResponse handleRequest(const ModuleRequest& request, bool maintenanceBusy = false);
    bool isRunning() const {
        return m_running;
    }
    bool isRebootExpected() const {
        return m_rebootExpected;
    }
    static QList<int> resolveEvseIndices(const QJsonObject& configuration);

signals:
    void responseReady(const ModuleResponse& response);

private:
    bool eligible();
    void start();
    void stop();
    void finish(bool success, bool attempted);
    void drainOutput();
    ModuleResponse response(const ModuleRequest& request, bool success, const QJsonObject& parameters,
                            bool final = true) const;

    Actions m_actions;
    ModuleRequest m_request;
    QProcess m_process;
    QTimer m_timeout;
    QTimer m_killTimer;
    QTimer m_keepalive;
    QString m_state = QStringLiteral("idle");
    QString m_command;
    QByteArray m_stdout;
    QByteArray m_stderr;
    bool m_running = false;
    bool m_checking = false;
    bool m_timedOut = false;
    bool m_rebootExpected = false;
    int m_executionTimeoutMs = 20000;
};

#endif
