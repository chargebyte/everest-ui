// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "PasswordReset.hpp"
#include "AuthManager.hpp"
#include "ServerConfig.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

class PasswordResetTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QByteArray oldRuntime;
    qint64 now = 100000;
    QString marker() const { return dir.path() + "/nvmem"; }
    QString authPath() const { return dir.path() + "/auth.json"; }
    QString statePath() const { return dir.path() + "/password-reset-state.json"; }
    void write(const QString &path, const QByteArray &bytes) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), qint64(bytes.size()));
    }
    QByteArray read(const QString &path) {
        QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
    }
    void markerValue(bool warm = false) {
        QByteArray bytes(32, char(0x55));
        bytes.replace(28, 4, warm ? QByteArray::fromHex("4d524157") : QByteArray(4, '\0'));
        write(marker(), bytes);
    }
    void user(AuthManager &auth) {
        QString error; QVERIFY(auth.initialize(error)); QVERIFY(auth.createUser("alice", "password1", error));
    }
    QJsonObject state() { return QJsonDocument::fromJson(read(statePath())).object(); }
    void state(QJsonObject o) { write(statePath(), QJsonDocument(o).toJson()); }
    int lock() {
        int fd = ::open(QFile::encodeName(dir.path() + "/password-reset.lock").constData(), O_CREAT | O_RDWR, 0600);
        if (fd < 0 || ::flock(fd, LOCK_EX | LOCK_NB) != 0) return -1;
        return fd;
    }
