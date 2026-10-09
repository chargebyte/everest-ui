// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "modules/EverestConfig.hpp"
#include "modules/FirmwareUpdate.hpp"
#include "modules/FirmwareUpdateRuntime.hpp"
#include "modules/SystemLogs.hpp"
#include "modules/OCPPConfig.hpp"
#include "modules/SafetyController.hpp"
#include "modules/NetworkConfiguration.hpp"
#include "RpcApiClient.hpp"
#include "SystemControlHandler.hpp"
#include "ProtocolSchema.hpp"

#include "ResponseBuilder.hpp"

#include <QDebug>

#include <stdexcept>

SystemControl::SystemControl(RpcApiClient *rpcApiClient, QObject *parent)
    : QObject(parent),
      m_rpcApiClient(rpcApiClient) {
    EverestConfig::setRpcApiClient(rpcApiClient);
    SafetyController::setRpcApiClient(rpcApiClient);
    OCPPConfig::setRpcApiClient(rpcApiClient);
    connect(&FirmwareUpdate::runtime(), &FirmwareUpdateRuntime::responseReady, this,
            &SystemControl::handleAsyncFirmwareResponse);
    m_safetyWriteKeepAliveTimer.setInterval(10000);
    connect(&m_safetyWriteKeepAliveTimer, &QTimer::timeout, this, &SystemControl::handleSafetyWriteKeepAlive);
}

void SystemControl::enqueueRequest(const ModuleRequest &request) {
    if (request.group == ModuleGroup::SafetyController &&
        request.action == QLatin1String(kActionReadSettings) && m_safetyWriteInProgress) {
        emit responseReady(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = request.requestId,
            .group = QLatin1String(kGroupSafety),
            .action = request.action,
            .parameters = {{QLatin1String(kError), QStringLiteral("safety_controller_busy")}},
            .success = false,
            .final = true,
        }));
        return;
    }
    if (request.group == ModuleGroup::SafetyController &&
        request.action == QLatin1String(kActionWriteSettings) &&
        (m_safetyWriteInProgress || m_pendingRequest.has_value() || !m_queue.isEmpty())) {
        emit responseReady(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = request.requestId,
            .group = QLatin1String(kGroupSafety),
            .action = request.action,
            .parameters = {{QLatin1String(kError), QStringLiteral("safety_controller_busy")}},
            .success = false,
            .final = true,
        }));
        return;
    }
    m_queue.enqueue(request);
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
        if (request.action == QLatin1String(kActionWriteSettings)) {
            m_safetyWriteInProgress = true;
            const ModuleResponse response = SafetyController::startWriteRequest(
                request, this, [this](const ModuleResponse &finalResponse) {
                    handleModuleResponse(finalResponse);
                });
            if (!response.final) {
                m_safetyWriteKeepAliveTimer.start();
            }
            handleModuleResponse(response);
            return;
        }
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
        if (isFirmwareUpdateStartAccepted(response)) {
            return;
        }
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::SystemLogs: {
        const ModuleResponse response = SystemLogs::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::Network: {
        const ModuleResponse response = NetworkConfiguration::handleRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::System: {
        const ModuleResponse response = handleSystemRequest(request);
        handleModuleResponse(response);
        return;
    }
    case ModuleGroup::Unknown: {
        qWarning() << "Ignoring request with unsupported module group";
        m_pendingRequest.reset();
        processQueue();
        return;
    }
    }

    throw std::runtime_error("SystemControl::startRequest reached unreachable code");
}

