// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "YamlUtils.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <iostream>

#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/yaml.h>

namespace {
QString normalizeLegacyYamlUnits(const QString &value) {
    QString normalizedValue = value;
    normalizedValue.replace(QStringLiteral("Â°C"), QStringLiteral("\u00b0C"));
    normalizedValue.replace(QStringLiteral("Î©"), QStringLiteral("\u03a9"));
    return normalizedValue;
}

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

        return QJsonValue(normalizeLegacyYamlUnits(QString::fromStdString(scalar)));
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

QJsonValue mergeJsonPatchInternal(const QJsonValue &target, const QJsonValue &patch) {
    if (!patch.isObject()) {
        return patch;
    }

    QJsonObject result = target.isObject() ? target.toObject() : QJsonObject{};
    const QJsonObject patchObject = patch.toObject();
    for (auto it = patchObject.constBegin(); it != patchObject.constEnd(); ++it) {
        if (it.value().isNull()) {
            result.remove(it.key());
        } else {
            result.insert(it.key(), mergeJsonPatchInternal(result.value(it.key()), it.value()));
        }
    }
    return result;
}
} // namespace

QJsonValue applyJsonMergePatch(const QJsonValue &target, const QJsonValue &patch) {
    return mergeJsonPatchInternal(target, patch);
}

YamlLoadResult loadYamlFile(const QString &path) {
    QJsonValue jsonValue;

    try {
        std::cout << "Checking file " << path.toStdString() << std::endl;
        jsonValue = yamlNodeToJsonValue(YAML::LoadFile(path.toStdString()));
    } catch (const YAML::BadFile &error) {
        const QFileInfo fileInfo(path);
        qWarning() << "Unable to open YAML file" << path
                   << "exists=" << fileInfo.exists()
                   << "isSymlink=" << fileInfo.isSymLink()
                   << "symlinkTarget=" << fileInfo.symLinkTarget()
                   << "error=" << error.what();
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

YamlLoadResult loadEffectiveEverestConfig() {
    const QString configPath = readBackendConfigValue(QLatin1String(kConfEverestConfPath));
    if (configPath.isEmpty()) {
        return {.success = false, .yamlRoot = {}, .error = QStringLiteral("everest_config_path_missing")};
    }

    YamlLoadResult baseResult = loadYamlFile(configPath);
    if (!baseResult.success) {
        return baseResult;
    }

    const QString canonicalConfigPath = QFileInfo(configPath).canonicalFilePath();
    if (canonicalConfigPath.isEmpty()) {
        return {.success = false, .yamlRoot = {}, .error = QStringLiteral("everest_config_path_invalid")};
    }
    const QFileInfo canonicalConfigInfo(canonicalConfigPath);
    const QString overlayPath = canonicalConfigInfo.dir().filePath(
        QStringLiteral("user-config/") + canonicalConfigInfo.fileName());
    if (!QFileInfo::exists(overlayPath)) {
        return baseResult;
    }

    const YamlLoadResult overlayResult = loadYamlFile(overlayPath);
    if (!overlayResult.success) {
        return overlayResult;
    }
    baseResult.yamlRoot = applyJsonMergePatch(baseResult.yamlRoot, overlayResult.yamlRoot).toObject();
    return baseResult;
}

QString formatYamlScalar(const QJsonValue &value) {
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }

    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("null");
    }

    const QString stringValue = value.toString();
    QString escapedValue = stringValue;
    escapedValue.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escapedValue.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escapedValue.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return QLatin1Char('"') + escapedValue + QLatin1Char('"');
}
