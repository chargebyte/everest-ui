// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef YAML_UTILS_HPP
#define YAML_UTILS_HPP

#include <QJsonValue>
#include <QJsonObject>
#include <QString>

struct YamlLoadResult {
    bool success = false;
    QJsonObject yamlRoot;
    QString error;
};

YamlLoadResult loadYamlFile(const QString &path);
QString formatYamlScalar(const QJsonValue &value);

#endif // YAML_UTILS_HPP
