// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "PasswordReset.hpp"
#include "AuthManager.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <utility>

namespace {
class BootLock {
public:
    explicit BootLock(const QString &directory) {
        fd = ::open(QFile::encodeName(directory + "/password-reset.lock").constData(),
                    O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        acquired = fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
    }
    ~BootLock() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    bool acquired = false;

private:
    int fd = -1;
};
constexpr qint64 maxExactInteger = 9007199254740991LL;
} // namespace

qint64 PasswordReset::bootMilliseconds() {
    timespec time{};
    if (::clock_gettime(CLOCK_BOOTTIME, &time) != 0) {
        return -1;
    }
    return qint64(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}

PasswordReset::PasswordReset(QString markerPath, int windowSeconds, Clock clock)
    : m_markerPath(std::move(markerPath)), m_windowSeconds(windowSeconds),
      m_clock(std::move(clock)), m_start(m_clock()) {}

PasswordReset::Load PasswordReset::load() {
    QFile file(m_directory + QStringLiteral("/password-reset-state.json"));
    if (!file.exists()) {
        return Load::Missing;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
        return Load::Invalid;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return Load::Invalid;
    }
    const auto o = doc.object();
    const QString id = o.value(QStringLiteral("bootId")).toString();
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$"));
    const QString classification = o.value(QStringLiteral("classification")).toString();
    const double deadline = o.value(QStringLiteral("deadlineMs")).toDouble(-1);
    if (o.value(QStringLiteral("version")).toInt() != 1 || !uuid.match(id).hasMatch() ||
        (classification != QStringLiteral("cold") &&
         classification != QStringLiteral("warm") &&
         classification != QStringLiteral("invalid")) ||
        !o.value(QStringLiteral("deadlineMs")).isDouble() || !std::isfinite(deadline) || deadline < 0 ||
        deadline > maxExactInteger || std::floor(deadline) != deadline ||
        !o.value(QStringLiteral("markerClearPending")).isBool() ||
        !o.value(QStringLiteral("resetAllowed")).isBool()) {
        return Load::Invalid;
    }
    if (id != m_bootId) {
        return Load::Missing;
    }
    m_classification = classification;
    m_deadline = qint64(deadline);
    m_pending = o.value(QStringLiteral("markerClearPending")).toBool();
    m_allowed = o.value(QStringLiteral("resetAllowed")).toBool();
    return Load::Valid;
}

bool PasswordReset::save() {
    QSaveFile file(m_directory + QStringLiteral("/password-reset-state.json"));
    const QJsonObject o{{QStringLiteral("version"), 1},
                        {QStringLiteral("bootId"), m_bootId},
                        {QStringLiteral("classification"), m_classification},
                        {QStringLiteral("deadlineMs"), double(m_deadline)},
                        {QStringLiteral("markerClearPending"), m_pending},
                        {QStringLiteral("resetAllowed"), m_allowed}};
    const auto bytes = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        qWarning() << "Password reset: cannot persist boot state";
        m_processBlocked = true;
        return false;
    }
    return true;
}

bool PasswordReset::clearMarker() {
    QFile file(m_markerPath);
    // Never use WriteOnly: NVMEM contains other registers which must survive.
    return file.open(QIODevice::ReadWrite) && file.seek(28) &&
           file.write(QByteArray(4, '\0')) == 4 && file.flush();
}

void PasswordReset::initialize() {
    m_directory = qEnvironmentVariable("RUNTIME_DIRECTORY");
    if (m_directory.isEmpty() || m_directory.contains(':') || !QDir::isAbsolutePath(m_directory) ||
        !QFileInfo(m_directory).isDir()) {
        return;
    }
    m_enabled = m_windowSeconds > 0;
    QFile boot("/proc/sys/kernel/random/boot_id");
    if (!boot.open(QIODevice::ReadOnly) || m_start < 0) {
        return;
    }
    m_bootId = QString::fromLatin1(boot.readAll()).trimmed();
    if (m_bootId.isEmpty()) {
        return;
    }
    BootLock lock(m_directory);
    if (!lock.acquired) {
        m_processBlocked = true;
        return;
    }
    const auto loaded = load();
    if (loaded == Load::Invalid) {
        m_processBlocked = true;
        return;
    }
    if (loaded == Load::Missing) {
        m_deadline = m_start + qint64(m_windowSeconds) * 1000;
        QFile marker(m_markerPath);
        QByteArray bytes;
        if (marker.open(QIODevice::ReadOnly) && marker.seek(28)) {
            bytes = marker.read(4);
        }
        m_classification = QStringLiteral("invalid");
        if (bytes.size() == 4) {
            const quint32 value = qFromLittleEndian<quint32>(bytes.constData());
            if (value == 0) {
                m_classification = QStringLiteral("cold");
            } else if (value == 0x5741524d) {
                m_classification = QStringLiteral("warm");
            }
        }
        m_allowed = m_enabled && m_classification == QStringLiteral("cold");
        m_pending = m_classification != QStringLiteral("invalid");
        if (!save()) {
            return;
        }
    } else if (m_pending) {
        // An interrupted initialization may already have cleared the register.
        m_allowed = false;
        if (!save()) {
            return;
        }
    }
    m_valid = true;
    if (m_pending) {
        if (clearMarker()) {
            m_pending = false;
        } else {
            m_allowed = false;
            qWarning() << "Password reset: marker clear failed; will retry on next start";
        }
        save();
    }
}

QString PasswordReset::reason() const {
    if (!m_enabled) {
        return QStringLiteral("disabled");
    }
    if (m_classification == QStringLiteral("invalid")) {
        return QStringLiteral("boot_info_invalid");
    }
    if (m_classification == QStringLiteral("warm")) {
        return QStringLiteral("warm_boot");
    }
    if (m_processBlocked || !m_valid || !m_allowed || m_pending) {
        return QStringLiteral("unavailable_this_boot");
    }
    const auto now = m_clock();
    if (now < 0) {
        return QStringLiteral("unavailable_this_boot");
    }
    if (now >= m_deadline) {
        return QStringLiteral("expired");
    }
    return {};
}

QJsonObject PasswordReset::status(bool hasUser) {
    // Observe allowances consumed by another process without polling the
    // browser.
    if (m_valid && !m_processBlocked && load() != Load::Valid) {
        m_valid = false;
        m_processBlocked = true;
    }
    QString why = reason();
    if (!hasUser) {
        why = QStringLiteral("setup_required");
    }
    const qint64 left = why.isEmpty() ? qMax<qint64>(0, m_deadline - m_clock()) : 0;
    return {
        {"available", why.isEmpty()},
        {"remainingSeconds", double(left) / 1000.0},
        {"windowSeconds", m_windowSeconds},
        {"reason", why}};
}

int PasswordReset::reset(AuthManager &auth, QString &error) {
    if (!m_enabled || m_processBlocked || !m_valid) {
        error = QStringLiteral("reset_unavailable");
        return 403;
    }
    BootLock lock(m_directory);
    if (!lock.acquired) {
        error = QStringLiteral("reset_temporarily_unavailable");
        return 503;
    }
    if (load() != Load::Valid) {
        m_valid = false;
        m_processBlocked = true;
        error = QStringLiteral("reset_unavailable");
        return 403;
    }
    if (!reason().isEmpty() || !auth.hasUser()) {
        error = QStringLiteral("reset_unavailable");
        return 403;
    }
    m_allowed = false;
    if (!save()) {
        error = QStringLiteral("reset_failed");
        return 500;
    }
    QString detail;
    if (!auth.resetUser(detail)) {
        qWarning() << "Password reset:" << detail;
        error = QStringLiteral("reset_failed");
        return 500;
    }
    return 200;
}
