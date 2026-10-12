// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_PASSWORD_RESET_HPP
#define EVEREST_UI_PASSWORD_RESET_HPP

#include <QJsonObject>
#include <QString>
#include <functional>

class AuthManager;

// Owns the boot-scoped reset allowance. All mutations are serialized with
// flock.
class PasswordReset {
public:
    using Clock = std::function<qint64()>;
    PasswordReset(QString markerPath, int windowSeconds, Clock clock = bootMilliseconds);
    void initialize();
    QJsonObject status(bool hasUser);
    // HTTP status; error contains a stable public error code on failure.
    int reset(AuthManager &auth, QString &error);
    static qint64 bootMilliseconds();

private:
    enum class Load { Missing, Valid, Invalid };
    Load load();
    bool save();
    bool clearMarker();
    QString reason() const;
    QString m_markerPath;
    QString m_directory;
    QString m_bootId;
    int m_windowSeconds;
    Clock m_clock;
    qint64 m_start;
    qint64 m_deadline = 0;
    QString m_classification;
    bool m_pending = false;
    bool m_allowed = false;
    bool m_enabled = false;
    bool m_valid = false;
    bool m_processBlocked = false;
};

#endif // EVEREST_UI_PASSWORD_RESET_HPP
