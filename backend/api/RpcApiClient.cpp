// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RpcApiClient.hpp"

#include "BackendConfig.hpp"
#include "YamlUtils.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace {
constexpr int kApiHelloRequestId = 1;
constexpr int kReconnectTimeoutMs = 2000;
constexpr int kPendingRequestTimeoutMs = 2000;
}

RpcApiClient::RpcApiClient(QObject *parent)
    : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    m_pendingRequestTimer.setSingleShot(true);
    connect(&m_webSocket, &QWebSocket::connected, this, &RpcApiClient::onConnected);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &RpcApiClient::onDisconnected);
    connect(&m_webSocket, &QWebSocket::binaryMessageReceived, this,
            &RpcApiClient::onBinaryMessageReceived);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RpcApiClient::ensureConnected);
    connect(&m_pendingRequestTimer, &QTimer::timeout, this,
            &RpcApiClient::onPendingRequestTimeout);
}

void RpcApiClient::start() {
    m_rpcApiHost = readBackendConfigValue(QStringLiteral("rpc_api_host"));
    m_everestConfigPath = readBackendConfigValue(QStringLiteral("everest_config_path"));
    m_rpcApiConfigured = false;
    m_configurationError.clear();
    m_everestConfigRoot = QJsonObject{};
    m_lastRpcResponse = QJsonObject{};
    m_hasLastRpcResponse = false;

    const YamlLoadResult yamlLoadResult = loadYamlFile(m_everestConfigPath);
    if (!yamlLoadResult.success) {
        m_configurationError = yamlLoadResult.error;
        return;
    }

    m_everestConfigRoot = yamlLoadResult.yamlRoot;

    const RpcApiModuleConfigResult moduleConfigResult = findRpcApiModuleConfig();
    if (!moduleConfigResult.success) {
        m_configurationError = moduleConfigResult.error;
        return;
    }

    const RpcApiUrlResult urlResult = buildRpcApiUrl(moduleConfigResult);
    if (!urlResult.success) {
        m_configurationError = urlResult.error;
        return;
    }

    m_rpcApiUrl = urlResult.url;
    m_rpcApiConfigured = true;
    ensureConnected();
}

bool RpcApiClient::isReady() const {
    return m_rpcApiConfigured &&
           m_webSocket.state() == QAbstractSocket::ConnectedState &&
           m_handshakeComplete;
}

RpcApiEvseStatusResult RpcApiClient::getEvseStatus(int evseIndex) {
    if (!m_rpcApiConfigured) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = m_configurationError.isEmpty()
                         ? QStringLiteral("rpc_api_not_configured")
                         : m_configurationError,
        };
    }

    if (!m_handshakeComplete) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = QStringLiteral("rpc_api_not_connected"),
        };
    }

    if (!m_pendingRequests.isEmpty()) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = QStringLiteral("rpc_api_request_pending"),
        };
    }

    const QJsonObject request{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("EVSE.GetStatus")},
        {QStringLiteral("params"),
         QJsonObject{
             {QStringLiteral("evse_index"), evseIndex},
         }},
    };

    const SendRpcRequestResult sendResult = sendRpcRequest(request);
    if (!sendResult.success) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = sendResult.error,
        };
    }

    QEventLoop waitLoop;
    QTimer waitPollTimer;
    waitPollTimer.setInterval(50);
    waitPollTimer.setSingleShot(false);

    connect(&m_webSocket, &QWebSocket::disconnected, &waitLoop, &QEventLoop::quit);
    connect(&waitPollTimer, &QTimer::timeout, this, [this, &waitLoop, requestId = sendResult.requestId]() {
        if (m_hasLastRpcResponse || !m_pendingRequests.contains(requestId)) {
            waitLoop.quit();
        }
    });

    waitPollTimer.start();
    waitLoop.exec();
    waitPollTimer.stop();

    if (!m_hasLastRpcResponse) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = QStringLiteral("rpc_api_no_response"),
        };
    }

    const QJsonObject response = m_lastRpcResponse;
    m_lastRpcResponse = QJsonObject{};
    m_hasLastRpcResponse = false;

    const QJsonValue errorValue = response.value(QStringLiteral("error"));
    if (errorValue.isObject()) {
        const QJsonObject errorObject = errorValue.toObject();
        const QString message = errorObject.value(QStringLiteral("message")).toString();
        const QString data = errorObject.value(QStringLiteral("data")).toString();

        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = !message.isEmpty() ? message
                   : !data.isEmpty()    ? data
                                        : QStringLiteral("rpc_api_request_failed"),
        };
    }

    const QJsonObject resultObject = response.value(QStringLiteral("result")).toObject();
    const QJsonObject statusObject = resultObject.value(QStringLiteral("status")).toObject();
    if (resultObject.isEmpty() || statusObject.isEmpty()) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = QStringLiteral("rpc_api_status_missing"),
        };
    }

    return RpcApiEvseStatusResult{
        .success = true,
        .status = statusObject,
        .error = QString(),
    };
}

