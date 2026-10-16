// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RpcApiClient.hpp"
#include "ProtocolSchema.hpp"

#include "BackendConfig.hpp"
#include "YamlUtils.hpp"

#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QTimer>

#include <algorithm>

namespace {
constexpr int kApiHelloRequestId = 1;
constexpr int kReconnectTimeoutMs = 500;
constexpr int kPendingRequestTimeoutMs = 2000;
}

RpcApiClient::RpcApiClient(QObject *parent)
    : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    m_pendingRequestTimer.setSingleShot(true);
    m_pendingRequestTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_webSocket, &QWebSocket::connected, this, &RpcApiClient::onConnected);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &RpcApiClient::onDisconnected);
    connect(&m_webSocket, &QWebSocket::binaryMessageReceived, this,
            &RpcApiClient::onBinaryMessageReceived);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RpcApiClient::ensureConnected);
    connect(&m_pendingRequestTimer, &QTimer::timeout, this,
            &RpcApiClient::onPendingRequestTimeout);
}

RpcApiClient::~RpcApiClient() {
    m_reconnectTimer.stop();
    m_pendingRequestTimer.stop();
    // Disconnect before aborting: socket teardown can emit signals while members are being destroyed.
    QObject::disconnect(&m_webSocket, nullptr, this, nullptr);
    QObject::disconnect(&m_reconnectTimer, nullptr, this, nullptr);
    QObject::disconnect(&m_pendingRequestTimer, nullptr, this, nullptr);
    m_webSocket.abort();
}

