// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH

#include "LayeredConfig.hpp"
#include "InstallPaths.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMutexLocker>
#include <QSaveFile>

#include <filesystem>
#include <system_error>
#include <utility>

namespace UiConfig {
namespace {
struct Directive {
    QString key;
    QString value;
    bool mask = false;
};

bool validKey(const QString& key) {
    if (key.isEmpty() || key.startsWith(QLatin1Char('!')) || key.startsWith(QLatin1Char('#'))) {
        return false;
    }
    for (const QChar ch : key) {
        if (ch.isSpace() || ch.isNull() || ch == QLatin1Char('=')) {
            return false;
        }
    }
    return true;
}

bool parseLine(const QString& line, Directive& directive) {
    const QString text = line.trimmed();
    directive = {};
    if (text.isEmpty() || text.startsWith(QLatin1Char('#'))) {
        return true;
    }
    const int separator = text.indexOf(QLatin1Char('='));
    if (text.startsWith(QLatin1Char('!')) && separator < 0) {
        directive.mask = true;
        directive.key = text.mid(1).trimmed();
    } else if (separator > 0) {
        directive.key = text.left(separator).trimmed();
        directive.value = text.mid(separator + 1).trimmed();
    } else {
        return false;
    }
    return validKey(directive.key) && !directive.value.contains(QChar::Null);
}

bool missingFile(const QString& path, QString& error) {
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(QFile::encodeName(path).constData(), statusError);
    if (statusError && statusError != std::errc::no_such_file_or_directory) {
        error = QStringLiteral("Cannot inspect configuration %1: %2")
                    .arg(path, QString::fromStdString(statusError.message()));
        return false;
    }
    return status.type() == std::filesystem::file_type::not_found;
}

bool readFile(const QString& path, QStringList& lines, QString& error) {
    if (path.isEmpty() || missingFile(path, error)) {
        return true;
    }
    if (!error.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    QFile file(path);
    if (!info.isFile()) {
        error = QStringLiteral("Configuration is not a readable regular file: %1").arg(path);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read configuration %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = QStringLiteral("Cannot read configuration %1: %2").arg(path, file.errorString());
        return false;
    }
    QString text = QString::fromUtf8(contents);
    if (text.startsWith(QChar::ByteOrderMark)) {
        text.remove(0, 1);
    }
    lines = text.split(QLatin1Char('\n'));
    return true;
}

bool noSymlinks(const QString& path) {
    QString current = QFileInfo(path).absoluteFilePath();
    while (true) {
        if (QFileInfo(current).isSymLink()) {
            return false;
        }
        const QString parent = QFileInfo(current).absolutePath();
        if (parent == current) {
            return true;
        }
        current = parent;
    }
}
} // namespace

QString Snapshot::source(const QString& key) const {
    const Entry entry = entries.value(key);
    return entry.path.isEmpty() ? files.join(QStringLiteral(", "))
                                : QStringLiteral("%1:%2").arg(entry.path).arg(entry.line);
}

QMap<QString, QString> Snapshot::values(const QString& prefix) const {
    QMap<QString, QString> result;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it.key().startsWith(prefix) && it.key().size() > prefix.size()) {
            result.insert(it.key().mid(prefix.size()), it.value().value);
        }
    }
    return result;
}

Paths pathsFor(Kind kind) {
    const bool backend = kind == Kind::Backend;
    const QString name = backend ? QStringLiteral("backend.conf") : QStringLiteral("frontend.conf");
    const QString appDir = QCoreApplication::applicationDirPath();
    QString defaults = backend ? QStringLiteral(EVEREST_UI_INSTALL_BACKEND_DEFAULTS)
                               : QStringLiteral(EVEREST_UI_INSTALL_FRONTEND_DEFAULTS);
    QString inspectionError;
    if (missingFile(defaults, inspectionError)) {
        const QStringList candidates{
            QDir(appDir).filePath(QStringLiteral("../config/") + name),
            QDir(appDir).filePath(name),
            QDir(appDir).filePath(backend ? QStringLiteral("../api/backend.conf") : QStringLiteral("../frontend.conf")),
        };
        for (const QString& candidate : candidates) {
            inspectionError.clear();
            if (!missingFile(candidate, inspectionError)) {
                defaults = QDir::cleanPath(candidate);
                break;
            }
        }
    }
    return {defaults, backend ? QStringLiteral(EVEREST_UI_INSTALL_BACKEND_CONFIG)
                              : QStringLiteral(EVEREST_UI_INSTALL_FRONTEND_CONFIG)};
}

