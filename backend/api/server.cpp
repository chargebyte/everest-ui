// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "server.hpp"

#include "BackendConfig.hpp"
#include "modules/PCAP.hpp"
#include "RequestHandler.hpp"
#include "RpcApiClient.hpp"
#include "SystemControlHandler.hpp"

#include <QCoreApplication>
#include <QThread>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {
quint16 loadServerPort() {
    const QString configPath = resolveBackendConfigPath();
    const QString configuredPort = readBackendConfigValue(QStringLiteral("backend_port"));
    if (configuredPort.isEmpty()) {
        qCritical().noquote()
            << "Missing required configuration entry 'backend_port' in"
            << configPath
            << ". Add a line like 'backend_port=9002' and restart the backend.";
        return 0;
    }

    bool validPort = false;
    const quint16 port = configuredPort.toUShort(&validPort);
    if (!validPort || port == 0) {
        qCritical().noquote()
            << "Invalid configuration entry 'backend_port=" + configuredPort + "' in"
            << configPath
            << ". Use a TCP port like 'backend_port=9002' and restart the backend.";
        return 0;
    }

    return port;
}
}

MinimalWebSocketServer::MinimalWebSocketServer(QObject *parent)
    : QObject(parent),
      m_server(new QWebSocketServer(QStringLiteral("MinimalWebSocketServer"),
                                    QWebSocketServer::NonSecureMode, this)),
      m_handler(new RequestHandler(this)),
      m_rpcApiClient(new RpcApiClient(this)),
      m_systemControl(new SystemControl(m_rpcApiClient, this)),
      m_pcapThread(new QThread(this)),
      m_pcap(new PCAP()) {
    m_rpcApiClient->start();

    // move PCAP-object into its own thread
    // make sure it is deleted when the thread is stopped
    m_pcap->moveToThread(m_pcapThread);
    connect(m_pcapThread, &QThread::finished, m_pcap, &QObject::deleteLater);
    m_pcapThread->start();

    // connect the different via signals and slots
    // in this way it is possible to trigger an action of an object that lives in a different
    // thread than the trigger
    connect(m_handler, &RequestHandler::pcapEnqueueRequested, m_pcap,
            &PCAP::enqueueRequest, Qt::QueuedConnection);
    connect(m_handler, &RequestHandler::systemControlEnqueueRequested, m_systemControl,
            &SystemControl::enqueueRequest);
    connect(m_pcap, &PCAP::responseReady, m_handler,
            &RequestHandler::enqueueResponse, Qt::QueuedConnection);
    connect(m_systemControl, &SystemControl::responseReady, m_handler,
            &RequestHandler::enqueueResponse);
}

MinimalWebSocketServer::~MinimalWebSocketServer() {
    if (m_pcapThread) {
        m_pcapThread->quit();
        m_pcapThread->wait();
    }
}

bool MinimalWebSocketServer::start(quint16 port) {
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "Failed to start server on port" << port;
        return false;
    }

    qInfo() << "Server started on 0.0.0.0:" << port;

    // make sure whenever a new connection to the server is registered the function to handle this
    // new connection is called
    connect(m_server, &QWebSocketServer::newConnection, this,
            &MinimalWebSocketServer::handleNewConnection);

    return true;
}

void MinimalWebSocketServer::handleNewConnection() {
    QWebSocket *incoming = m_server->nextPendingConnection();
    if (!incoming) {
        return;
    }

    // reject all incoming connections if one connection is already established
    if (m_client) {
        qWarning() << "Rejecting additional client connection";
        incoming->close();
        incoming->deleteLater();
        return;
    }

    m_client = incoming;
    qInfo() << "Client connected";

    m_handler->setSocket(m_client);
    connect(m_client, &QWebSocket::textMessageReceived, m_handler,
            &RequestHandler::handleTextMessage);
    connect(m_client, &QWebSocket::disconnected, this,
            &MinimalWebSocketServer::handleDisconnected);
}

void MinimalWebSocketServer::handleDisconnected() {
    qInfo() << "Client disconnected";

    if (m_handler) {
        QObject::disconnect(m_client, nullptr, m_handler, nullptr);
        m_handler->setSocket(nullptr);
    }

    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    const quint16 serverPort = loadServerPort();
    if (serverPort == 0) {
        return 1;
    }

    MinimalWebSocketServer server;
    if (!server.start(serverPort)) {
        return 1;
    }

    return app.exec();
}
