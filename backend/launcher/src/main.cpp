// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "InstallPaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QSocketNotifier>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr int kBackendPollIntervalMs = 200;
constexpr int kBackendStartupTimeoutMs = 10000;

#ifdef Q_OS_UNIX
int g_signalSockets[2] = {-1, -1};

void handleUnixSignal(int signalNumber) {
    const char signalByte = static_cast<char>(signalNumber);
    if (g_signalSockets[0] != -1) {
        const ssize_t bytesWritten = ::write(g_signalSockets[0], &signalByte, sizeof(signalByte));
        Q_UNUSED(bytesWritten);
    }
}

QString unixSignalName(int signalNumber) {
    switch (signalNumber) {
    case SIGINT:
        return QStringLiteral("SIGINT");
    case SIGQUIT:
        return QStringLiteral("SIGQUIT");
    default:
        return QStringLiteral("signal %1").arg(signalNumber);
    }
}

bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool installUnixSignalHandler(int signalNumber) {
    struct sigaction action = {};
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    return ::sigaction(signalNumber, &action, nullptr) == 0;
}

class UnixSignalHandler final : public QObject {
    Q_OBJECT

public:
    explicit UnixSignalHandler(QObject *parent = nullptr)
        : QObject(parent) {
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalSockets) != 0) {
            QTextStream(stderr) << "Failed to create signal socket pair.\n";
            return;
        }

        if (!setNonBlocking(g_signalSockets[0]) || !setNonBlocking(g_signalSockets[1])) {
            QTextStream(stderr) << "Failed to configure signal socket pair.\n";
            closeSignalSockets();
            return;
        }

        m_signalNotifier = new QSocketNotifier(g_signalSockets[1],
                                               QSocketNotifier::Read,
                                               this);
        connect(m_signalNotifier, &QSocketNotifier::activated, this,
                [this]() {
                    handleSignalNotification();
                });

        if (!installUnixSignalHandler(SIGINT) || !installUnixSignalHandler(SIGQUIT)) {
            QTextStream(stderr) << "Failed to install Unix signal handlers.\n";
        }
    }

    ~UnixSignalHandler() override {
        closeSignalSockets();
    }

signals:
    void shutdownSignalReceived(int signalNumber);

private slots:
    void handleSignalNotification() {
        m_signalNotifier->setEnabled(false);

        char signalByte = 0;
        if (::read(g_signalSockets[1], &signalByte, sizeof(signalByte)) > 0) {
            emit shutdownSignalReceived(static_cast<unsigned char>(signalByte));
        }

        m_signalNotifier->setEnabled(true);
    }

private:
    void closeSignalSockets() {
        if (m_signalNotifier) {
            m_signalNotifier->setEnabled(false);
        }

        if (g_signalSockets[0] != -1) {
            ::close(g_signalSockets[0]);
            g_signalSockets[0] = -1;
        }

        if (g_signalSockets[1] != -1) {
            ::close(g_signalSockets[1]);
            g_signalSockets[1] = -1;
        }
    }

    QSocketNotifier *m_signalNotifier = nullptr;
};
#endif

QString resolveFirstExistingPath(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return candidates.isEmpty() ? QString() : QDir::cleanPath(candidates.constFirst());
}

QString resolveBackendBinaryPath() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    return resolveFirstExistingPath({
        QStringLiteral(EVEREST_UI_INSTALL_API_BINARY),
        QDir(applicationDir).absoluteFilePath(QStringLiteral("api")),
    });
}

QString resolveFrontendBinaryPath() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    return resolveFirstExistingPath({
        QStringLiteral(EVEREST_UI_INSTALL_WEBSERVER_BINARY),
        QDir(applicationDir).absoluteFilePath(QStringLiteral("webserver")),
    });
}

QString resolveBackendConfigPath() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    return resolveFirstExistingPath({
        QStringLiteral(EVEREST_UI_INSTALL_BACKEND_CONFIG),
        QDir(applicationDir).absoluteFilePath(QStringLiteral("../config/backend.conf")),
        QDir(applicationDir).absoluteFilePath(QStringLiteral("backend.conf")),
    });
}

QString readConfigValue(const QString &configPath, const QString &configKey) {
    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream stream(&configFile);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const QStringList parts = line.split(QLatin1Char('='));
        if (parts.size() != 2) {
            continue;
        }

        const QString key = parts.at(0).trimmed();
        if (key != configKey) {
            continue;
        }

        return parts.at(1).trimmed();
    }

    return QString();
}

class WebUiLauncher : public QObject {
    Q_OBJECT

public:
    explicit WebUiLauncher(QObject *parent = nullptr)
        : QObject(parent) {
        m_backend.setProcessChannelMode(QProcess::ForwardedChannels);
        m_frontend.setProcessChannelMode(QProcess::ForwardedChannels);
        m_backendPollTimer.setInterval(kBackendPollIntervalMs);
        m_backendPollTimer.setSingleShot(false);
        m_backendStartupTimeout.setInterval(kBackendStartupTimeoutMs);
        m_backendStartupTimeout.setSingleShot(true);

        connect(&m_backend, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                &WebUiLauncher::onBackendFinished);
        connect(&m_frontend, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                &WebUiLauncher::onFrontendFinished);
        connect(&m_backendPollTimer, &QTimer::timeout, this, &WebUiLauncher::pollBackend);
        connect(&m_backendStartupTimeout, &QTimer::timeout, this,
                &WebUiLauncher::onBackendStartupTimeout);
        connect(qApp, &QCoreApplication::aboutToQuit, this, &WebUiLauncher::shutdownChildren);
    }

