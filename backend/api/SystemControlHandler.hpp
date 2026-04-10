// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef SYSTEM_CONTROL_HPP
#define SYSTEM_CONTROL_HPP

#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QString>

#include <optional>

#include "RequestResponseTypes.hpp"

class SystemControl final : public QObject {
    Q_OBJECT

public:
    explicit SystemControl(class RpcApiClient *rpcApiClient, QObject *parent = nullptr);

public slots:
    void enqueueRequest(const QJsonObject &request);

signals:
    void responseReady(const QJsonObject &response);

private:
    void processQueue();
    void startRequest(const ModuleRequest &request);
    void handleModuleResponse(const ModuleResponse &response);
    void emitResponse(const ModuleResponse &response);
    ModuleGroup toModuleGroup(const QString &group) const;

    QQueue<ModuleRequest> m_queue;
    std::optional<ModuleRequest> m_pendingRequest;
};

#endif // SYSTEM_CONTROL_HPP
