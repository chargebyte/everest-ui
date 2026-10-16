// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH

#include "LayeredConfig.hpp"
#include "ServerConfig.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

namespace {
bool writeFile(const QString& path, const QByteArray& data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

UiConfig::Paths paths(const QTemporaryDir& dir) {
    return {dir.filePath(QStringLiteral("vendor/app.conf")), dir.filePath(QStringLiteral("etc/app.conf"))};
}

QByteArray frontendDefaults() {
    return "port=80\nroot=public\nbind=127.0.0.1\nws_path=/ws\nbackend_ws=ws://127.0.0.1:9002\n"
           "max_request_bytes=8192\nlog_level=info\nauth_file=auth.json\napp_title=Vendor\n"
           "allow_origin=https://example.org\n";
}
} // namespace

class LayeredConfigTest final : public QObject {
    Q_OBJECT
private slots:
    void mergesArbitraryKeysWithPresenceMasksAndSources() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.defaults, "# vendor\nport=9002\nempty=vendor\nremoved=value\n"
                                                "platform.one=a\nplatform.two=b\nfuture.key=old\n"));
        QVERIFY(writeFile(configPaths.overlay, "# local\nport=9003\nport=9004\nempty=\n!removed\n"
                                               "platform.one=\n!platform.two\nplatform.three=c\n"
                                               "literal.key=x=y#literal\n!future.key\nfuture.key=last=value\n"));
        const auto config = UiConfig::load(configPaths);
        QVERIFY2(config.valid(), qPrintable(config.error));
        QCOMPARE(config.value(QStringLiteral("port")), QStringLiteral("9004"));
        QVERIFY(config.contains(QStringLiteral("empty")));
        QVERIFY(config.value(QStringLiteral("empty")).isEmpty());
        QVERIFY(!config.contains(QStringLiteral("removed")));
        QCOMPARE(config.value(QStringLiteral("future.key")), QStringLiteral("last=value"));
        QCOMPARE(config.value(QStringLiteral("literal.key")), QStringLiteral("x=y#literal"));
        const QMap<QString, QString> expected{{QStringLiteral("one"), QString()},
                                              {QStringLiteral("three"), QStringLiteral("c")}};
        QCOMPARE(config.values(QStringLiteral("platform.")), expected);
        QCOMPARE(config.entries.value(QStringLiteral("port")).path, configPaths.overlay);
        QCOMPARE(config.entries.value(QStringLiteral("port")).line, 3);
        QVERIFY(config.source(QStringLiteral("port")).endsWith(QStringLiteral(":3")));
    }

    void supportsMissingLayersAndCommentOnlyOverlays() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        auto config = UiConfig::load(configPaths);
        QVERIFY(config.valid());
        QVERIFY(config.entries.isEmpty());
        QVERIFY(writeFile(configPaths.overlay, "port=9002\nlegacy=value\n"));
        config = UiConfig::load(configPaths);
        QVERIFY(config.valid());
        QCOMPARE(config.value(QStringLiteral("legacy")), QStringLiteral("value"));
        QVERIFY(writeFile(configPaths.defaults, "port=9003\nnew.key=value\n"));
        QVERIFY(writeFile(configPaths.overlay, "# no overrides\n\n"));
        config = UiConfig::load(configPaths);
        QCOMPARE(config.value(QStringLiteral("port")), QStringLiteral("9003"));
        QCOMPARE(config.entries.value(QStringLiteral("port")).path, configPaths.defaults);
        QVERIFY(QFile::remove(configPaths.overlay));
        QCOMPARE(UiConfig::load(configPaths).values(), config.values());
    }

    void acceptsUtf8BomAndCrLf() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.defaults, QByteArray::fromHex("efbbbf") + "key=value\r\n"));
        const auto config = UiConfig::load(configPaths);
        QVERIFY(config.valid());
        QCOMPARE(config.value(QStringLiteral("key")), QStringLiteral("value"));
    }

    void reportsErrorsWithoutExposingPartialResults() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.defaults, "key=vendor\n"));
        QVERIFY(writeFile(configPaths.overlay, "key=local\ninvalid line\n"));
        auto config = UiConfig::load(configPaths);
        QVERIFY(!config.valid());
        QVERIFY(config.error.contains(configPaths.overlay + QStringLiteral(":2")));
        QVERIFY(config.entries.isEmpty());
        QVERIFY(QFile::remove(configPaths.overlay));
        QVERIFY(QDir().mkdir(configPaths.overlay));
        config = UiConfig::load(configPaths);
        QVERIFY(!config.valid());
        QVERIFY(config.error.contains(configPaths.overlay));
    }

    void retainsLastGoodRuntimeConfigurationAndRecovers() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.defaults, "available_features=none\nkey=vendor\n"));
        UiConfig::Store store(configPaths);
        QVERIFY(store.read().valid());
        QVERIFY(writeFile(configPaths.overlay, "key=partial\nbad\n"));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral(".*retaining last valid configuration.*")));
        auto config = store.read();
        QCOMPARE(config.value(QStringLiteral("key")), QStringLiteral("vendor"));
        QCOMPARE(config.value(QStringLiteral("available_features")), QStringLiteral("none"));
        QCOMPARE(store.read().value(QStringLiteral("key")), QStringLiteral("vendor"));
        QVERIFY(writeFile(configPaths.overlay, "key=recovered\n"));
        QCOMPARE(store.read().value(QStringLiteral("key")), QStringLiteral("recovered"));
        QVERIFY(QFile::remove(configPaths.overlay));
        QCOMPARE(store.read().value(QStringLiteral("key")), QStringLiteral("vendor"));
        UiConfig::Store fresh(configPaths);
        QVERIFY(writeFile(configPaths.overlay, "bad\n"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*no valid configuration.*")));
        QVERIFY(!fresh.read().valid());
    }

    void writesOnlyLocalDirectivesAndPreservesUnrelatedContent() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        const QByteArray vendor = "key=vendor\nother=unchanged\n";
        const QByteArray local = "# retain this comment\nunknown.future=keep=me\nkey=first\n!key\nkey=last\n";
        QVERIFY(writeFile(configPaths.defaults, vendor));
        QVERIFY(QFile::setPermissions(configPaths.defaults, QFile::ReadOwner));
        QVERIFY(writeFile(configPaths.overlay, local));
        QString error;
        QVERIFY2(UiConfig::writeOverlay(configPaths, QStringLiteral("key"), UiConfig::Update::Set, QString(), error),
                 qPrintable(error));
        QVERIFY(UiConfig::load(configPaths).contains(QStringLiteral("key")));
        QVERIFY(UiConfig::load(configPaths).value(QStringLiteral("key")).isEmpty());
        const auto saved = readFile(configPaths.overlay);
        QVERIFY(saved.contains("# retain this comment\nunknown.future=keep=me\n"));
        QCOMPARE(saved.count("key="), 1);
        QVERIFY(UiConfig::writeOverlay(configPaths, QStringLiteral("key"), UiConfig::Update::Mask, {}, error));
        QVERIFY(!UiConfig::load(configPaths).contains(QStringLiteral("key")));
        QVERIFY(UiConfig::writeOverlay(configPaths, QStringLiteral("key"), UiConfig::Update::Inherit, {}, error));
        QCOMPARE(UiConfig::load(configPaths).value(QStringLiteral("key")), QStringLiteral("vendor"));
        QCOMPARE(readFile(configPaths.defaults), vendor);
        QVERIFY(readFile(configPaths.overlay).contains("unknown.future=keep=me"));
    }

    void createsMissingOverlayAndRejectsUnsafeWrites() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.defaults, "key=vendor\n"));
        QString error;
        QVERIFY(UiConfig::writeOverlay(configPaths, QStringLiteral("future.key"), UiConfig::Update::Set,
                                       QStringLiteral("a=b"), error));
        QCOMPARE(UiConfig::load(configPaths).value(QStringLiteral("future.key")), QStringLiteral("a=b"));
        const auto saved = readFile(configPaths.overlay);
        QVERIFY(!UiConfig::writeOverlay(configPaths, QStringLiteral("bad key"), UiConfig::Update::Set, {}, error));
        QVERIFY(!UiConfig::writeOverlay(configPaths, QStringLiteral("key"), UiConfig::Update::Set,
                                        QStringLiteral("bad\nline"), error));
        QVERIFY(!UiConfig::writeOverlay({configPaths.defaults, configPaths.defaults}, QStringLiteral("key"),
                                        UiConfig::Update::Set, {}, error));
        QCOMPARE(readFile(configPaths.overlay), saved);
        QVERIFY(writeFile(configPaths.overlay, "bad line\n"));
        QVERIFY(!UiConfig::writeOverlay(configPaths, QStringLiteral("key"), UiConfig::Update::Set, {}, error));
        QCOMPARE(readFile(configPaths.overlay), QByteArray("bad line\n"));