RpcApiEvseStateResult RpcApiClient::getEvseState(int evseIndex) {
    const RpcApiEvseStatusResult statusResult = getEvseStatus(evseIndex);
    if (!statusResult.success) {
        return RpcApiEvseStateResult{
            .success = false,
            .state = QString(),
            .error = statusResult.error,
        };
    }

    const QJsonValue stateValue = statusResult.status.value(QStringLiteral("state"));
    if (!stateValue.isString()) {
        return RpcApiEvseStateResult{
            .success = false,
            .state = QString(),
            .error = QStringLiteral("rpc_api_state_missing"),
        };
    }

    return RpcApiEvseStateResult{
        .success = true,
        .state = stateValue.toString(),
        .error = QString(),
    };
}

RpcApiEvseErrorPresentResult RpcApiClient::getEvseErrorPresent(int evseIndex) {
    const RpcApiEvseStatusResult statusResult = getEvseStatus(evseIndex);
    if (!statusResult.success) {
        return RpcApiEvseErrorPresentResult{
            .success = false,
            .errorPresent = false,
            .error = statusResult.error,
        };
    }

    const QJsonValue errorPresentValue = statusResult.status.value(QStringLiteral("error_present"));
    if (!errorPresentValue.isBool()) {
        return RpcApiEvseErrorPresentResult{
            .success = false,
            .errorPresent = false,
            .error = QStringLiteral("rpc_api_error_present_missing"),
        };
    }

    return RpcApiEvseErrorPresentResult{
        .success = true,
        .errorPresent = errorPresentValue.toBool(),
        .error = QString(),
    };
}

void RpcApiClient::onConnected() {
    m_connectInProgress = false;
    m_handshakeComplete = false;
    sendApiHello();
}

void RpcApiClient::onDisconnected() {
    m_handshakeComplete = false;
    m_connectInProgress = false;
    m_lastRpcResponse = QJsonObject{};
    m_hasLastRpcResponse = false;
    m_pendingRequestTimer.stop();
    m_pendingRequests.clear();
    m_reconnectTimer.start(kReconnectTimeoutMs);
}

void RpcApiClient::onBinaryMessageReceived(const QByteArray &message) {
    const QJsonDocument document = QJsonDocument::fromJson(message);
    if (!document.isObject()) {
        if (!m_pendingRequests.isEmpty()) {
            m_pendingRequestTimer.stop();
            onPendingRequestTimeout();
        }
        return;
    }

    const QJsonObject response = document.object();
    const QJsonValue idValue = response.value(QStringLiteral("id"));
    if (!idValue.isDouble()) {
        if (!m_pendingRequests.isEmpty()) {
            m_pendingRequestTimer.stop();
            onPendingRequestTimeout();
        }
        return;
    }

    const int responseId = idValue.toInt();
    if (responseId == kApiHelloRequestId) {
        onHelloResponse(response);
        return;
    }

    if (!m_pendingRequests.contains(responseId)) {
        return;
    }

    m_pendingRequestTimer.stop();

    const bool isSuccessResponse =
        response.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0") &&
        response.value(QStringLiteral("result")).isObject();
    const bool isErrorResponse =
        response.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0") &&
        response.value(QStringLiteral("error")).isObject();

    if (isSuccessResponse || isErrorResponse) {
        m_lastRpcResponse = response;
        m_hasLastRpcResponse = true;
        m_pendingRequests.remove(responseId);
        return;
    }

    onPendingRequestTimeout();
}

void RpcApiClient::ensureConnected() {
    if (m_handshakeComplete || m_connectInProgress) {
        return;
    }

    if (m_reconnectTimer.isActive()) {
        m_reconnectTimer.stop();
    }

    m_connectInProgress = true;
    m_webSocket.open(m_rpcApiUrl);
}

int RpcApiClient::nextRequestId() {
    do {
        m_requestIdCounter += 1;
    } while (m_requestIdCounter == kApiHelloRequestId);

    return m_requestIdCounter;
}

SendRpcRequestResult RpcApiClient::sendRpcRequest(QJsonObject request) {
    if (!m_handshakeComplete || m_webSocket.state() != QAbstractSocket::ConnectedState) {
        return SendRpcRequestResult{
            .success = false,
            .requestId = 0,
            .error = QStringLiteral("rpc_api_not_connected"),
        };
    }

    if (!m_pendingRequests.isEmpty()) {
        return SendRpcRequestResult{
            .success = false,
            .requestId = 0,
            .error = QStringLiteral("rpc_api_request_pending"),
        };
    }

    const int requestId = nextRequestId();
    m_lastRpcResponse = QJsonObject{};
    m_hasLastRpcResponse = false;
    request.insert(QStringLiteral("id"), requestId);

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    const qint64 queuedBytes = m_webSocket.sendTextMessage(QString::fromUtf8(payload));
    if (queuedBytes < 0) {
        return SendRpcRequestResult{
            .success = false,
            .requestId = 0,
            .error = QStringLiteral("rpc_api_send_failed"),
        };
    }

    m_pendingRequests.insert(requestId, PendingRpcRequest{
                                           .request = request,
                                           .attempts = 1,
                                       });
    m_pendingRequestTimer.start(kPendingRequestTimeoutMs);
    return SendRpcRequestResult{
        .success = true,
        .requestId = requestId,
        .error = QString(),
    };
}

