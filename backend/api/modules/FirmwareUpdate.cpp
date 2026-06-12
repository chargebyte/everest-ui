// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "FirmwareUpdate.hpp"
#include "FirmwareUpdateRuntime.hpp"

#include "BackendConfig.hpp"
#include "ProtocolSchema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

namespace {
constexpr char kRaucStatusTemplate[] = "rauc status --output-format=json --detailed";
constexpr char kStateBooted[] = "booted";
constexpr char kFirmwareImageDirConfigKey[] = "firmware_image_dir";
constexpr char kFirmwareReadFailed[] = "firmware_read_failed";
constexpr char kFirmwareReadParseFailed[] = "firmware_read_parse_failed";
constexpr char kFirmwareVersionNotFound[] = "firmware_version_not_found";
constexpr char kFirmwareImageDirMissing[] = "firmware_image_dir_missing";
constexpr char kFirmwareImageDirNotFound[] = "firmware_image_dir_not_found";
constexpr char kFirmwareImageDirCreateFailed[] = "firmware_image_dir_create_failed";
constexpr char kFirmwareImageDirNotADirectory[] = "firmware_image_dir_not_a_directory";
constexpr char kFirmwareImageDirNotWritable[] = "firmware_image_dir_not_writable";
constexpr char kFirmwareImageInvalidParams[] = "invalid_params";
constexpr char kFirmwareImageBase64Invalid[] = "invalid_image";
constexpr char kFirmwareImageWriteFailed[] = "firmware_image_write_failed";
constexpr char kFirmwareImageCleanupFailed[] = "firmware_image_cleanup_failed";
} // namespace
namespace {
FirmwareUpdateAction toFirmwareUpdateAction(const QString &action) {
    if (action == QLatin1String(kActionReadVersion)) {
        return FirmwareUpdateAction::ReadVersion;
    }

    if (action == QLatin1String(kActionUpdateImage)) {
        return FirmwareUpdateAction::UpdateImage;
    }

    if (action == QLatin1String(kActionReboot)) {
        return FirmwareUpdateAction::Reboot;
    }

    if (action == QLatin1String(kActionUploadImageStart)) {
        return FirmwareUpdateAction::UploadImageStart;
    }

    if (action == QLatin1String(kActionUploadImageChunk)) {
        return FirmwareUpdateAction::UploadImageChunk;
    }

    if (action == QLatin1String(kActionUploadImageFinish)) {
        return FirmwareUpdateAction::UploadImageFinish;
    }

    return FirmwareUpdateAction::Unknown;
}
} // namespace

namespace FirmwareUpdate {
FirmwareUpdateRuntime &runtime() {
    static FirmwareUpdateRuntime instance;
    return instance;
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

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QStringLiteral("firmware"),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
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
    return runtime().handleUpdateRequest(request);
}

ModuleResponse handleRebootRequest(const ModuleRequest &request) {
    return runtime().handleRebootRequest(request);
}

ModuleResponse handleUploadStartRequest(const ModuleRequest &request) {
    return runtime().handleUploadStartRequest(request);
}

ModuleResponse handleUploadChunkRequest(const ModuleRequest &request) {
    return runtime().handleUploadChunkRequest(request);
}

ModuleResponse handleUploadFinishRequest(const ModuleRequest &request) {
    return runtime().handleUploadFinishRequest(request);
}

ModuleResponse handleRequest(const ModuleRequest &request) {
    switch (toFirmwareUpdateAction(request.action)) {
    case FirmwareUpdateAction::ReadVersion:
        return handleReadRequest(request);
    case FirmwareUpdateAction::UpdateImage:
        return handleUpdateRequest(request);
    case FirmwareUpdateAction::Reboot:
        return handleRebootRequest(request);
    case FirmwareUpdateAction::UploadImageStart:
        return handleUploadStartRequest(request);
    case FirmwareUpdateAction::UploadImageChunk:
        return handleUploadChunkRequest(request);
    case FirmwareUpdateAction::UploadImageFinish:
        return handleUploadFinishRequest(request);
    case FirmwareUpdateAction::Unknown:
        throw std::runtime_error("FirmwareUpdate::handleRequest got unsupported action");
    }

    throw std::runtime_error("FirmwareUpdate::handleRequest reached unreachable code");
}

FirmwareImageDirResult loadFirmwareImageDir(const QString &configKey) {
    const QString value = loadBackendConfigValue(configKey);
    if (value.isEmpty()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kFirmwareImageDirMissing),
        };
    }

    const QString cleanPath = QDir::cleanPath(value);
    QFileInfo pathInfo(cleanPath);
    if (!pathInfo.exists()) {
        QDir imageDir;
        if (!imageDir.mkpath(cleanPath)) {
            return FirmwareImageDirResult{
                .success = false,
                .path = QString(),
                .error = QString::fromLatin1(kFirmwareImageDirCreateFailed),
            };
        }

        pathInfo = QFileInfo(cleanPath);
    }

    if (!pathInfo.exists()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kFirmwareImageDirNotFound),
        };
    }

    if (!pathInfo.isDir()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kFirmwareImageDirNotADirectory),
        };
    }

    if (!pathInfo.isWritable()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kFirmwareImageDirNotWritable),
        };
    }

    return FirmwareImageDirResult{
        .success = true,
        .path = cleanPath,
        .error = QString(),
    };
}

