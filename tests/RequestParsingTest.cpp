// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH

#include "RequestParsing.hpp"

#include <QtTest>

class RequestParsingTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsValidRequest() {
        const QByteArray request = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        ParsedRequest parsed;
        StaticResponse error;
        QVERIFY(isRequestValid(request, parsed, &error));
        QCOMPARE(parsed.method, QByteArray("GET"));
        QCOMPARE(parsed.path, QByteArray("/"));
        QCOMPARE(parsed.headers.value("host"), QByteArray("127.0.0.1"));
    }

    void rejectsAmbiguousHeaders_data() {
        QTest::addColumn<QByteArray>("headers");
        QTest::newRow("duplicate-host")
            << QByteArray("Host: 127.0.0.1\r\nhost: 127.0.0.1\r\n");
        QTest::newRow("duplicate-origin")
            << QByteArray("Origin: http://127.0.0.1\r\norigin: http://127.0.0.1\r\n");
        QTest::newRow("duplicate-content-type")
            << QByteArray("Content-Type: application/json\r\ncontent-type: application/json\r\n");
        QTest::newRow("duplicate-content-length")
            << QByteArray("Content-Length: 2\r\ncontent-length: 2\r\n");
        QTest::newRow("folded-header")
            << QByteArray("Host: 127.0.0.1\r\n Origin: http://127.0.0.1\r\n");
        QTest::newRow("invalid-header-name")
            << QByteArray("Host : 127.0.0.1\r\n");
        QTest::newRow("transfer-encoding")
            << QByteArray("Transfer-Encoding: chunked\r\n");
    }

    void rejectsAmbiguousHeaders() {
        QFETCH(QByteArray, headers);
        const QByteArray request = "POST / HTTP/1.1\r\n" + headers +
                                   "Content-Length: 2\r\n\r\n{}";
        ParsedRequest parsed;
        StaticResponse error;
        QVERIFY(!isRequestValid(request, parsed, &error));
        QCOMPARE(error.statusCode, 400);
    }
};

QTEST_GUILESS_MAIN(RequestParsingTest)
#include "RequestParsingTest.moc"
