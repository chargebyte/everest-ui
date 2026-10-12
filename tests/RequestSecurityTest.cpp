// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH

#include "RequestParsing.hpp"
#include "RequestSecurity.hpp"

#include <QtTest>

class RequestSecurityTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsJsonAndSameOrigin() {
        ParsedRequest request;
        request.headers = {{"host", "charger.local"},
                           {"origin", "http://CHARGER.local.:80"},
                           {"content-type", "application/json; charset=utf-8"}};
        QCOMPARE(RequestSecurity::validateAuthMutation(request), 200);
    }

    void acceptsJsonWithQuotedParameter() {
        ParsedRequest request;
        request.headers = {{"host", "charger.local"},
                           {"content-type", "application/json; charset=\"utf-8\""}};
        QCOMPARE(RequestSecurity::validateAuthMutation(request), 200);
    }

    void acceptsMissingOrigin() {
        ParsedRequest request;
        request.headers = {{"host", "127.0.0.1"}, {"content-type", "application/json"}};
        QCOMPARE(RequestSecurity::validateAuthMutation(request), 200);
    }

    void rejectsUnsupportedContentType() {
        ParsedRequest request;
        request.headers = {{"host", "127.0.0.1"}, {"content-type", "text/plain"}};
        QCOMPARE(RequestSecurity::validateAuthMutation(request), 415);
    }

    void rejectsForeignAndNullOrigin_data() {
        QTest::addColumn<QByteArray>("origin");
        QTest::newRow("foreign") << QByteArray("http://evil.example");
        QTest::newRow("null") << QByteArray("null");
        QTest::newRow("https") << QByteArray("https://charger.local");
    }

    void rejectsForeignAndNullOrigin() {
        QFETCH(QByteArray, origin);
        ParsedRequest request;
        request.headers = {{"host", "charger.local"},
                           {"origin", origin},
                           {"content-type", "application/json"}};
        QCOMPARE(RequestSecurity::validateAuthMutation(request), 403);
    }

    void comparesPortsAndValidatesAuthority_data() {
        QTest::addColumn<QByteArray>("host");
        QTest::addColumn<QByteArray>("origin");
        QTest::addColumn<int>("expectedStatus");
        QTest::newRow("different-default-port") << QByteArray("charger.local:8080")
                                                 << QByteArray("http://charger.local") << 403;
        QTest::newRow("malformed-authority") << QByteArray("charger.local/path")
                                              << QByteArray("http://charger.local/path") << 403;
        QTest::newRow("ipv6-matching-port") << QByteArray("[::1]:8080")
                                             << QByteArray("http://[::1]:8080") << 200;
        QTest::newRow("ipv6-different-port") << QByteArray("[::1]:8080")
                                              << QByteArray("http://[::1]") << 403;
    }

    void comparesPortsAndValidatesAuthority() {
        QFETCH(QByteArray, host);
        QFETCH(QByteArray, origin);
        QFETCH(int, expectedStatus);
        ParsedRequest request;
        request.headers = {{"host", host}, {"origin", origin}, {"content-type", "application/json"}};
        QCOMPARE(RequestSecurity::validateAuthMutation(request), expectedStatus);
    }

    void validatesHostAllowlist() {
        const auto allowed = RequestSecurity::allowedHosts({"customer.lan"}, "charger.example.lan");
        QVERIFY(allowed.contains("localhost"));
        QVERIFY(allowed.contains("customer.lan"));
        QVERIFY(allowed.contains("charger.example.lan"));
        QVERIFY(!RequestSecurity::validHostName("*.local"));
        QVERIFY(!RequestSecurity::validHostName("charger:80"));

        ParsedRequest request;
        request.headers.insert("host", "evil.example");
        QCOMPARE(RequestSecurity::validateHost(request, allowed), 421);
        request.headers["host"] = "CHARGER.EXAMPLE.LAN.:80";
        QCOMPARE(RequestSecurity::validateHost(request, allowed), 200);
        request.headers["host"] = "192.0.2.10";
        QCOMPARE(RequestSecurity::validateHost(request, allowed), 200);
        request.headers["host"] = "charger.example.lan:65536";
        QCOMPARE(RequestSecurity::validateHost(request, allowed), 400);
    }
};

QTEST_GUILESS_MAIN(RequestSecurityTest)
#include "RequestSecurityTest.moc"
