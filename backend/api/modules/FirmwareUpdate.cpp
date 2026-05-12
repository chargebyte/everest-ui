// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "FirmwareUpdate.hpp"

#include "ConsoleConnector.hpp"
#include "ProtocolSchema.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

namespace {
constexpr char kRaucStatusTemplate[] = "rauc status --output-format=json --detailed";
constexpr char kStateBooted[] = "booted";
constexpr char kFirmwareReadFailed[] = "firmware_read_failed";
constexpr char kFirmwareReadParseFailed[] = "firmware_read_parse_failed";
constexpr char kFirmwareVersionNotFound[] = "firmware_version_not_found";

FirmwareUpdateAction toFirmwareUpdateAction(const QString &action) {
    if (action == QLatin1String(kActionReadVersion)) {
        return FirmwareUpdateAction::ReadVersion;
    }

    if (action == QLatin1String(kActionUpdateImage)) {
        return FirmwareUpdateAction::UpdateImage;
    }

    return FirmwareUpdateAction::Unknown;
}
} // namespace

namespace FirmwareUpdate {
ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toFirmwareUpdateAction(request.action)) {
    case FirmwareUpdateAction::ReadVersion:
        return handleReadRequest(request);
    case FirmwareUpdateAction::UpdateImage:
        return handleUpdateRequest(request);
    case FirmwareUpdateAction::Unknown:
        throw std::runtime_error("FirmwareUpdate::handleRequest got unsupported action");
    }

    throw std::runtime_error("FirmwareUpdate::handleRequest reached unreachable code");
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
    };

    const FirmwareVersionReadResult versionResult = readFirmwareVersion();
    if (!versionResult.success) {
        response.parameters = QJsonObject{
            {QStringLiteral("error"), versionResult.error},
        };
        return response;
    }

    response.parameters = QJsonObject{
        {QStringLiteral("image"),
         QJsonObject{
             {QStringLiteral("version"), versionResult.version},
         }},
    };
    response.success = true;
    return response;
}

ModuleResponse handleUpdateRequest(const ModuleRequest &request) {
    return ModuleResponse{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = request.parameters,
        .success = false,
    };
}

FirmwareVersionReadResult readFirmwareVersion() {
    ConsoleConnector console;
    ConsoleConnector::ExecOptions options;
    const ConsoleConnector::RunResult result = console.executeTemplate(
        QString::fromLatin1(kRaucStatusTemplate),
        {},
        options,
        ConsoleConnector::ExecMode::Sync);

    if (result.exitCode != 0) {
        return FirmwareVersionReadResult{
            .success = false,
            .version = QString(),
            .error = QString::fromLatin1(kFirmwareReadFailed),
        };
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.stdoutData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return FirmwareVersionReadResult{
            .success = false,
            .version = QString(),
            .error = QString::fromLatin1(kFirmwareReadParseFailed),
        };
    }

    const QJsonArray slotArray = document.object().value(QStringLiteral("slots")).toArray();
    for (const QJsonValue &slotEntryValue : slotArray) {
        const QJsonObject slotEntryObject = slotEntryValue.toObject();
        const QStringList slotNames = slotEntryObject.keys();

        if (slotNames.size() != 1) {
            continue;
        }

        const QJsonObject slotObject = slotEntryObject.value(slotNames.first()).toObject();
        if (slotObject.value(QStringLiteral("state")).toString() != QLatin1String(kStateBooted)) {
            continue;
        }

        const QString version = slotObject
                                    .value(QStringLiteral("slot_status"))
                                    .toObject()
                                    .value(QStringLiteral("bundle"))
                                    .toObject()
                                    .value(QStringLiteral("version"))
                                    .toString()
                                    .trimmed();
        if (version.isEmpty()) {
            break;
        }

        return FirmwareVersionReadResult{
            .success = true,
            .version = version,
            .error = QString(),
        };
    }

    return FirmwareVersionReadResult{
        .success = false,
        .version = QString(),
        .error = QString::fromLatin1(kFirmwareVersionNotFound),
    };
}
} // namespace FirmwareUpdate
