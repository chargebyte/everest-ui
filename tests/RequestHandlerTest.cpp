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

private:
    void connectClient(QWebSocket &client) {
        QSignalSpy connected(&client, &QWebSocket::connected);
        QVERIFY(connected.isValid());
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(m_server.serverPort())));
        QVERIFY(waitForSignal(connected, 1000));
        QTest::qWait(20);
    }

    void sendRequest(QWebSocket &client, qint64 requestId) {
        const QJsonObject request{
            {QLatin1String(kKeyRequestId), requestId},
            {QStringLiteral("group"), QLatin1String(kGroupSystem)},
            {QStringLiteral("action"), QStringLiteral("read_app_title")},
            {QLatin1String(kKeyParameters), QJsonObject{}},
        };
        client.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
    }

    QWebSocketServer m_server{QStringLiteral("RequestHandlerTest"), QWebSocketServer::NonSecureMode};
    RequestHandler m_handler;
};

QTEST_MAIN(RequestHandlerTest)
#include "RequestHandlerTest.moc"
