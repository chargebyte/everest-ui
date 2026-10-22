// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "LoginState.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
QString runtimeDirectory() {
    return qEnvironmentVariable("RUNTIME_DIRECTORY", QStringLiteral("/run/everest-ui"));
}

bool writeState(bool allowed, const QString& operation, QString& error) {
    const auto path = UiRuntime::loginStatePath();
    if (!QDir::isAbsolutePath(path) || QFileInfo(runtimeDirectory()).isSymLink() || QFileInfo(path).isSymLink()) {
        error = QStringLiteral("Unsafe login state path");
        return false;
    }
    QSaveFile file(path);
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1},
                                  {"loginAllowed", allowed},
                                  {"loginDisabledReason", allowed ? QString() : QStringLiteral("waiting_for_reboot")},
                                  {"rebootOperation", operation}})
            .toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
} // namespace

namespace UiRuntime {
QString loginStatePath() {
    return QDir(runtimeDirectory()).filePath(QStringLiteral("login-state.json"));
}

bool initializeLoginState(QString& error) {
    const auto directory = runtimeDirectory();
    if (!QDir::isAbsolutePath(directory) || QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) {
        error = QStringLiteral("Cannot create safe runtime directory for login state");
        return false;
    }
    if (!QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        error = QStringLiteral("Cannot secure login state runtime directory");
        return false;
    }
    return writeState(true, {}, error);
}

LoginState readLoginState() {
    const auto path = loginStatePath();
    QFile file(path);
    if (!QDir::isAbsolutePath(path) || QFileInfo(runtimeDirectory()).isSymLink() || QFileInfo(path).isSymLink() ||
        !file.open(QIODevice::ReadOnly) || file.size() > 4096)
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const auto object = document.object();
    const auto allowed = object.value(QStringLiteral("loginAllowed"));
    const auto reason = object.value(QStringLiteral("loginDisabledReason"));
    const auto operation = object.value(QStringLiteral("rebootOperation"));
    if (object.value(QStringLiteral("version")) != QJsonValue(1) || !allowed.isBool() || !reason.isString() ||
        !operation.isString())
        return {};
    if (allowed.toBool()) {
        return reason.toString().isEmpty() && operation.toString().isEmpty() ? LoginState{true, {}, {}} : LoginState{};
    }
    if (reason.toString() != QLatin1String("waiting_for_reboot") ||
        (operation.toString() != QLatin1String("factory_reset") && operation.toString() != QLatin1String("reboot")))
        return {};
    return {false, reason.toString(), operation.toString()};
}

bool blockLoginForReboot(const QString& operation, QString& error) {
    if (operation != QLatin1String("factory_reset") && operation != QLatin1String("reboot")) {
        error = QStringLiteral("Invalid reboot operation");
        return false;
    }
    if (writeState(false, operation, error))
        return true;
    qCritical() << "Unable to publish reboot login restriction:" << error;
    // Missing state fails closed. If even invalidation fails, stop the API and its launcher-managed sibling.
    if (!QFile::remove(loginStatePath()) && QFileInfo::exists(loginStatePath()))
        QCoreApplication::exit(1);
    return false;
}
} // namespace UiRuntime
