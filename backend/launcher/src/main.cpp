// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>

namespace {
constexpr int kBackendPollIntervalMs = 200;
constexpr int kBackendStartupTimeoutMs = 10000;

QString resolveInstallPath(const QString &relativePath) {
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relativePath);
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
        m_backendBinaryPath = resolveInstallPath(QStringLiteral("api"));
        m_frontendBinaryPath = resolveInstallPath(QStringLiteral("webserver"));
        m_backendConfigPath = resolveInstallPath(QStringLiteral("../config/backend.conf"));

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

    WebUiLauncher launcher;
    const int startResult = launcher.start();
    if (startResult != 0) {
        return startResult;
    }

    return app.exec();
}

#include "main.moc"
