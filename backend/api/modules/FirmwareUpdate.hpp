// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef FIRMWARE_UPDATE_HPP
#define FIRMWARE_UPDATE_HPP

#include "RequestResponseTypes.hpp"

#include <QByteArray>

class FirmwareUpdateRuntime;

enum class FirmwareUpdateAction {
    ReadVersion,
    UpdateImage,
    UploadImageStart,
    UploadImageChunk,
    UploadImageFinish,
    Unknown
};

struct FirmwareVersionReadResult {
    bool success = false;
    QString version;
    QString error;
};

struct FirmwareImageDirResult {
    bool success = false;
    QString path;
    QString error;
};

struct FirmwareImageWriteResult {
    bool success = false;
    QString path;
    QString error;
};

struct FirmwareImagePayloadResult {
    bool success = false;
    QString fileName;
    QByteArray imageData;
    QString error;
};

struct FirmwareImageCleanupResult {
    bool success = false;
    QString error;
};

namespace FirmwareUpdate {
ModuleResponse handleRequest(const ModuleRequest &request);
ModuleResponse handleReadRequest(const ModuleRequest &request);
ModuleResponse handleUpdateRequest(const ModuleRequest &request);
ModuleResponse handleUploadStartRequest(const ModuleRequest &request);
ModuleResponse handleUploadChunkRequest(const ModuleRequest &request);
ModuleResponse handleUploadFinishRequest(const ModuleRequest &request);
FirmwareVersionReadResult readFirmwareVersion();
FirmwareImageDirResult loadFirmwareImageDir(const QString &configKey);
QString loadBackendConfigValue(const QString &configKey);
FirmwareImagePayloadResult parseFirmwareImagePayload(const QJsonObject &requestParameters);
FirmwareImageWriteResult saveFirmwareImageToDisk(const QString &imageDirPath,
                                                 const QString &fileName,
                                                 const QByteArray &imageData);
FirmwareImageCleanupResult cleanOldFirmwareImages(const QString &keepFilePath = QString());
FirmwareUpdateRuntime &runtime();
}

#endif // FIRMWARE_UPDATE_HPP
