// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "LoginState.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class LoginStateTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
    QByteArray m_previousRuntime;

private slots:
    void initTestCase() {
        m_previousRuntime = qgetenv("RUNTIME_DIRECTORY");
        qputenv("RUNTIME_DIRECTORY", m_directory.path().toUtf8());
    }
    void cleanupTestCase() {
        if (m_previousRuntime.isNull())
            qunsetenv("RUNTIME_DIRECTORY");
        else
            qputenv("RUNTIME_DIRECTORY", m_previousRuntime);
    }
    void init() {
        QFile::remove(UiRuntime::loginStatePath());
    }
    void missingAndMalformedStateFailClosed() {
        QVERIFY(!UiRuntime::readLoginState().allowed);
        QCOMPARE(UiRuntime::readLoginState().reason, QStringLiteral("login_state_unavailable"));
        for (const auto& bytes :
             {QByteArray(), QByteArray("{"), QByteArray("{}"), QByteArray("{\"version\":1,\"loginAllowed\":true}")}) {
            QFile file(UiRuntime::loginStatePath());
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
            file.close();
            QVERIFY(!UiRuntime::readLoginState().allowed);
        }
    }
    void lockPersistsUntilLauncherReinitialization() {
        QString error;
        QVERIFY(UiRuntime::initializeLoginState(error));
        QVERIFY(UiRuntime::readLoginState().allowed);
        for (const auto& operation : {QStringLiteral("factory_reset"), QStringLiteral("reboot")}) {
            QVERIFY(UiRuntime::blockLoginForReboot(operation, error));
            auto state = UiRuntime::readLoginState();
            QVERIFY(!state.allowed);
            QCOMPARE(state.reason, QStringLiteral("waiting_for_reboot"));
            QCOMPARE(state.operation, operation);
            QVERIFY(!UiRuntime::readLoginState().allowed); // Re-reading never clears the lock.
        }
        QVERIFY(UiRuntime::initializeLoginState(error));
        QVERIFY(UiRuntime::readLoginState().allowed);
        QVERIFY(UiRuntime::readLoginState().reason.isEmpty());
        QCOMPARE(QFile::permissions(UiRuntime::loginStatePath()) & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                                                    QFileDevice::ReadOther | QFileDevice::WriteOther),
                 QFileDevice::Permissions{});
    }
    void initializationPreservesOtherRuntimeFilesAndRejectsSymlinks() {
        QFile unrelated(m_directory.filePath(QStringLiteral("password-reset-state.json")));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.write("preserve");
        unrelated.close();
        QString error;
        QVERIFY(UiRuntime::initializeLoginState(error));
        QVERIFY(unrelated.exists());
        QVERIFY(QFile::remove(UiRuntime::loginStatePath()));
        QVERIFY(QFile::link(unrelated.fileName(), UiRuntime::loginStatePath()));
        QVERIFY(!UiRuntime::readLoginState().allowed);
        QVERIFY(!UiRuntime::initializeLoginState(error));
        QVERIFY(unrelated.open(QIODevice::ReadOnly));
        QCOMPARE(unrelated.readAll(), QByteArray("preserve"));
    }
    void failedPublicationLeavesAccessClosed() {
        QString error;
        QVERIFY(UiRuntime::initializeLoginState(error));
        QFile target(m_directory.filePath(QStringLiteral("unrelated")));
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write("preserve");
        target.close();
        QVERIFY(QFile::remove(UiRuntime::loginStatePath()));
        QVERIFY(QFile::link(target.fileName(), UiRuntime::loginStatePath()));
        QVERIFY(!UiRuntime::blockLoginForReboot(QStringLiteral("factory_reset"), error));
        QVERIFY(!UiRuntime::readLoginState().allowed);
        QCOMPARE(UiRuntime::readLoginState().reason, QStringLiteral("login_state_unavailable"));
        QVERIFY(target.open(QIODevice::ReadOnly));
        QCOMPARE(target.readAll(), QByteArray("preserve"));
    }
};

QTEST_GUILESS_MAIN(LoginStateTest)
#include "LoginStateTest.moc"