ModuleResponse SystemControl::handleSystemRequest(const ModuleRequest &request) const {
    if (request.action != QLatin1String(kActionReadAppTitle)) {
        return ModuleResponse{
            .requestId = request.requestId,
            .group = QLatin1String(kGroupSystem),
            .action = request.action,
            .parameters = QJsonObject{
                {QLatin1String(kError), QStringLiteral("unsupported_action")},
            },
            .success = false,
            .final = true,
        };
    }

    return ModuleResponse{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupSystem),
        .action = request.action,
        .parameters = QJsonObject{
            {QStringLiteral("appTitle"), m_rpcApiClient ? m_rpcApiClient->appTitle() : QString()},
        },
        .success = true,
        .final = true,
    };
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
    if (!response.final) {
        return;
    }
    if (pendingRequest.group == ModuleGroup::SafetyController &&
        pendingRequest.action == QLatin1String(kActionWriteSettings)) {
        m_safetyWriteKeepAliveTimer.stop();
        m_safetyWriteInProgress = false;
    }
    m_pendingRequest.reset();
    processQueue();
}

void SystemControl::handleSafetyWriteKeepAlive() {
    if (!m_pendingRequest.has_value() || m_pendingRequest->group != ModuleGroup::SafetyController ||
        m_pendingRequest->action != QLatin1String(kActionWriteSettings)) {
        m_safetyWriteKeepAliveTimer.stop();
        return;
    }
    emitResponse(ModuleResponse{
        .requestId = m_pendingRequest->requestId,
        .group = QLatin1String(kGroupSafety),
        .action = m_pendingRequest->action,
        .parameters = {},
        .success = true,
        .final = false,
    });
}

void SystemControl::handleAsyncFirmwareResponse(const ModuleResponse &response) {
    if (!m_pendingRequest.has_value()) {
        throw std::runtime_error("SystemControl::handleAsyncFirmwareResponse got response without pending request");
    }

    const ModuleRequest &pendingRequest = *m_pendingRequest;
    if (!isMatchingAsyncFirmwareResponse(response, pendingRequest)) {
        throw std::runtime_error("SystemControl::handleAsyncFirmwareResponse got mismatching response");
    }

    emitResponse(response);
    if (!isFinalAsyncFirmwareResponse(response, pendingRequest)) {
        return;
    }

    m_pendingRequest.reset();
    processQueue();
}

void SystemControl::emitResponse(const ModuleResponse &response) {
    emit responseReady(ResponseBuilder::buildResponse(response));
}

ModuleGroup SystemControl::toModuleGroup(const QString &group) const {
    if (group == QLatin1String(kGroupEverest)) {
        return ModuleGroup::EverestConfig;
    }
    if (group == QLatin1String(kGroupSafety)) {
        return ModuleGroup::SafetyController;
    }
    if (group == QLatin1String(kGroupOcpp)) {
        return ModuleGroup::OCPPConfig;
    }
    if (group == QLatin1String(kGroupFirmware)) {
        return ModuleGroup::FirmwareUpdate;
    }
    if (group == QLatin1String(kGroupSystemLogs)) {
        return ModuleGroup::SystemLogs;
    }
    if (group == QLatin1String(kGroupNetwork)) {
        return ModuleGroup::Network;
    }
    if (group == QLatin1String(kGroupSystem)) {
        return ModuleGroup::System;
    }

    return ModuleGroup::Unknown;
}

bool SystemControl::isFirmwareUpdateStartAccepted(const ModuleResponse &response) const {
    return response.group == QLatin1String(kGroupFirmware) &&
           response.action == QLatin1String(kActionUpdateImage) &&
           response.success &&
           response.parameters.isEmpty();
}

bool SystemControl::isMatchingAsyncFirmwareResponse(const ModuleResponse &response,
                                                    const ModuleRequest &pendingRequest) const {
    if (pendingRequest.group != ModuleGroup::FirmwareUpdate) {
        return false;
    }

    if (response.requestId != pendingRequest.requestId ||
        toModuleGroup(response.group) != pendingRequest.group) {
        return false;
    }

    return response.action == pendingRequest.action ||
           response.action == pendingRequest.action + QStringLiteral(".") + QLatin1String(kActionProgress);
}

bool SystemControl::isFinalAsyncFirmwareResponse(const ModuleResponse &response,
                                                 const ModuleRequest &pendingRequest) const {
    if (response.action != pendingRequest.action) {
        return false;
    }

    if (!response.success) {
        return true;
    }

    return !response.parameters.isEmpty();
}
