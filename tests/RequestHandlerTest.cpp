// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "RequestHandler.hpp"

#include "ProtocolSchema.hpp"
#include "ResponseBuilder.hpp"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QHostAddress>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {
bool waitForSignal(QSignalSpy &spy, int timeoutMs) {
    return !spy.isEmpty() || spy.wait(timeoutMs);
}
} // namespace

class RequestHandlerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<ModuleRequest>("ModuleRequest");
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QWebSocketServer::newConnection, this, [this]() {
            QWebSocket *socket = m_server.nextPendingConnection();
            m_handler.setSocket(socket);
            connect(socket, &QWebSocket::textMessageReceived,
                    &m_handler, &RequestHandler::handleTextMessage);
        });
    }

    void cleanup() {
        m_handler.setSocket(nullptr);
    }

    void lateResponseDoesNotReachNextSession() {
        QWebSocket clientA;
        QSignalSpy requests(&m_handler, &RequestHandler::systemControlEnqueueRequested);
        QVERIFY(requests.isValid());

        connectClient(clientA);
        sendRequest(clientA, 1);
        QVERIFY(waitForSignal(requests, 1000));
        const ModuleRequest requestA = qvariant_cast<ModuleRequest>(requests.takeFirst().constFirst());

        QSignalSpy disconnectedA(&clientA, &QWebSocket::disconnected);
        QVERIFY(disconnectedA.isValid());
        clientA.close();
        QVERIFY(waitForSignal(disconnectedA, 1000));
        QTest::qWait(50);

        QWebSocket clientB;
        QSignalSpy messagesB(&clientB, &QWebSocket::textMessageReceived);
        QVERIFY(messagesB.isValid());
        connectClient(clientB);

        m_handler.enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = requestA.requestId,
            .group = QLatin1String(kGroupSystem),
            .action = QStringLiteral("read_app_title"),
            .parameters = QJsonObject{{QStringLiteral("appTitle"), QStringLiteral("old client")}},
            .success = true,
            .final = true,
        }));
        QTest::qWait(100);
        QCOMPARE(messagesB.count(), 0);

        sendRequest(clientB, 1);
        QVERIFY(waitForSignal(requests, 1000));
        const ModuleRequest requestB = qvariant_cast<ModuleRequest>(requests.takeFirst().constFirst());
        m_handler.enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = requestB.requestId,
            .group = QLatin1String(kGroupSystem),
            .action = QStringLiteral("read_app_title"),
            .parameters = QJsonObject{{QStringLiteral("appTitle"), QStringLiteral("new client")}},
            .success = true,
            .final = true,
        }));

        QVERIFY(waitForSignal(messagesB, 1000));
        const QJsonObject response = QJsonDocument::fromJson(messagesB.takeFirst().constFirst().toString().toUtf8()).object();
        QCOMPARE(static_cast<qint64>(response.value(QLatin1String(kKeyRequestId)).toDouble()), qint64(1));
        QCOMPARE(response.value(QLatin1String(kKeyParameters)).toObject().value(QStringLiteral("appTitle")).toString(),
                 QStringLiteral("new client"));
    }

    void pcapReadOnlyRemovesRelatedWriteContext() {
        QWebSocket client;
        QSignalSpy pcapRequests(&m_handler, &RequestHandler::pcapEnqueueRequested);
        QSignalSpy chunkRequests(&m_handler, &RequestHandler::pcapChunkRequested);
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        QVERIFY(pcapRequests.isValid());
        QVERIFY(chunkRequests.isValid());
        QVERIFY(messages.isValid());
        connectClient(client);

        sendRequest(client, 10, QLatin1String(kGroupPcap), QLatin1String(kActionWrite));
        QVERIFY(waitForSignal(pcapRequests, 1000));
        const ModuleRequest writeRequest = qvariant_cast<ModuleRequest>(pcapRequests.takeFirst().constFirst());
        m_handler.enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = writeRequest.requestId,
            .group = QLatin1String(kGroupPcap),
            .action = QLatin1String(kActionWrite),
            .parameters = {},
            .success = true,
            .final = true,
        }));
        QVERIFY(waitForSignal(messages, 1000));
        acknowledge(client, messages);

        sendRequest(client, 11, QLatin1String(kGroupPcap), QLatin1String(kActionRead));
        QVERIFY(waitForSignal(pcapRequests, 1000));
        const ModuleRequest readRequest = qvariant_cast<ModuleRequest>(pcapRequests.takeFirst().constFirst());
        m_handler.enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = readRequest.requestId,
            .group = QLatin1String(kGroupPcap),
            .action = QLatin1String(kActionRead),
            .parameters = QJsonObject{
                {QLatin1String(kKeyTransfer), QLatin1String(kTransferBinary)},
                {QLatin1String(kKeyCaptureRequestId), writeRequest.requestId},
            },
            .success = true,
            .final = false,
        }));
        QVERIFY(waitForSignal(messages, 1000));
        acknowledge(client, messages);
        QVERIFY(waitForSignal(chunkRequests, 1000));
        QCOMPARE(qvariant_cast<qint64>(chunkRequests.takeFirst().constFirst()), readRequest.requestId);

        sendRequest(client, 12, QLatin1String(kGroupPcap), QLatin1String(kActionWrite));
        QVERIFY(waitForSignal(pcapRequests, 1000));
        const ModuleRequest newerWriteRequest = qvariant_cast<ModuleRequest>(pcapRequests.takeFirst().constFirst());
        m_handler.enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = newerWriteRequest.requestId,
            .group = QLatin1String(kGroupPcap),
            .action = QLatin1String(kActionWrite),
            .parameters = {},
            .success = true,
            .final = true,
        }));
        QVERIFY(waitForSignal(messages, 1000));
        acknowledge(client, messages);

        m_handler.enqueuePcapChunk(readRequest.requestId, 0, true, QByteArray("pcap"));
        m_handler.enqueueResponse(ResponseBuilder::buildResponse(ModuleResponse{
            .requestId = newerWriteRequest.requestId,
            .group = QLatin1String(kGroupPcap),
            .action = QLatin1String(kActionWrite),
            .parameters = QJsonObject{{QLatin1String(kError), QStringLiteral("done")}},
            .success = false,
            .final = true,
        }));
        QVERIFY(waitForSignal(messages, 1000));
        const QJsonObject response = parseMessage(messages.takeLast());
        QCOMPARE(response.value(QLatin1String(kKeyRequestId)).toDouble(), 12.0);
    }