#ifdef Q_OS_UNIX
        QVERIFY(QFile::remove(configPaths.overlay));
        QVERIFY(QFile::link(configPaths.defaults, configPaths.overlay));
        QVERIFY(!UiConfig::writeOverlay(configPaths, QStringLiteral("key"), UiConfig::Update::Set, {}, error));
        const QString linkedParent = dir.filePath(QStringLiteral("linked"));
        QVERIFY(QFile::link(QFileInfo(configPaths.defaults).absolutePath(), linkedParent));
        QVERIFY(!UiConfig::writeOverlay({configPaths.defaults, linkedParent + QStringLiteral("/new.conf")},
                                        QStringLiteral("key"), UiConfig::Update::Set, {}, error));
#endif
        QCOMPARE(readFile(configPaths.defaults), QByteArray("key=vendor\n"));
    }

    void frontendMergesBeforeValidationAndResolvesPathsPerSource() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.defaults, frontendDefaults()));
        QVERIFY(writeFile(configPaths.overlay, "port=8080\nauth_file=local/auth.json\napp_title=\nallow_origin=\n"));
        ServerConfig config;
        QString error;
        QVERIFY2(loadAndValidateServerConfig(configPaths, config, error), qPrintable(error));
        QCOMPARE(config.port, quint16(8080));
        QCOMPARE(config.root, dir.filePath(QStringLiteral("vendor/public")));
        QCOMPARE(config.authFile, dir.filePath(QStringLiteral("etc/local/auth.json")));
        QVERIFY(config.appTitle.isEmpty());
        QVERIFY(!config.enforceOrigin);
        QVERIFY(writeFile(configPaths.overlay, "!port\n"));
        QVERIFY(!loadAndValidateServerConfig(configPaths, config, error));
        QVERIFY(error.contains(QStringLiteral("port")));
        QVERIFY(writeFile(configPaths.overlay, "port=0\n"));
        QVERIFY(!loadAndValidateServerConfig(configPaths, config, error));
        QVERIFY(writeFile(configPaths.overlay, "new.unsupported=value\n"));
        QVERIFY(!loadAndValidateServerConfig(configPaths, config, error));
        QVERIFY(error.contains(configPaths.overlay + QStringLiteral(":1")));
        QVERIFY(writeFile(configPaths.defaults, frontendDefaults() + "new.unsupported=value\n"));
        QVERIFY(writeFile(configPaths.overlay, "!new.unsupported\n"));
        QVERIFY2(loadAndValidateServerConfig(configPaths, config, error), qPrintable(error));
    }

    void frontendSupportsLegacyCompleteLocalFiles() {
        QTemporaryDir dir;
        const auto configPaths = paths(dir);
        QVERIFY(writeFile(configPaths.overlay, frontendDefaults()));
        ServerConfig config;
        QString error;
        QVERIFY2(loadAndValidateServerConfig(configPaths, config, error), qPrintable(error));
        QCOMPARE(config.port, quint16(80));
        QVERIFY(config.enforceOrigin);
        QCOMPARE(config.root, dir.filePath(QStringLiteral("etc/public")));
    }
};

QTEST_GUILESS_MAIN(LayeredConfigTest)
#include "LayeredConfigTest.moc"
