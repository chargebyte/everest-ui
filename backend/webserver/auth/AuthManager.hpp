// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef AUTH_MANAGER_HPP
#define AUTH_MANAGER_HPP

#include <QDateTime>
#include <QHash>
#include <QString>

class AuthManager {
public:
    static constexpr const char *kSessionCookieName = "everest_ui_session";

    explicit AuthManager(QString authFilePath);

    bool initialize(QString &errorMessage);
    bool setupRequired() const;
    bool hasUser() const;

    bool createUser(const QString &username, const QString &password, QString &errorMessage);
    bool authenticate(const QString &username, const QString &password) const;

    QString createSession(const QString &username);
    bool validateSession(const QString &sessionId);
    void removeSession(const QString &sessionId);

private:
    struct UserRecord {
        QString username;
        QString password;
        QString salt;
        QString createdAt;
        QString updatedAt;
    };

    struct SessionRecord {
        QString username;
        QDateTime expiresAt;
    };

    bool load(QString &errorMessage);
    bool save(QString &errorMessage) const;
    bool isUserRecordValid(const UserRecord &user) const;
    bool isUsernameValid(const QString &username) const;
    bool isPasswordValid(const QString &password) const;
    QString hashPassword(const QString &password, const QString &salt) const;
    QString createRandomToken(int byteCount) const;
    QDateTime newSessionExpiry() const;

    QString m_authFilePath;
    bool m_loaded = false;
    bool m_hasUser = false;
    UserRecord m_user;
    QHash<QString, SessionRecord> m_sessions;
};

#endif // AUTH_MANAGER_HPP
