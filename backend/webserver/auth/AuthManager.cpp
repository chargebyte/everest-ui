// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "AuthManager.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace {
constexpr int kSessionTtlSeconds = 12 * 60 * 60;
constexpr int kSaltBytes = 16;
constexpr int kSessionBytes = 32;

QString nowUtcIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QByteArray base64Url(const QByteArray &data) {
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
} // namespace

AuthManager::AuthManager(QString authFilePath) : m_authFilePath(std::move(authFilePath)) {}

bool AuthManager::initialize(QString &errorMessage) {
    return load(errorMessage);
}

bool AuthManager::setupRequired() const {
    return m_loaded && !m_hasUser;
}

bool AuthManager::hasUser() const {
    return m_loaded && m_hasUser;
}

bool AuthManager::createUser(const QString &username,
                             const QString &password,
                             QString &errorMessage) {
    if (!m_loaded) {
        errorMessage = QStringLiteral("Auth store is not loaded");
        return false;
    }
    if (m_hasUser) {
        errorMessage = QStringLiteral("User already exists");
        return false;
    }
    if (!isUsernameValid(username)) {
        errorMessage = QStringLiteral("Invalid username");
        return false;
    }
    if (!isPasswordValid(password)) {
        errorMessage = QStringLiteral("Invalid password");
        return false;
    }

    const QString timestamp = nowUtcIso();
    UserRecord user;
    user.username = username.trimmed().toLower();
    user.salt = createRandomToken(kSaltBytes);
    user.password = hashPassword(password, user.salt);
    user.createdAt = timestamp;
    user.updatedAt = timestamp;

    m_user = user;
    m_hasUser = true;
    if (!save(errorMessage)) {
        m_hasUser = false;
        m_user = UserRecord{};
        return false;
    }
    return true;
}

bool AuthManager::authenticate(const QString &username, const QString &password) const {
    if (!m_loaded || !m_hasUser) {
        return false;
    }
    if (username.trimmed().toLower() != m_user.username) {
        return false;
    }
    return hashPassword(password, m_user.salt) == m_user.password;
}

QString AuthManager::createSession(const QString &username) {
    if (!m_loaded || !m_hasUser || username.trimmed().toLower() != m_user.username) {
        return QString();
    }

    QString sessionId;
    do {
        sessionId = createRandomToken(kSessionBytes);
    } while (m_sessions.contains(sessionId));

    m_sessions.insert(sessionId, SessionRecord{m_user.username, newSessionExpiry()});
    return sessionId;
}

bool AuthManager::validateSession(const QString &sessionId) {
    if (sessionId.isEmpty()) {
        return false;
    }

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) {
        return false;
    }

    if (it->expiresAt <= QDateTime::currentDateTimeUtc()) {
        m_sessions.erase(it);
        return false;
    }

    it->expiresAt = newSessionExpiry();
    return true;
}

void AuthManager::removeSession(const QString &sessionId) {
    if (!sessionId.isEmpty()) {
        m_sessions.remove(sessionId);
    }
}

bool AuthManager::load(QString &errorMessage) {
    m_loaded = false;
    m_hasUser = false;
    m_user = UserRecord{};

    if (!QFileInfo::exists(m_authFilePath)) {
        m_loaded = true;
        return true;
    }

    QFile file(m_authFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("Cannot open auth file: %1").arg(m_authFilePath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QStringLiteral("Invalid auth JSON in %1: %2")
                           .arg(m_authFilePath, parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        errorMessage = QStringLiteral("Unsupported auth file version in %1").arg(m_authFilePath);
        return false;
    }

    const QJsonValue userValue = root.value(QStringLiteral("user"));
    if (userValue.isUndefined() || userValue.isNull()) {
        m_loaded = true;
        return true;
    }
    if (!userValue.isObject()) {
        errorMessage = QStringLiteral("Invalid auth user object in %1").arg(m_authFilePath);
        return false;
    }

    const QJsonObject userObject = userValue.toObject();
    UserRecord user;
    user.username = userObject.value(QStringLiteral("username")).toString();
    user.password = userObject.value(QStringLiteral("password")).toString();
    user.salt = userObject.value(QStringLiteral("salt")).toString();
    user.createdAt = userObject.value(QStringLiteral("createdAt")).toString();
    user.updatedAt = userObject.value(QStringLiteral("updatedAt")).toString();
    if (!isUserRecordValid(user)) {
        errorMessage = QStringLiteral("Invalid auth user fields in %1").arg(m_authFilePath);
        return false;
    }

    m_user = user;
    m_hasUser = true;
    m_loaded = true;
    return true;
}

bool AuthManager::save(QString &errorMessage) const {
    const QFileInfo fileInfo(m_authFilePath);
    QDir dir(fileInfo.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        errorMessage = QStringLiteral("Cannot create auth directory: %1").arg(dir.absolutePath());
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    if (m_hasUser) {
        QJsonObject userObject;
        userObject.insert(QStringLiteral("username"), m_user.username);
        userObject.insert(QStringLiteral("password"), m_user.password);
        userObject.insert(QStringLiteral("salt"), m_user.salt);
        userObject.insert(QStringLiteral("createdAt"), m_user.createdAt);
        userObject.insert(QStringLiteral("updatedAt"), m_user.updatedAt);
        root.insert(QStringLiteral("user"), userObject);
    }

    QSaveFile file(m_authFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = QStringLiteral("Cannot write auth file: %1").arg(m_authFilePath);
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        errorMessage = QStringLiteral("Cannot write complete auth file: %1").arg(m_authFilePath);
        return false;
    }
    if (!file.commit()) {
        errorMessage = QStringLiteral("Cannot commit auth file: %1").arg(m_authFilePath);
        return false;
    }

    QFile::setPermissions(m_authFilePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool AuthManager::isUserRecordValid(const UserRecord &user) const {
    return isUsernameValid(user.username) && !user.password.isEmpty() && !user.salt.isEmpty() &&
           !user.createdAt.isEmpty() && !user.updatedAt.isEmpty();
}

bool AuthManager::isUsernameValid(const QString &username) const {
    static const QRegularExpression kUsernamePattern(QStringLiteral("^[A-Za-z0-9_.-]{1,64}$"));
    return kUsernamePattern.match(username.trimmed()).hasMatch();
}

bool AuthManager::isPasswordValid(const QString &password) const {
    return password.size() >= 8 && password.size() <= 256;
}

QString AuthManager::hashPassword(const QString &password, const QString &salt) const {
    const QByteArray hash = QCryptographicHash::hash(QString(password + salt).toUtf8(),
                                                     QCryptographicHash::Sha512);
    return QString::fromUtf8(hash.toBase64());
}

QString AuthManager::createRandomToken(int byteCount) const {
    QByteArray bytes;
    bytes.resize(byteCount);
    for (int i = 0; i < byteCount; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    return QString::fromUtf8(base64Url(bytes));
}

QDateTime AuthManager::newSessionExpiry() const {
    return QDateTime::currentDateTimeUtc().addSecs(kSessionTtlSeconds);
}
