// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef RPC_API_CLIENT_HPP
#define RPC_API_CLIENT_HPP

#include <QJsonObject>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

struct RpcApiModuleConfigResult {
    bool success = false;
    int port = 0;
    bool tlsEnabled = false;
    QString error;
};

struct RpcApiUrlResult {
    bool success = false;
    QUrl url;
    QString error;
};

struct PendingRpcRequest {
    QJsonObject request;
    int attempts = 1;
};

struct SendRpcRequestResult {
    bool success = false;
    int requestId = 0;
    QString error;
};

struct RpcApiEvseStateResult {
    bool success = false;
    QString state;
    QString error;
};

class RpcApiClient : public QObject {
    Q_OBJECT

public:
    explicit RpcApiClient(QObject *parent = nullptr);
    void start();
    bool isReady() const;
    RpcApiEvseStateResult getEvseState(int evseIndex);

private slots:
    void onConnected();
    void onDisconnected();
    void onBinaryMessageReceived(const QByteArray &message);
    void ensureConnected();
    void onPendingRequestTimeout();

private:
    int nextRequestId();
    SendRpcRequestResult sendRpcRequest(QJsonObject request);
    bool resendPendingRequest(int requestId);
    void onHelloResponse(const QJsonObject &response);
    void sendApiHello();
    RpcApiModuleConfigResult findRpcApiModuleConfig() const;
    RpcApiUrlResult buildRpcApiUrl(const RpcApiModuleConfigResult &moduleConfig) const;

    QWebSocket m_webSocket;
    QTimer m_reconnectTimer;
    QTimer m_pendingRequestTimer;
    QString m_rpcApiHost;
    QString m_everestConfigPath;
    QJsonObject m_everestConfigRoot;
    QString m_configurationError;
    QUrl m_rpcApiUrl;
    QJsonObject m_lastRpcResponse;
    QHash<int, PendingRpcRequest> m_pendingRequests;
    int m_requestIdCounter = 1;
    bool m_hasLastRpcResponse = false;
    bool m_rpcApiConfigured = false;
    bool m_handshakeComplete = false;
    bool m_connectInProgress = false;
};

#endif // RPC_API_CLIENT_HPP