private slots:
    void init() {
        oldRuntime = qgetenv("RUNTIME_DIRECTORY");
        qputenv("RUNTIME_DIRECTORY", dir.path().toUtf8());
        for (const auto &name : QDir(dir.path()).entryList(QDir::Files)) QFile::remove(dir.path() + '/' + name);
        now = 100000;
        markerValue();
    }
    void cleanup() { if (oldRuntime.isNull()) qunsetenv("RUNTIME_DIRECTORY"); else qputenv("RUNTIME_DIRECTORY", oldRuntime); }
    void coldResetAndUnlimitedSetup() {
        AuthManager auth(authPath()); user(auth);
        const auto session = auth.createSession("alice");
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        QVERIFY(reset.status(true).value("available").toBool());
        QCOMPARE(reset.status(true).value("remainingSeconds").toInt(), 60);
        QCOMPARE(reset.status(true).value("windowSeconds").toInt(), 60);
        QCOMPARE(QFileInfo(statePath()).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
        QString error; QCOMPARE(reset.reset(auth, error), 200);
        QVERIFY(auth.setupRequired()); QVERIFY(!auth.validateSession(session));
        now += 600000;
        AuthManager reloaded(authPath()); QVERIFY(reloaded.initialize(error));
        QVERIFY(reloaded.setupRequired()); QVERIFY(reloaded.createUser("newuser", "password2", error));
        QVERIFY(reloaded.authenticate("newuser", "password2")); QVERIFY(!reloaded.authenticate("alice", "password1"));
        PasswordReset restart(marker(), 60, [&]{return now;}); restart.initialize();
        QCOMPARE(restart.status(true).value("reason").toString(), QString("unavailable_this_boot"));
        QCOMPARE(restart.reset(reloaded, error), 403);
    }
    void warmSurvivesClearingAndRestart() {
        markerValue(true); const auto before = read(marker());
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        QCOMPARE(reset.status(true).value("reason").toString(), QString("warm_boot"));
        const auto after = read(marker()); QCOMPARE(after.size(), 32); QCOMPARE(after.left(28), before.left(28));
        QCOMPARE(after.mid(28), QByteArray(4, '\0'));
        now += 100000;
        PasswordReset restart(marker(), 60, [&]{return now;}); restart.initialize();
        QCOMPARE(restart.status(true).value("reason").toString(), QString("warm_boot"));
    }
    void deadlineDoesNotRestart() {
        AuthManager auth(authPath()); user(auth);
        PasswordReset first(marker(), 60, [&]{return now;}); first.initialize();
        now += 59000;
        PasswordReset second(marker(), 600, [&]{return now;}); second.initialize();
        QCOMPARE(second.status(true).value("remainingSeconds").toInt(), 1);
        now += 1000;
        QCOMPARE(second.status(true).value("reason").toString(), QString("expired"));
        QString e; QCOMPARE(second.reset(auth, e), 403); QVERIFY(auth.hasUser());
    }
    void runtimeDirectoryRequired_data() {
        QTest::addColumn<QByteArray>("runtime");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("relative") << QByteArray("relative");
        QTest::newRow("multiple") << QByteArray("/tmp:/run");
        QTest::newRow("missing") << QByteArray("/nonexistent/reset-runtime");
    }
    void runtimeDirectoryRequired() {
        QFETCH(QByteArray, runtime); qputenv("RUNTIME_DIRECTORY", runtime);
        markerValue(true); const auto original = read(marker());
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        QCOMPARE(reset.status(true).value("reason").toString(), QString("disabled"));
        QCOMPARE(read(marker()), original); QVERIFY(!QFile::exists(statePath()));
    }
    void invalidBootData_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("short") << QByteArray(30, '\0');
        QTest::newRow("unknown") << QByteArray(32, char(0x42));
    }
    void invalidBootData() {
        QFETCH(QByteArray, bytes); write(marker(), bytes);
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        QCOMPARE(reset.status(true).value("reason").toString(), QString("boot_info_invalid"));
        QCOMPARE(read(marker()), bytes);
    }
    void corruptStateDoesNotReclassify() {
        write(statePath(), "{broken"); markerValue(true); const auto before = read(marker());
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        QVERIFY(!reset.status(true).value("available").toBool()); QCOMPARE(read(marker()), before);
    }
    void stateBecomingInvalidIsUnavailableThisBoot() {
        PasswordReset reset(marker(), 60, [&]{return now;});
        reset.initialize();
        QVERIFY(reset.status(true).value("available").toBool());

        write(statePath(), "{broken");

        const auto status = reset.status(true);
        QVERIFY(!status.value("available").toBool());
        QCOMPARE(status.value("reason").toString(), QString("unavailable_this_boot"));
    }
    void interruptedClearRetriesButNeverEnablesReset() {
        PasswordReset first(marker(), 60, [&]{return now;}); first.initialize();
        auto o = state(); o["markerClearPending"] = true; state(o);
        markerValue(true);
        PasswordReset retry(marker(), 60, [&]{return now;}); retry.initialize();
        QCOMPARE(read(marker()).mid(28), QByteArray(4, '\0'));
        QCOMPARE(state().value("classification").toString(), QString("cold"));
        QVERIFY(!state().value("markerClearPending").toBool());
        QVERIFY(!state().value("resetAllowed").toBool());
        QCOMPARE(retry.status(true).value("reason").toString(), QString("unavailable_this_boot"));
    }
    void failedWriteRetriesOnRestart() {
        PasswordReset first(marker(), 60, [&]{return now;}); first.initialize();
        auto pending = state(); pending["markerClearPending"] = true; state(pending);
        QFile::remove(marker()); QVERIFY(QFile::link("/dev/full", marker()));
        PasswordReset failed(marker(), 60, [&]{return now;}); failed.initialize();
        QVERIFY(state().value("markerClearPending").toBool());
        QVERIFY(!state().value("resetAllowed").toBool());
        QFile::remove(marker()); markerValue(true);
        PasswordReset retry(marker(), 60, [&]{return now;}); retry.initialize();
        QVERIFY(!state().value("markerClearPending").toBool());
        QVERIFY(!state().value("resetAllowed").toBool());
        QCOMPARE(read(marker()).mid(28), QByteArray(4, '\0'));
    }
    void initializationLockFailureIsProcessScoped() {
        int fd = lock(); QVERIFY(fd >= 0);
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        ::close(fd);
        QVERIFY(!QFile::exists(statePath())); QVERIFY(!reset.status(true).value("available").toBool());
        PasswordReset next(marker(), 60, [&]{return now;}); next.initialize();
        QVERIFY(next.status(true).value("available").toBool());
    }
    void requestLockConflictIsRetryable() {
        AuthManager auth(authPath()); user(auth);
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        int fd = lock(); QVERIFY(fd >= 0); const auto saved = read(statePath());
        QString e; QCOMPARE(reset.reset(auth, e), 503); QCOMPARE(e, QString("reset_temporarily_unavailable"));
        QCOMPARE(read(statePath()), saved); QVERIFY(auth.hasUser()); ::close(fd);
        QVERIFY(reset.status(true).value("available").toBool()); QCOMPARE(reset.reset(auth, e), 200);
    }
    void crashReleasesLockWithoutRemovingFile() {
        int pipefd[2]; QCOMPARE(::pipe(pipefd), 0);
        const auto pid = ::fork(); QVERIFY(pid >= 0);
        if (pid == 0) {
            ::close(pipefd[0]); const int fd = lock(); char ready = fd >= 0 ? '1' : '0';
            ::write(pipefd[1], &ready, 1); ::pause(); ::_exit(1);
        }
        ::close(pipefd[1]); char ready = 0; QCOMPARE(::read(pipefd[0], &ready, 1), ssize_t(1)); ::close(pipefd[0]);
        ::kill(pid, SIGKILL); int result; ::waitpid(pid, &result, 0); QCOMPARE(ready, '1');
        QVERIFY(QFile::exists(dir.path() + "/password-reset.lock"));
        const int fd = lock(); QVERIFY(fd >= 0); ::close(fd);
    }
    void credentialsSurviveStoreFailure() {
        AuthManager auth(authPath()); user(auth); auto session = auth.createSession("alice");
        QFile::remove(authPath()); QVERIFY(QDir().mkdir(authPath()));
        QString e; QVERIFY(!auth.resetUser(e)); QVERIFY(auth.authenticate("alice", "password1"));
        QVERIFY(auth.validateSession(session)); QDir().rmdir(authPath());
    }
    void statusPriorityAndNewBoot() {
        markerValue(true);
        PasswordReset warm(marker(), 60, [&]{return now;}); warm.initialize();
        now += 90000;
        QCOMPARE(warm.status(true).value("reason").toString(), QString("warm_boot"));
        QCOMPARE(warm.status(false).value("reason").toString(), QString("setup_required"));
        PasswordReset disabled(marker(), 0, [&]{return now;}); disabled.initialize();
        QCOMPARE(disabled.status(true).value("reason").toString(), QString("disabled"));
        QCOMPARE(disabled.status(true).value("windowSeconds").toInt(), 0);
        PasswordReset configured(marker(), 180, [&]{return now;}); configured.initialize();
        QCOMPARE(configured.status(true).value("windowSeconds").toInt(), 180);
        auto previousBoot = state(); previousBoot["bootId"] = "00000000-0000-0000-0000-000000000000";
        previousBoot["markerClearPending"] = true; state(previousBoot);
        markerValue();
        PasswordReset cold(marker(), 60, [&]{return now;}); cold.initialize();
        QVERIFY(cold.status(true).value("available").toBool());
        QCOMPARE(cold.status(true).value("remainingSeconds").toInt(), 60);
    }
    void stateSaveFailureNeverDeletesCredentials() {
        AuthManager auth(authPath()); user(auth);
        PasswordReset reset(marker(), 60, [&]{return now;}); reset.initialize();
        QFile::remove(statePath()); QVERIFY(QDir().mkdir(statePath()));
        QString error; QCOMPARE(reset.reset(auth, error), 403); QVERIFY(auth.hasUser());
        QVERIFY(!reset.status(true).value("available").toBool());
        QDir().rmdir(statePath());
    }
    void serverConfiguration() {
        const QByteArray base = "port=8081\nroot=/tmp\nbind=127.0.0.1\nws_path=/ws\n"
            "backend_ws=ws://127.0.0.1:9002\nmax_request_bytes=8192\nlog_level=info\nauth_file=auth.json\n";
        const auto config = dir.path() + "/frontend.conf";
        write(config, base); ServerConfig cfg; QString error;
        QVERIFY(loadAndValidateServerConfig(config, cfg, error));
        QCOMPARE(cfg.passwordResetWindowSeconds, 60); QVERIFY(cfg.allowedHosts.isEmpty());
        write(config, base + "password_reset_window_seconds=0\nallowed_hosts=charger.example,::1\n");
        QVERIFY(loadAndValidateServerConfig(config, cfg, error));
        QCOMPARE(cfg.passwordResetWindowSeconds, 0); QCOMPARE(cfg.allowedHosts.size(), 2);
        for (const auto &extra : {QByteArray("allowed_hosts=*.example\n"), QByteArray("allowed_hosts=http://charger\n"),
                                 QByteArray("password_reset_window_seconds=-1\n"),
                                 QByteArray("password_reset_boot_status_path=relative\n")}) {
            write(config, base + extra); QVERIFY(!loadAndValidateServerConfig(config, cfg, error));
        }
    }
};
QTEST_GUILESS_MAIN(PasswordResetTest)
#include "PasswordResetTest.moc"
