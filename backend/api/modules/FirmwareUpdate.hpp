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
    Reboot,
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

struct FirmwareImageSpaceResult {
    bool success = false;
    qint64 availableBytes = 0;
    qint64 requiredBytes = 0;
    QString error;
};

namespace FirmwareUpdate {
    ModuleResponse handleRequest(const ModuleRequest &request);
    FirmwareImageDirResult loadFirmwareImageDir(const QString &configKey);
    QString loadBackendConfigValue(const QString &configKey);
    FirmwareImageCleanupResult cleanOldFirmwareImages(const QString &keepFilePath = QString());
    FirmwareImageSpaceResult checkFirmwareImageSpace(const QString &imageDirPath, qint64 requiredBytes);
    FirmwareUpdateRuntime &runtime();
}

#endif // FIRMWARE_UPDATE_HPP