Snapshot load(const Paths& paths) {
    Snapshot result;
    for (const QString& path : {paths.defaults, paths.overlay}) {
        QStringList lines;
        if (!readFile(path, lines, result.error)) {
            result.entries.clear();
            return result;
        }
        if (lines.isEmpty()) {
            continue;
        }
        result.files.append(path);
        for (int i = 0; i < lines.size(); ++i) {
            Directive directive;
            if (!parseLine(lines.at(i), directive)) {
                result.error =
                    QStringLiteral("Invalid configuration at %1:%2: expected key=value or !key").arg(path).arg(i + 1);
                result.entries.clear();
                return result;
            }
            if (directive.mask) {
                result.entries.remove(directive.key);
            } else if (!directive.key.isEmpty()) {
                result.entries.insert(directive.key, {directive.value, path, i + 1});
            }
        }
    }
    return result;
}

Store::Store(Paths paths) : m_paths(std::move(paths)) {
}

Snapshot Store::read() {
    QMutexLocker lock(&m_mutex);
    const Snapshot candidate = load(m_paths);
    if (candidate.valid()) {
        m_snapshot = candidate;
        m_loaded = true;
        m_lastError.clear();
    } else {
        if (candidate.error != m_lastError) {
            qWarning().noquote() << candidate.error
                                 << (m_loaded ? "(retaining last valid configuration)" : "(no valid configuration)");
            m_lastError = candidate.error;
        }
        if (!m_loaded) {
            return candidate;
        }
    }
    return m_snapshot;
}

bool writeOverlay(const Paths& paths, const QString& key, Update update, const QString& value, QString& error) {
    error.clear();
    const QString target = QDir::cleanPath(QFileInfo(paths.overlay).absoluteFilePath());
    if (paths.overlay.isEmpty() || !validKey(key) || value.contains(QLatin1Char('\n')) ||
        value.contains(QLatin1Char('\r')) || value.contains(QChar::Null) || !noSymlinks(target) ||
        target == QDir::cleanPath(QFileInfo(paths.defaults).absoluteFilePath())) {
        error = QStringLiteral("Unsafe configuration overlay target or invalid directive");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        error = QStringLiteral("Cannot create overlay directory for %1").arg(target);
        return false;
    }
    QLockFile lock(target + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) {
        error = QStringLiteral("Configuration overlay is locked: %1").arg(target);
        return false;
    }
    if (!noSymlinks(target)) {
        error = QStringLiteral("Refusing configuration overlay symlink: %1").arg(target);
        return false;
    }
    QStringList lines;
    if (!readFile(target, lines, error)) {
        return false;
    }
    QStringList output;
    for (int i = 0; i < lines.size(); ++i) {
        Directive directive;
        if (!parseLine(lines.at(i), directive)) {
            error = QStringLiteral("Invalid configuration at %1:%2").arg(target).arg(i + 1);
            return false;
        }
        if (directive.key != key) {
            output.append(lines.at(i));
        }
    }
    QString text = output.join(QLatin1Char('\n'));
    if (update != Update::Inherit) {
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
            text += QLatin1Char('\n');
        }
        text += update == Update::Mask ? QLatin1Char('!') + key : key + QLatin1Char('=') + value.trimmed();
        text += QLatin1Char('\n');
    }
    QSaveFile file(target);
    const QByteArray bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QStringLiteral("Cannot save configuration overlay %1: %2").arg(target, file.errorString());
        return false;
    }
    return true;
}

bool writeOverride(Kind kind, const QString& key, Update update, const QString& value, QString& error) {
    return writeOverlay(pathsFor(kind), key, update, value, error);
}
} // namespace UiConfig
