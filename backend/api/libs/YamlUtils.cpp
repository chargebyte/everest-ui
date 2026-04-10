// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "YamlUtils.hpp"

#include <QJsonArray>
#include <QJsonValue>

#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/yaml.h>

namespace {
QJsonValue yamlNodeToJsonValue(const YAML::Node &node) {
    if (!node || node.IsNull()) {
        return QJsonValue();
    }

    if (node.IsScalar()) {
        const std::string scalar = node.as<std::string>();
        if (scalar == "true") {
            return QJsonValue(true);
        }
        if (scalar == "false") {
            return QJsonValue(false);
        }

        bool isInteger = false;
        const qlonglong integerValue = QString::fromStdString(scalar).toLongLong(&isInteger);
        if (isInteger) {
            return QJsonValue(static_cast<qint64>(integerValue));
        }

        bool isDouble = false;
        const double doubleValue = QString::fromStdString(scalar).toDouble(&isDouble);
        if (isDouble) {
            return QJsonValue(doubleValue);
        }

        return QJsonValue(QString::fromStdString(scalar));
    }

    if (node.IsSequence()) {
        QJsonArray array;
        for (const YAML::Node &child : node) {
            array.append(yamlNodeToJsonValue(child));
        }
        return array;
    }

    if (node.IsMap()) {
        QJsonObject object;
        for (const auto &entry : node) {
            object.insert(QString::fromStdString(entry.first.as<std::string>()),
                          yamlNodeToJsonValue(entry.second));
        }
        return object;
    }

    return QJsonValue();
}
} // namespace

YamlLoadResult loadYamlFile(const QString &path) {
    QJsonValue jsonValue;

    try {
        jsonValue = yamlNodeToJsonValue(YAML::LoadFile(path.toStdString()));
    } catch (const YAML::BadFile &) {
        return YamlLoadResult{
            .success = false,
            .yamlRoot = QJsonObject{},
            .error = QStringLiteral("everest_config_open_failed"),
        };
    } catch (const YAML::ParserException &) {
        return YamlLoadResult{
            .success = false,
            .yamlRoot = QJsonObject{},
            .error = QStringLiteral("everest_config_parse_failed"),
        };
    } catch (const std::exception &) {
        return YamlLoadResult{
            .success = false,
            .yamlRoot = QJsonObject{},
            .error = QStringLiteral("everest_config_load_failed"),
        };
    }

    if (!jsonValue.isObject()) {
        return YamlLoadResult{
            .success = false,
            .yamlRoot = QJsonObject{},
            .error = QStringLiteral("everest_config_root_not_map"),
        };
    }

    return YamlLoadResult{
        .success = true,
        .yamlRoot = jsonValue.toObject(),
        .error = QString(),
    };
}