    int start() {
        m_backendBinaryPath = resolveBackendBinaryPath();
        m_frontendBinaryPath = resolveFrontendBinaryPath();
        m_backendConfigPath = resolveBackendConfigPath();

        if (!QFileInfo::exists(m_backendBinaryPath)) {
            QTextStream(stderr) << "Missing api binary: " << m_backendBinaryPath << "\n";
            return 1;
        }
        if (!QFileInfo::exists(m_frontendBinaryPath)) {
            QTextStream(stderr) << "Missing webserver binary: " << m_frontendBinaryPath << "\n";
            return 1;
        }
        if (!QFileInfo::exists(m_backendConfigPath)) {
            QTextStream(stderr) << "Missing backend config: " << m_backendConfigPath << "\n";
            return 1;
        }

        const QString configuredPort =
            readConfigValue(m_backendConfigPath, QStringLiteral("backend_port"));
        if (configuredPort.isEmpty()) {
            QTextStream(stderr)
                << "Missing required configuration entry 'backend_port' in "
                << m_backendConfigPath
                << ". Add a line like 'backend_port=9002'.\n";
            return 1;
        }

        bool validPort = false;
        const quint16 port = configuredPort.toUShort(&validPort);
        if (!validPort || port == 0) {
            QTextStream(stderr)
                << "Invalid configuration entry 'backend_port=" << configuredPort << "' in "
                << m_backendConfigPath
                << ". Use a TCP port like 'backend_port=9002'.\n";
            return 1;
        }
        m_backendPort = port;

        QTextStream(stdout) << "Starting api: " << m_backendBinaryPath << "\n";
        m_backend.start(m_backendBinaryPath, QStringList());
        if (!m_backend.waitForStarted()) {
            QTextStream(stderr) << "Failed to start api: " << m_backend.errorString() << "\n";
            return 1;
        }

        m_backendPollTimer.start();
        m_backendStartupTimeout.start();
        return 0;
    }

private slots:
    void pollBackend() {
        if (m_frontendStarted) {
            return;
        }

        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, m_backendPort);
        if (!socket.waitForConnected(50)) {
            return;
        }

        m_backendPollTimer.stop();
        m_backendStartupTimeout.stop();
        m_frontendStarted = true;

        QTextStream(stdout) << "Backend is reachable on 127.0.0.1:" << m_backendPort << "\n";
        QTextStream(stdout) << "Starting webserver: " << m_frontendBinaryPath << "\n";
        m_frontend.start(m_frontendBinaryPath, QStringList());
        if (!m_frontend.waitForStarted()) {
            QTextStream(stderr) << "Failed to start webserver: " << m_frontend.errorString()
                               << "\n";
            terminateSibling(m_backend);
            QCoreApplication::exit(1);
        }
    }

    void onBackendStartupTimeout() {
        QTextStream(stderr)
            << "Backend did not become reachable on 127.0.0.1:" << m_backendPort
            << " within " << (kBackendStartupTimeoutMs / 1000) << " seconds.\n";
        terminateSibling(m_backend);
        QCoreApplication::exit(1);
    }

    void onBackendFinished(int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_shuttingDown) {
            return;
        }

        m_backendPollTimer.stop();
        m_backendStartupTimeout.stop();
        if (m_frontend.state() != QProcess::NotRunning) {
            terminateSibling(m_frontend);
        }

        QTextStream(stderr)
            << "Api exited "
            << (exitStatus == QProcess::NormalExit ? "normally" : "abnormally")
            << " with code " << exitCode << ".\n";
        QCoreApplication::exit(exitStatus == QProcess::NormalExit ? exitCode : 1);
    }

    void onFrontendFinished(int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_shuttingDown) {
            return;
        }

        if (m_backend.state() != QProcess::NotRunning) {
            terminateSibling(m_backend);
        }

        QTextStream(stderr)
            << "Webserver exited "
            << (exitStatus == QProcess::NormalExit ? "normally" : "abnormally")
            << " with code " << exitCode << ".\n";
        QCoreApplication::exit(exitStatus == QProcess::NormalExit ? exitCode : 1);
    }

    void shutdownChildren() {
        if (m_shuttingDown) {
            return;
        }

        m_shuttingDown = true;
        m_backendPollTimer.stop();
        m_backendStartupTimeout.stop();
        terminateSibling(m_frontend);
        terminateSibling(m_backend);
    }

private:
    void terminateSibling(QProcess &process) {
        if (process.state() == QProcess::NotRunning) {
            return;
        }

        QString command = process.program();
        const QString arguments = process.arguments().join(QStringLiteral(" "));
        if (!arguments.isEmpty()) {
            command += QStringLiteral(" ") + arguments;
        }

        QTextStream(stderr)
            << "Terminating sibling process "
            << command
            << " pid=" << process.processId()
            << ".\n";
        process.terminate();
        if (!process.waitForFinished(3000)) {
            process.kill();
            process.waitForFinished(3000);
        }
    }

    QProcess m_backend;
    QProcess m_frontend;
    QTimer m_backendPollTimer;
    QTimer m_backendStartupTimeout;
    QString m_backendBinaryPath;
    QString m_frontendBinaryPath;
    QString m_backendConfigPath;
    quint16 m_backendPort = 0;
    bool m_frontendStarted = false;
    bool m_shuttingDown = false;
};
} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("webui"));

#ifdef Q_OS_UNIX
    UnixSignalHandler unixSignalHandler;
    QObject::connect(&unixSignalHandler, &UnixSignalHandler::shutdownSignalReceived,
                     &app, [](int signalNumber) {
                         QTextStream(stderr)
                             << "Received " << unixSignalName(signalNumber)
                             << ", shutting down.\n";
                         QCoreApplication::quit();
                     });
#endif

    WebUiLauncher launcher;
    const int startResult = launcher.start();
    if (startResult != 0) {
        return startResult;
    }

    return app.exec();
}

#include "main.moc"
