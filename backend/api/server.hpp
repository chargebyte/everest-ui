// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef MINIMAL_WS_SERVER_HPP
#define MINIMAL_WS_SERVER_HPP

#include <QObject>

class QWebSocket;
class QWebSocketServer;
class RequestHandler;
class RpcApiClient;

class MinimalWebSocketServer final : public QObject {
    Q_OBJECT

public:
    explicit MinimalWebSocketServer(QObject *parent = nullptr);
    ~MinimalWebSocketServer() override;
    bool start(quint16 port);

private:
    void handleNewConnection();
    void handleDisconnected();

    QWebSocketServer *m_server = nullptr;
    QWebSocket *m_client = nullptr;
    RequestHandler *m_handler = nullptr;
    RpcApiClient *m_rpcApiClient = nullptr;
    class SystemControl *m_systemControl = nullptr;
    class QThread *m_pcapThread = nullptr;
    class PCAP *m_pcap = nullptr;
};

#endif // MINIMAL_WS_SERVER_HPP
