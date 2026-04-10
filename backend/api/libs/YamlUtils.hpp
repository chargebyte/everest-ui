// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef YAML_UTILS_HPP
#define YAML_UTILS_HPP

#include <QJsonObject>
#include <QString>

struct YamlLoadResult {
    bool success = false;
    QJsonObject yamlRoot;
    QString error;
};

YamlLoadResult loadYamlFile(const QString &path);

#endif // YAML_UTILS_HPP