private:
    void connectClient(QWebSocket &client) {
        QSignalSpy connected(&client, &QWebSocket::connected);
        QVERIFY(connected.isValid());
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(m_server.serverPort())));
        QVERIFY(waitForSignal(connected, 1000));
        QTest::qWait(20);
    }

    void sendRequest(QWebSocket &client, qint64 requestId,
                     const QString &group = QStringLiteral("system"),
                     const QString &action = QStringLiteral("read_app_title")) {
        const QJsonObject request{
            {QLatin1String(kKeyRequestId), requestId},
            {QStringLiteral("group"), group},
            {QStringLiteral("action"), action},
            {QLatin1String(kKeyParameters), QJsonObject{}},
        };
        client.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
    }

    void acknowledge(QWebSocket &client, QSignalSpy &messages) {
        const QJsonObject response = parseMessage(messages.takeLast());
        const QJsonObject ack{
            {QLatin1String(kKeyType), QLatin1String(kTypeAck)},
            {QLatin1String(kKeyResponseId), response.value(QLatin1String(kKeyResponseId))},
        };
        client.sendTextMessage(QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));
    }

    static QJsonObject parseMessage(const QVariant &message) {
        return QJsonDocument::fromJson(message.toString().toUtf8()).object();
    }

    QWebSocketServer m_server{QStringLiteral("RequestHandlerTest"), QWebSocketServer::NonSecureMode};
    RequestHandler m_handler;
};

QTEST_MAIN(RequestHandlerTest)
#include "RequestHandlerTest.moc"
