// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef APP_TITLE_RESOLVER_HPP
#define APP_TITLE_RESOLVER_HPP

#include <QObject>
#include <QString>
#include <QUrl>

class AppTitleResolver final : public QObject {
    Q_OBJECT

public:
    explicit AppTitleResolver(const QString &configuredTitle,
                              const QUrl &backendUrl,
                              QObject *parent = nullptr);

    QString title();

private:
    QString resolveFromBackend() const;

    QString m_configuredTitle;
    QUrl m_backendUrl;
    QString m_cachedTitle;
};

#endif // APP_TITLE_RESOLVER_HPP
