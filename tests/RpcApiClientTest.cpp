// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RpcApiClient.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocketServer>

#include <functional>
#include <memory>

namespace {
void sendStatus(QWebSocket* socket, int id, bool errorPresent) {
    const QJsonObject response{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"),
         QJsonObject{{QStringLiteral("status"), QJsonObject{{QStringLiteral("state"), QStringLiteral("Unplugged")},
                                                            {QStringLiteral("error_present"), errorPresent}}}}},
    };
    socket->sendBinaryMessage(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void serve(QWebSocketServer& server, std::function<void(QWebSocket*, const QJsonObject&)> statusHandler) {
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&server, statusHandler] {
        QWebSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QWebSocket::textMessageReceived, socket,
                         [socket, statusHandler](const QString& message) {
                             const QJsonObject request = QJsonDocument::fromJson(message.toUtf8()).object();
                             if (request.value(QStringLiteral("method")).toString() == QStringLiteral("API.Hello")) {
                                 const QJsonObject response{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                                            {QStringLiteral("id"), 1},
                                                            {QStringLiteral("result"), QJsonObject{}}};
                                 socket->sendBinaryMessage(QJsonDocument(response).toJson(QJsonDocument::Compact));
                             } else {
                                 statusHandler(socket, request);
                             }
                         });
    });
}
} // namespace

class RpcApiClientTest final : public QObject {
    Q_OBJECT

    void connectClient(RpcApiClient& client, const QWebSocketServer& server) {
        client.m_rpcApiConfigured = true;
        client.m_rpcApiUrl = QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()));
        client.ensureConnected();
    }

private slots:
    void destructionStopsCallbacksAndReconnects_data() {
        QTest::addColumn<QString>("state");
        QTest::newRow("connected") << QStringLiteral("connected");
        QTest::newRow("pending-request") << QStringLiteral("pending-request");
        QTest::newRow("scheduled-reconnect") << QStringLiteral("scheduled-reconnect");
    }

    void destructionStopsCallbacksAndReconnects() {
        QFETCH(QString, state);
        QWebSocketServer server(QStringLiteral("rpc-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        serve(server, [](QWebSocket*, const QJsonObject&) {});
        QSignalSpy connections(&server, &QWebSocketServer::newConnection);
        QVERIFY(connections.isValid());
        auto client = std::make_unique<RpcApiClient>();
        connectClient(*client, server);
        QTRY_VERIFY(client->isReady());
        QCOMPARE(connections.count(), 1);
        QSignalSpy completions(client.get(), &RpcApiClient::requestFinished);
        QVERIFY(completions.isValid());

        if (state == QStringLiteral("pending-request")) {
            const QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                      {QStringLiteral("method"), QStringLiteral("EVSE.GetStatus")},
                                      {QStringLiteral("params"), QJsonObject{{QStringLiteral("evse_index"), 1}}}};
            const auto result = client->sendRpcRequest(request, 100);
            QVERIFY(result.success);
            QVERIFY(client->m_pendingRequests.contains(result.requestId));
            QVERIFY(client->m_pendingRequestTimer.isActive());
        } else if (state == QStringLiteral("scheduled-reconnect")) {
            client->m_webSocket.abort();
            QTRY_VERIFY(client->m_reconnectTimer.isActive());
        }

        client.reset();
        QCOMPARE(completions.count(), 0);
        // Process events beyond the pending-request and reconnect deadlines after the sender is gone.
        QTest::qWait(650);
        QCOMPARE(completions.count(), 0);
        QCOMPARE(connections.count(), 1);
    }

    void matchesResponseIdAndRejectsNestedStatusReads() {
        QWebSocketServer server(QStringLiteral("rpc-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        serve(server, [&](QWebSocket* socket, const QJsonObject& request) {
            ++requests;
            const int id = request.value(QStringLiteral("id")).toInt();
            sendStatus(socket, id + 100, true);
            QTimer::singleShot(20, socket, [socket, id] { sendStatus(socket, id, false); });
        });
        RpcApiClient client;
        connectClient(client, server);
        QTRY_VERIFY(client.isReady());
        QString nestedError;
        QTimer::singleShot(0, &client, [&] { nestedError = client.getEvseState(2).error; });
        const auto result = client.getEvseErrorPresent(1, 500);
        QVERIFY(result.success);
        QVERIFY(!result.errorPresent);
        QCOMPARE(nestedError, QStringLiteral("rpc_api_request_pending"));
        QCOMPARE(requests, 1);
        QVERIFY(client.m_pendingRequests.isEmpty());
        QVERIFY(!client.m_pendingRequestTimer.isActive());
    }

    void requestRetryCannotExtendCallerDeadline() {
        QWebSocketServer server(QStringLiteral("rpc-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        serve(server, [&](QWebSocket*, const QJsonObject&) { ++requests; });
        RpcApiClient client;
        connectClient(client, server);
        QTRY_VERIFY(client.isReady());
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = client.getEvseErrorPresent(1, 2300);
        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_api_no_response"));
        QVERIFY(elapsed.elapsed() >= 2300);
        QVERIFY(elapsed.elapsed() < 4000);
        QCOMPARE(requests, 2);
        QVERIFY(!client.isReady());
        QVERIFY(client.m_pendingRequests.isEmpty());
        QVERIFY(!client.m_pendingRequestTimer.isActive());
    }

    void oldReplyCannotCompleteRequestAfterReconnect() {
        QWebSocketServer server(QStringLiteral("rpc-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        int oldId = 0;
        serve(server, [&](QWebSocket* socket, const QJsonObject& request) {
            ++requests;
            const int id = request.value(QStringLiteral("id")).toInt();
            if (requests == 1) {
                oldId = id;
                return;
            }
            sendStatus(socket, oldId, true);
            QTimer::singleShot(20, socket, [socket, id] { sendStatus(socket, id, false); });
        });
        RpcApiClient client;
        connectClient(client, server);
        QTRY_VERIFY(client.isReady());
        const quint64 generation = client.handshakeGeneration();
        QVERIFY(!client.getEvseErrorPresent(1, 100).success);
        QTRY_VERIFY(client.isReady());
        QVERIFY(client.handshakeGeneration() > generation);
        const auto result = client.getEvseErrorPresent(1, 500);
        QVERIFY(result.success);
        QVERIFY(!result.errorPresent);
        QCOMPARE(requests, 2);
    }

    void disconnectCompletesOutstandingRead() {
        QWebSocketServer server(QStringLiteral("rpc-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        serve(server, [](QWebSocket* socket, const QJsonObject&) { socket->abort(); });
        RpcApiClient client;
        connectClient(client, server);
        QTRY_VERIFY(client.isReady());
        const auto result = client.getEvseErrorPresent(1, 500);
        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("rpc_api_no_response"));
        QVERIFY(client.m_pendingRequests.isEmpty());
        QVERIFY(!client.m_statusReadInProgress);
    }
};

QTEST_GUILESS_MAIN(RpcApiClientTest)
#include "RpcApiClientTest.moc"
