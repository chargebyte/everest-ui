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
#include <QStorageInfo>

#include <stdexcept>

namespace {
constexpr char kRaucStatusTemplate[] = "rauc status --output-format=json --detailed";
constexpr char kRaucSlots[] = "slots";
constexpr char kRaucState[] = "state";
constexpr char kRaucSlotStatus[] = "slot_status";
constexpr char kRaucBundle[] = "bundle";
constexpr char kRaucVersion[] = "version";
constexpr char kStateBooted[] = "booted";
constexpr char kErrorFirmwareReadFailed[] = "firmware_read_failed";
constexpr char kErrorFirmwareReadParseFailed[] = "firmware_read_parse_failed";
constexpr char kErrorFirmwareVersionNotFound[] = "firmware_version_not_found";
constexpr char kErrorFirmwareImageDirMissing[] = "firmware_image_dir_missing";
constexpr char kErrorFirmwareImageDirNotFound[] = "firmware_image_dir_not_found";
constexpr char kErrorFirmwareImageDirCreateFailed[] = "firmware_image_dir_create_failed";
constexpr char kErrorFirmwareImageDirNotADirectory[] = "firmware_image_dir_not_a_directory";
constexpr char kErrorFirmwareImageDirNotWritable[] = "firmware_image_dir_not_writable";
constexpr char kErrorFirmwareImageBase64Invalid[] = "invalid_image";
constexpr char kErrorFirmwareImageWriteFailed[] = "firmware_image_write_failed";
constexpr char kErrorFirmwareImageCleanupFailed[] = "firmware_image_cleanup_failed";
constexpr char kErrorFirmwareImageDirInsufficientSpace[] = "firmware_image_dir_insufficient_space";
constexpr char kParametersVersion[] = "version";

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
            .error = QString::fromLatin1(kErrorFirmwareReadFailed),
        };
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.stdoutData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return FirmwareVersionReadResult{
            .success = false,
            .version = QString(),
            .error = QString::fromLatin1(kErrorFirmwareReadParseFailed),
        };
    }

    const QJsonArray slotArray = document.object().value(QLatin1String(kRaucSlots)).toArray();
    for (const QJsonValue &slotEntryValue : slotArray) {
        const QJsonObject slotEntryObject = slotEntryValue.toObject();
        const QStringList slotNames = slotEntryObject.keys();

        if (slotNames.size() != 1) {
            continue;
        }

        const QJsonObject slotObject = slotEntryObject.value(slotNames.first()).toObject();
        if (slotObject.value(QLatin1String(kRaucState)).toString() != QLatin1String(kStateBooted)) {
            continue;
        }

        const QString version = slotObject
                                    .value(QLatin1String(kRaucSlotStatus))
                                    .toObject()
                                    .value(QLatin1String(kRaucBundle))
                                    .toObject()
                                    .value(QLatin1String(kRaucVersion))
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
        .error = QString::fromLatin1(kErrorFirmwareVersionNotFound),
    };
}

ModuleResponse handleReadRequest(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupFirmware),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    const FirmwareVersionReadResult versionResult = readFirmwareVersion();
    if (!versionResult.success) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), versionResult.error},
        };
        return response;
    }

    response.parameters = QJsonObject{
        {QLatin1String(kParametersImage),
         QJsonObject{
             {QLatin1String(kParametersVersion), versionResult.version},
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
            .error = QString::fromLatin1(kErrorFirmwareImageDirMissing),
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
                .error = QString::fromLatin1(kErrorFirmwareImageDirCreateFailed),
            };
        }

        pathInfo = QFileInfo(cleanPath);
    }

    if (!pathInfo.exists()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kErrorFirmwareImageDirNotFound),
        };
    }

    if (!pathInfo.isDir()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kErrorFirmwareImageDirNotADirectory),
        };
    }

    if (!pathInfo.isWritable()) {
        return FirmwareImageDirResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kErrorFirmwareImageDirNotWritable),
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
    const QJsonObject imageObject = requestParameters.value(QLatin1String(kParametersImage)).toObject();
    const QString rawFileName = imageObject.value(QLatin1String(kParametersFileName)).toString().trimmed();
    const QString dataB64 = imageObject.value(QLatin1String(kKeyDataB64)).toString().trimmed();

    const QString fileName = QFileInfo(rawFileName).fileName();
    if (fileName.isEmpty() || dataB64.isEmpty()) {
        return FirmwareImagePayloadResult{
            .success = false,
            .fileName = QString(),
            .imageData = QByteArray(),
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    const QByteArray imageData = QByteArray::fromBase64(dataB64.toLatin1());
    if (imageData.isEmpty()) {
        return FirmwareImagePayloadResult{
            .success = false,
            .fileName = QString(),
            .imageData = QByteArray(),
            .error = QString::fromLatin1(kErrorFirmwareImageBase64Invalid),
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
            .error = QString::fromLatin1(kErrorFirmwareImageWriteFailed),
        };
    }

    if (imageFile.write(imageData) < 0) {
        imageFile.close();
        return FirmwareImageWriteResult{
            .success = false,
            .path = QString(),
            .error = QString::fromLatin1(kErrorFirmwareImageWriteFailed),
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
        loadFirmwareImageDir(QString::fromLatin1(kConfFirmwareImageDir));
    if (!imageDirResult.success) {
        return FirmwareImageCleanupResult{
            .success = false,
            .error = imageDirResult.error,
        };
    }

    const QDir imageDir(imageDirResult.path);
    const QString keepPath = keepFilePath.isEmpty() ? QString() : QDir::cleanPath(keepFilePath);
    const QFileInfoList fileInfos = imageDir.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::NoSymLinks,
        QDir::Time | QDir::Reversed);

    for (const QFileInfo &fileInfo : fileInfos) {
        const QString candidatePath = QDir::cleanPath(fileInfo.absoluteFilePath());
        if (!keepPath.isEmpty() && candidatePath == keepPath) {
            continue;
        }

        if (!QFile::remove(candidatePath)) {
            return FirmwareImageCleanupResult{
                .success = false,
                .error = QString::fromLatin1(kErrorFirmwareImageCleanupFailed),
            };
        }
    }

    return FirmwareImageCleanupResult{
        .success = true,
        .error = QString(),
    };
}

FirmwareImageSpaceResult checkFirmwareImageSpace(const QString &imageDirPath, qint64 requiredBytes) {
    if (requiredBytes <= 0) {
        return FirmwareImageSpaceResult{
            .success = false,
            .availableBytes = 0,
            .requiredBytes = requiredBytes,
            .error = QString::fromLatin1(kErrorInvalidParams),
        };
    }

    const QStorageInfo storageInfo(imageDirPath);
    if (!storageInfo.isValid() || !storageInfo.isReady()) {
        return FirmwareImageSpaceResult{
            .success = false,
            .availableBytes = 0,
            .requiredBytes = requiredBytes,
            .error = QString::fromLatin1(kErrorFirmwareImageDirInsufficientSpace),
        };
    }

    const qint64 availableBytes = storageInfo.bytesAvailable();
    if (availableBytes < requiredBytes) {
        return FirmwareImageSpaceResult{
            .success = false,
            .availableBytes = availableBytes,
            .requiredBytes = requiredBytes,
            .error = QString::fromLatin1(kErrorFirmwareImageDirInsufficientSpace),
        };
    }

    return FirmwareImageSpaceResult{
        .success = true,
        .availableBytes = availableBytes,
        .requiredBytes = requiredBytes,
        .error = QString(),
    };
}
} // namespace FirmwareUpdate
