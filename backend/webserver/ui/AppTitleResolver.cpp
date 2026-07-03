// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "AppTitleResolver.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QWebSocket>

namespace {
constexpr int kTitleLookupTimeoutMs = 1500;
constexpr int kAppTitleRequestId = 1;

QString defaultTitle() {
    return QStringLiteral("EVerest WebUI");
}

bool isUsableTitleBase(const QString &titleBase) {
    const QString trimmedTitleBase = titleBase.trimmed();
    return !trimmedTitleBase.isEmpty() &&
           trimmedTitleBase.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) != 0;
}

QJsonObject appTitleRequest() {
    return QJsonObject{
        {QStringLiteral("requestId"), kAppTitleRequestId},
        {QStringLiteral("group"), QStringLiteral("system")},
        {QStringLiteral("action"), QStringLiteral("read_app_title")},
        {QStringLiteral("parameters"), QJsonObject{}},
    };
}
} // namespace

AppTitleResolver::AppTitleResolver(const QString &configuredTitle,
                                   const QUrl &backendUrl,
                                   QObject *parent)
    : QObject(parent),
      m_configuredTitle(configuredTitle.trimmed()),
      m_backendUrl(backendUrl) {}

QString AppTitleResolver::title() {
    if (!m_configuredTitle.isEmpty()) {
        return m_configuredTitle;
    }

    if (isUsableTitleBase(m_cachedTitle)) {
        return m_cachedTitle;
    }

    const QString backendTitle = resolveFromBackend();
    if (isUsableTitleBase(backendTitle)) {
        m_cachedTitle = backendTitle;
        return m_cachedTitle;
    }

    return defaultTitle();
}

QString AppTitleResolver::resolveFromBackend() const {
    if (!m_backendUrl.isValid() || m_backendUrl.host().isEmpty() || m_backendUrl.port() <= 0) {
        return QString();
    }

    QWebSocket socket;
    QEventLoop loop;
    QTimer timeout;
    QString appTitle;

    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QWebSocket::connected, &socket, [&socket]() {
        socket.sendTextMessage(QString::fromUtf8(
            QJsonDocument(appTitleRequest()).toJson(QJsonDocument::Compact)));
    });
    QObject::connect(&socket, &QWebSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QWebSocket::textMessageReceived, &loop,
                     [&socket, &loop, &appTitle](const QString &message) {
                         const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
                         if (document.isObject()) {
                             const QJsonObject response = document.object();
                             const QJsonObject parameters =
                                 response.value(QStringLiteral("parameters")).toObject();
                             appTitle = parameters.value(QStringLiteral("appTitle"))
                                            .toString()
                                            .trimmed();
                             const QString responseId =
                                 response.value(QStringLiteral("responseId")).toString();
                             if (!responseId.isEmpty()) {
                                 socket.sendTextMessage(QString::fromUtf8(
                                     QJsonDocument(QJsonObject{
                                         {QStringLiteral("type"), QStringLiteral("ack")},
                                         {QStringLiteral("responseId"), responseId},
                                     }).toJson(QJsonDocument::Compact)));
                             }
                         }
                         loop.quit();
                     });
    QObject::connect(&socket, &QWebSocket::binaryMessageReceived, &loop,
                     [&socket, &loop, &appTitle](const QByteArray &message) {
                         const QJsonDocument document = QJsonDocument::fromJson(message);
                         if (document.isObject()) {
                             const QJsonObject response = document.object();
                             const QJsonObject parameters =
                                 response.value(QStringLiteral("parameters")).toObject();
                             appTitle = parameters.value(QStringLiteral("appTitle"))
                                            .toString()
                                            .trimmed();
                             const QString responseId =
                                 response.value(QStringLiteral("responseId")).toString();
                             if (!responseId.isEmpty()) {
                                 socket.sendTextMessage(QString::fromUtf8(
                                     QJsonDocument(QJsonObject{
                                         {QStringLiteral("type"), QStringLiteral("ack")},
                                         {QStringLiteral("responseId"), responseId},
                                     }).toJson(QJsonDocument::Compact)));
                             }
                         }
                         loop.quit();
                     });

    timeout.start(kTitleLookupTimeoutMs);
    socket.open(m_backendUrl);
    loop.exec();
    timeout.stop();
    socket.close();

    if (!isUsableTitleBase(appTitle)) {
        return QString();
    }
    return appTitle;
}