QString loadBackendConfigValue(const QString &configKey) {
    return ::readBackendConfigValue(configKey);
}

FirmwareImagePayloadResult parseFirmwareImagePayload(const QJsonObject &requestParameters) {
    const QJsonObject imageObject = requestParameters.value(QStringLiteral("image")).toObject();
    const QString rawFileName = imageObject.value(QStringLiteral("file_name")).toString().trimmed();
    const QString dataB64 = imageObject.value(QStringLiteral("dataB64")).toString().trimmed();

    const QString fileName = QFileInfo(rawFileName).fileName();
    if (fileName.isEmpty() || dataB64.isEmpty()) {
        return FirmwareImagePayloadResult{
            .success = false,
            .fileName = QString(),
            .imageData = QByteArray(),
            .error = QString::fromLatin1(kFirmwareImageInvalidParams),
        };
    }

    const QByteArray imageData = QByteArray::fromBase64(dataB64.toLatin1());
    if (imageData.isEmpty()) {
        return FirmwareImagePayloadResult{
            .success = false,
            .fileName = QString(),
            .imageData = QByteArray(),
            .error = QString::fromLatin1(kFirmwareImageBase64Invalid),
        };
    }

    return FirmwareImagePayloadResult{
        .success = true,
        .fileName = fileName,
        .imageData = imageData,
        .error = QString(),
    };
}

FirmwareImageWriteResult saveFirmwareImageToDisk(const QString &imageDirPath,
                                                 const QString &fileName,
                                                 const QByteArray &imageData) {
    const QString targetPath = QDir(imageDirPath).filePath(fileName);
    QFile imageFile(targetPath);
    if (!imageFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return FirmwareImageWriteResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kFirmwareImageWriteFailed),
        };
    }

    if (imageFile.write(imageData) < 0) {
        imageFile.close();
        return FirmwareImageWriteResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kFirmwareImageWriteFailed),
        };
    }

    imageFile.close();

    return FirmwareImageWriteResult{
        .success = true,
        .path = targetPath,
        .error = QString(),
    };
}

FirmwareImageCleanupResult cleanOldFirmwareImages(const QString &keepFilePath) {
    const FirmwareImageDirResult imageDirResult =
        loadFirmwareImageDir(QString::fromLatin1(kFirmwareImageDirConfigKey));
    if (!imageDirResult.success) {
        return FirmwareImageCleanupResult{
            .success = false,
            .error = imageDirResult.error,
        };
    }

    const QDir imageDir(imageDirResult.path);
    const QString keepPath = keepFilePath.isEmpty() ? QString() : QDir::cleanPath(keepFilePath);
    const QFileInfoList fileInfos = imageDir.entryInfoList(
        QDir::Files | QDir::NoSymLinks | QDir::Readable | QDir::Writable,
        QDir::Time | QDir::Reversed);

    for (const QFileInfo &fileInfo : fileInfos) {
        const QString candidatePath = QDir::cleanPath(fileInfo.absoluteFilePath());
        if (!keepPath.isEmpty() && candidatePath == keepPath) {
            continue;
        }

        if (!QFile::remove(candidatePath)) {
            return FirmwareImageCleanupResult{
                .success = false,
                .error = QString::fromLatin1(kFirmwareImageCleanupFailed),
            };
        }
    }

    return FirmwareImageCleanupResult{
        .success = true,
        .error = QString(),
    };
}
} // namespace FirmwareUpdate
