// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "modules/EverestConfig.hpp"
#include "modules/FirmwareUpdate.hpp"
#include "modules/Logs.hpp"
#include "modules/OCPPConfig.hpp"
#include "modules/SafetyController.hpp"
#include "RpcApiClient.hpp"
#include "SystemControlHandler.hpp"

#include "ResponseBuilder.hpp"

#include <QDebug>

#include <stdexcept>

SystemControl::SystemControl(RpcApiClient *rpcApiClient, QObject *parent)
    : QObject(parent) {
    EverestConfig::setRpcApiClient(rpcApiClient);
}

void SystemControl::enqueueRequest(const QJsonObject &request) {
    ModuleRequest internalRequest{
        .requestId = static_cast<qint64>(request.value(QStringLiteral("requestId")).toDouble()),
        .group = toModuleGroup(request.value(QStringLiteral("group")).toString()),
        .action = request.value(QStringLiteral("action")).toString(),
        .parameters = request.value(QStringLiteral("parameters")).toObject(),
    };

    m_queue.enqueue(internalRequest);
    processQueue();
}

void SystemControl::processQueue() {
    if (m_pendingRequest.has_value()) {
        return;
    }

    if (m_queue.isEmpty()) {
        return;
    }

    m_pendingRequest = m_queue.dequeue();
    startRequest(*m_pendingRequest);
}

void SystemControl::startRequest(const ModuleRequest &request) {
    switch (request.group) {
    case ModuleGroup::EverestConfig: {
        const ModuleResponse response = EverestConfig::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::SafetyController: {
        const ModuleResponse response = SafetyController::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::OCPPConfig: {
        const ModuleResponse response = OCPPConfig::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::FirmwareUpdate: {
        const ModuleResponse response = FirmwareUpdate::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::Logs: {
        const ModuleResponse response = Logs::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::Unknown:
        throw std::runtime_error("SystemControl::startRequest got unsupported group");
    }

    throw std::runtime_error("SystemControl::startRequest reached unreachable code");
}

void SystemControl::handleModuleResponse(const ModuleResponse &response) {
    if (!m_pendingRequest.has_value()) {
        throw std::runtime_error("SystemControl::handleModuleResponse got response without pending request");
    }

    const ModuleRequest &pendingRequest = *m_pendingRequest;
    const bool matchesPendingRequest =
        response.requestId == pendingRequest.requestId &&
        toModuleGroup(response.group) == pendingRequest.group &&
        response.action == pendingRequest.action;

    if (!matchesPendingRequest) {
        throw std::runtime_error("SystemControl::handleModuleResponse got mismatching response");
    }

    emitResponse(response);
    m_pendingRequest.reset();
    processQueue();
}

void SystemControl::emitResponse(const ModuleResponse &response) {
    emit responseReady(ResponseBuilder::buildResponse(response));
}

ModuleGroup SystemControl::toModuleGroup(const QString &group) const {
    if (group == QStringLiteral("everest")) {
        return ModuleGroup::EverestConfig;
    }
    if (group == QStringLiteral("safety")) {
        return ModuleGroup::SafetyController;
    }
    if (group == QStringLiteral("ocpp")) {
        return ModuleGroup::OCPPConfig;
    }
    if (group == QStringLiteral("firmware")) {
        return ModuleGroup::FirmwareUpdate;
    }
    if (group == QStringLiteral("logs")) {
        return ModuleGroup::Logs;
    }

    return ModuleGroup::Unknown;
}