bool RpcApiClient::resendPendingRequest(int requestId) {
    auto pendingRequestIt = m_pendingRequests.find(requestId);
    if (pendingRequestIt == m_pendingRequests.end()) {
        return false;
    }

    const QByteArray payload =
        QJsonDocument(pendingRequestIt->request).toJson(QJsonDocument::Compact);
    const qint64 queuedBytes = m_webSocket.sendTextMessage(QString::fromUtf8(payload));
    if (queuedBytes < 0) {
        return false;
    }

    pendingRequestIt->attempts += 1;
    m_pendingRequestTimer.start(kPendingRequestTimeoutMs);
    return true;
}

void RpcApiClient::onPendingRequestTimeout() {
    if (m_pendingRequests.isEmpty()) {
        return;
    }

    const int requestId = m_pendingRequests.begin().key();
    const PendingRpcRequest pendingRequest = m_pendingRequests.value(requestId);
    if (pendingRequest.attempts >= 2 || !resendPendingRequest(requestId)) {
        m_pendingRequests.remove(requestId);
        m_webSocket.close();
    }
}

void RpcApiClient::sendApiHello() {
    const QJsonObject helloRequest{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("API.Hello")},
        {QStringLiteral("id"), kApiHelloRequestId},
    };

    const QByteArray payload = QJsonDocument(helloRequest).toJson(QJsonDocument::Compact);
    m_webSocket.sendTextMessage(QString::fromUtf8(payload));
}

RpcApiModuleConfigResult RpcApiClient::findRpcApiModuleConfig() const {
    constexpr int kDefaultRpcApiWebsocketPort = 8080;

    const QJsonObject activeModules =
        m_everestConfigRoot.value(QStringLiteral("active_modules")).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        if (activeModule.value(QStringLiteral("module")).toString() != QStringLiteral("RpcApi")) {
            continue;
        }

        const QJsonObject configModule =
            activeModule.value(QStringLiteral("config_module")).toObject();
        const QJsonValue websocketEnabledValue =
            configModule.value(QStringLiteral("websocket_enabled"));
        if (websocketEnabledValue.isBool() && !websocketEnabledValue.toBool()) {
            return RpcApiModuleConfigResult{
                .success = false,
                .port = 0,
                .tlsEnabled = false,
                .error = QStringLiteral("rpc_api_websocket_disabled"),
            };
        }

        const QJsonValue portValue = configModule.value(QStringLiteral("websocket_port"));
        const int port = portValue.isDouble() ? portValue.toInt() : kDefaultRpcApiWebsocketPort;
        if (port <= 0) {
            return RpcApiModuleConfigResult{
                .success = false,
                .port = 0,
                .tlsEnabled = false,
                .error = QStringLiteral("rpc_api_port_invalid"),
            };
        }

        return RpcApiModuleConfigResult{
            .success = true,
            .port = port,
            .tlsEnabled = configModule.value(QStringLiteral("websocket_tls_enabled")).isBool() &&
                          configModule.value(QStringLiteral("websocket_tls_enabled")).toBool(),
            .error = QString(),
        };
    }

    return RpcApiModuleConfigResult{
        .success = false,
        .port = 0,
        .tlsEnabled = false,
        .error = QStringLiteral("rpc_api_module_missing"),
    };
}

RpcApiUrlResult RpcApiClient::buildRpcApiUrl(const RpcApiModuleConfigResult &moduleConfig) const {
    if (m_rpcApiHost.isEmpty()) {
        return RpcApiUrlResult{
            .success = false,
            .url = QUrl(),
            .error = QStringLiteral("rpc_api_host_missing"),
        };
    }

    QUrl url;
    url.setScheme(moduleConfig.tlsEnabled ? QStringLiteral("wss") : QStringLiteral("ws"));
    url.setHost(m_rpcApiHost);
    url.setPort(moduleConfig.port);

    if (!url.isValid()) {
        return RpcApiUrlResult{
            .success = false,
            .url = QUrl(),
            .error = QStringLiteral("rpc_api_url_invalid"),
        };
    }

    return RpcApiUrlResult{
        .success = true,
        .url = url,
        .error = QString(),
    };
}

void RpcApiClient::onHelloResponse(const QJsonObject &response) {
    const bool isValidHelloResponse =
        response.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0") &&
        response.value(QStringLiteral("id")).toInt() == kApiHelloRequestId &&
        response.value(QStringLiteral("result")).isObject();

    if (isValidHelloResponse) {
        m_handshakeComplete = true;
        return;
    }

    m_webSocket.close();
}