void RpcApiClient::start() {
    m_rpcApiHost = readBackendConfigValue(QStringLiteral("rpc_api_host"));
    m_everestConfigPath = readBackendConfigValue(QLatin1String(kConfEverestConfPath));
    m_rpcApiConfigured = false;
    m_configurationError.clear();
    m_handshakeComplete = false;
    m_handshakeGeneration = 0;
    m_disconnectionGeneration = 0;
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

quint64 RpcApiClient::handshakeGeneration() const {
    return m_handshakeGeneration;
}

quint64 RpcApiClient::disconnectionGeneration() const {
    return m_disconnectionGeneration;
}

QString RpcApiClient::appTitle() const {
    return m_appTitle;
}

RpcApiEvseStatusResult RpcApiClient::getEvseStatus(int evseIndex, int timeoutMs) {
    if (m_statusReadInProgress) {
        return {.success = false, .status = {}, .error = QStringLiteral("rpc_api_request_pending")};
    }
    QScopedValueRollback<bool> readingStatus(m_statusReadInProgress, true);
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

    if (timeoutMs <= 0) {
        return {.success = false, .status = {}, .error = QStringLiteral("rpc_api_no_response")};
    }
    const QDeadlineTimer deadline(std::min(timeoutMs, 2 * kPendingRequestTimeoutMs), Qt::PreciseTimer);
    const quint64 generation = m_handshakeGeneration;
    const SendRpcRequestResult sendResult = sendRpcRequest(request, static_cast<int>(deadline.remainingTime()));
    if (!sendResult.success) {
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = sendResult.error,
        };
    }

    QEventLoop waitLoop;
    connect(this, &RpcApiClient::requestFinished, &waitLoop, [&](int requestId) {
        if (requestId == sendResult.requestId) {
            waitLoop.quit();
        }
    });
    if (m_pendingRequests.contains(sendResult.requestId)) {
        waitLoop.exec();
    }

    if (!m_hasLastRpcResponse || deadline.hasExpired() || generation != m_handshakeGeneration || !isReady() ||
        m_lastRpcResponse.value(QStringLiteral("id")).toInt() != sendResult.requestId) {
        m_lastRpcResponse = {};
        m_hasLastRpcResponse = false;
        qWarning().noquote() << QStringLiteral("RPC status failed: phase=status_response; evse=%1; rpc_request_id=%2; "
                                               "error=rpc_api_no_response; generation_before=%3; generation_after=%4")
                                    .arg(evseIndex)
                                    .arg(sendResult.requestId)
                                    .arg(generation)
                                    .arg(m_handshakeGeneration);
        return RpcApiEvseStatusResult{
            .success = false,
            .status = QJsonObject{},
            .error = QStringLiteral("rpc_api_no_response"),
        };
    }

    const QJsonObject response = m_lastRpcResponse;
    m_lastRpcResponse = QJsonObject{};
    m_hasLastRpcResponse = false;

    const QJsonValue errorValue = response.value(QLatin1String(kError));
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

    const QJsonObject resultObject = response.value(QLatin1String(kTypeResult)).toObject();
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

RpcApiEvseErrorPresentResult RpcApiClient::getEvseErrorPresent(int evseIndex, int timeoutMs) {
    const RpcApiEvseStatusResult statusResult = getEvseStatus(evseIndex, timeoutMs);
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
#ifdef EVEREST_UI_RPC_DEBUG
    qInfo() << "RPC connected: phase=hello_handshake; url=" << m_rpcApiUrl;
#endif
    sendApiHello();
}

void RpcApiClient::onDisconnected() {
    ++m_disconnectionGeneration;
    m_handshakeComplete = false;
    m_connectInProgress = false;
    m_lastRpcResponse = QJsonObject{};
    m_hasLastRpcResponse = false;
    m_pendingRequestTimer.stop();
    const QList<int> pendingIds = m_pendingRequests.keys();
    m_pendingRequests.clear();
    for (const int requestId : pendingIds) {
        emit requestFinished(requestId);
    }
#ifdef EVEREST_UI_RPC_DEBUG
    qInfo() << "RPC disconnected: generation=" << m_handshakeGeneration << "disconnects=" << m_disconnectionGeneration;
#endif
    m_reconnectTimer.start(kReconnectTimeoutMs);
}

void RpcApiClient::onBinaryMessageReceived(const QByteArray &message) {
    const QJsonDocument document = QJsonDocument::fromJson(message);
    if (!document.isObject()) {
        return;
    }

    const QJsonObject response = document.object();
    const QJsonValue idValue = response.value(QStringLiteral("id"));
    if (!idValue.isDouble()) {
        if (response.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0") &&
            response.value(QStringLiteral("method")).isString()) {
            return;
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

    if (m_pendingRequests.value(responseId).generation != m_handshakeGeneration) {
        return;
    }

    m_pendingRequestTimer.stop();

    const bool isSuccessResponse =
        response.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0") &&
        response.value(QLatin1String(kTypeResult)).isObject();
    const bool isErrorResponse =
        response.value(QStringLiteral("jsonrpc")).toString() == QStringLiteral("2.0") &&
        response.value(QLatin1String(kError)).isObject();

    if (isSuccessResponse || isErrorResponse) {
        m_lastRpcResponse = response;
        m_hasLastRpcResponse = true;
        m_pendingRequests.remove(responseId);
        emit requestFinished(responseId);
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

SendRpcRequestResult RpcApiClient::sendRpcRequest(QJsonObject request, int timeoutMs) {
    if (timeoutMs <= 0) {
        return {.success = false, .requestId = 0, .error = QStringLiteral("rpc_api_no_response")};
    }
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
                                            .deadline = QDeadlineTimer(timeoutMs, Qt::PreciseTimer),
                                            .generation = m_handshakeGeneration,
                                        });
    m_pendingRequestTimer.start(std::min(kPendingRequestTimeoutMs, std::max(1, timeoutMs)));
#ifdef EVEREST_UI_RPC_DEBUG
    qInfo().noquote()
        << QStringLiteral("RPC request: phase=status_response; rpc_request_id=%1; evse=%2; "
                          "attempt=1; timeout_ms=%3; generation=%4")
               .arg(requestId)
               .arg(request.value(QStringLiteral("params")).toObject().value(QStringLiteral("evse_index")).toInt())
               .arg(timeoutMs)
               .arg(m_handshakeGeneration);
#endif
    return SendRpcRequestResult{
        .success = true,
        .requestId = requestId,
        .error = QString(),
    };
}

bool RpcApiClient::resendPendingRequest(int requestId) {
    auto pendingRequestIt = m_pendingRequests.find(requestId);
    if (pendingRequestIt == m_pendingRequests.end() || pendingRequestIt->deadline.hasExpired() || !isReady()) {
        return false;
    }

    const QByteArray payload =
        QJsonDocument(pendingRequestIt->request).toJson(QJsonDocument::Compact);
    const qint64 queuedBytes = m_webSocket.sendTextMessage(QString::fromUtf8(payload));
    if (queuedBytes < 0) {
        return false;
    }

    pendingRequestIt->attempts += 1;
    const int remainingMs = static_cast<int>(pendingRequestIt->deadline.remainingTime());
    m_pendingRequestTimer.start(std::max(1, std::min(kPendingRequestTimeoutMs, remainingMs)));
#ifdef EVEREST_UI_RPC_DEBUG
    qInfo().noquote() << QStringLiteral("RPC retry: rpc_request_id=%1; attempt=%2; remaining_ms=%3; generation=%4")
                             .arg(requestId)
                             .arg(pendingRequestIt->attempts)
                             .arg(remainingMs)
                             .arg(m_handshakeGeneration);
#endif
    return true;
}

void RpcApiClient::onPendingRequestTimeout() {
    if (m_pendingRequests.isEmpty()) {
        return;
    }

    const int requestId = m_pendingRequests.begin().key();
    const PendingRpcRequest pendingRequest = m_pendingRequests.value(requestId);
    if (pendingRequest.deadline.hasExpired() || pendingRequest.attempts >= 2 || !resendPendingRequest(requestId)) {
        qWarning().noquote() << QStringLiteral("RPC request exhausted: rpc_request_id=%1; attempt=%2; remaining_ms=%3; "
                                               "generation=%4")
                                    .arg(requestId)
                                    .arg(pendingRequest.attempts)
                                    .arg(pendingRequest.deadline.remainingTime())
                                    .arg(m_handshakeGeneration);
        m_pendingRequests.remove(requestId);
        m_webSocket.abort();
        emit requestFinished(requestId);
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
        m_everestConfigRoot.value(QLatin1String(kEverestConfActiveModules)).toObject();
    const auto activeModuleKeys = activeModules.keys();
    for (const QString &activeModuleKey : activeModuleKeys) {
        const QJsonObject activeModule = activeModules.value(activeModuleKey).toObject();
        if (activeModule.value(QLatin1String(kEverestConfModule)).toString() != QStringLiteral("RpcApi")) {
            continue;
        }

        const QJsonObject configModule =
            activeModule.value(QLatin1String(kEverestConfConfigModule)).toObject();
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
        response.value(QLatin1String(kTypeResult)).isObject();

    if (isValidHelloResponse) {
        const QJsonObject resultObject = response.value(QLatin1String(kTypeResult)).toObject();
        const QJsonObject chargerInfoObject =
            resultObject.value(QStringLiteral("charger_info")).toObject();
        const QString model = chargerInfoObject.value(QStringLiteral("model")).toString().trimmed();
        if (!model.isEmpty() &&
            model.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) != 0) {
            m_appTitle = model;
        } else {
            m_appTitle.clear();
        }
        m_handshakeComplete = true;
        ++m_handshakeGeneration;
#ifdef EVEREST_UI_RPC_DEBUG
        qInfo() << "RPC Hello completed: phase=hello_handshake; generation=" << m_handshakeGeneration
                << "disconnects=" << m_disconnectionGeneration;
#endif
        return;
    }

    m_webSocket.close();
}
