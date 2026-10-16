// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH

#ifndef EVEREST_UI_LAYERED_CONFIG_HPP
#define EVEREST_UI_LAYERED_CONFIG_HPP

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace UiConfig {
enum class Kind {
    Backend,
    Frontend
};
struct Paths {
    QString defaults;
    QString overlay;
};
struct Entry {
    QString value;
    QString path;
    int line = 0;
};
struct Snapshot {
    QMap<QString, Entry> entries;
    QStringList files;
    QString error;
    bool valid() const {
        return error.isEmpty();
    }
    bool contains(const QString& key) const {
        return entries.contains(key);
    }
    QString value(const QString& key) const {
        return entries.value(key).value;
    }
    QString source(const QString& key) const;
    QMap<QString, QString> values(const QString& prefix = {}) const;
};

Paths pathsFor(Kind kind);
Snapshot load(const Paths& paths);

// Readers receive either a complete new snapshot or the last complete valid snapshot.
class Store {
public:
    explicit Store(Paths paths);
    Snapshot read();

private:
    Paths m_paths;
    Snapshot m_snapshot;
    bool m_loaded = false;
    QString m_lastError;
    QMutex m_mutex;
};

enum class Update {
    Set,
    Mask,
    Inherit
};
bool writeOverlay(const Paths& paths, const QString& key, Update update, const QString& value, QString& error);
bool writeOverride(Kind kind, const QString& key, Update update, const QString& value, QString& error);
} // namespace UiConfig
#endif
