// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef PROTOCOL_SCHEMA_HPP
#define PROTOCOL_SCHEMA_HPP

#include <QLatin1String>
#include <cstddef>

enum class FieldName {
    RequestId,
    Group,
    Action,
    Parameters,
};

inline constexpr const char *kFieldNameKeys[] = {
    "requestId",
    "group",
    "action",
    "parameters",
};

inline QLatin1String FieldNameKey(FieldName field) {
    return QLatin1String(kFieldNameKeys[static_cast<size_t>(field)]);
}

// Common JSON keys
inline constexpr const char kKeyType[] = "type";
inline constexpr const char kKeyOk[] = "ok";
inline constexpr const char kKeyFinal[] = "final";
inline constexpr const char kKeyRequestId[] = "requestId";
inline constexpr const char kKeyResponseId[] = "responseId";
inline constexpr const char kKeyParameters[] = "parameters";
inline constexpr const char kKeyDataB64[] = "dataB64";
inline constexpr const char kKeyFile[] = "file";

// Common group/action values
inline constexpr const char kGroupPcap[] = "pcap";
inline constexpr const char kGroupEverest[] = "everest";
inline constexpr const char kGroupSafety[] = "safety";
inline constexpr const char kGroupOcpp[] = "ocpp";
inline constexpr const char kGroupFirmware[] = "firmware";
inline constexpr const char kGroupSystemLogs[] = "system_logs";
inline constexpr const char kGroupSystem[] = "system";

inline constexpr const char kActionRead[] = "read";
inline constexpr const char kActionWrite[] = "write";
inline constexpr const char kActionReadConfigParameters[] = "read_config_parameters";
inline constexpr const char kActionWriteConfigParameters[] = "write_config_parameters";
inline constexpr const char kActionDownloadConfig[] = "download_config";
inline constexpr const char kActionUploadConfig[] = "upload_config";
inline constexpr const char kActionReadSettings[] = "read_settings";
inline constexpr const char kActionWriteSettings[] = "write_settings";
inline constexpr const char kActionReadVersion[] = "read_version";
inline constexpr const char kActionUpdateImage[] = "update_image";
inline constexpr const char kActionReboot[] = "reboot";
inline constexpr const char kActionUploadImage[] = "upload_image";
inline constexpr const char kActionUploadImageStart[] = "upload_image.start";
inline constexpr const char kActionUploadImageChunk[] = "upload_image.chunk";
inline constexpr const char kActionUploadImageFinish[] = "upload_image.finish";
inline constexpr const char kActionDownload[] = "download";
inline constexpr const char kActionExtract[] = "extract";
inline constexpr const char kActionProgress[] = "progress";
inline constexpr const char kActionReadAppTitle[] = "read_app_title";

// Parameter keys
inline constexpr const char kKeyGeneral[] = "general";
inline constexpr const char kKeyInterface[] = "interface";

// Response types
inline constexpr const char kTypeAck[] = "ack";
inline constexpr const char kTypeResult[] = "result";

// Error strings
inline constexpr const char kError[] = "error";
inline constexpr const char kErrorMissing[] = "_missing";
inline constexpr const char kErrorInvalidParams[] = "invalid_params";
inline constexpr const char kErrorEverestStateNotAllowed[] = "everest_state_not_allowed";
inline constexpr const char kErrorNotRecording[] = "not_recording";
inline constexpr const char kErrorPcapBusy[] = "pcap_busy";
inline constexpr const char kErrorPcapStopFailed[] = "pcap_stop_failed";
inline constexpr const char kErrorFileIoFailed[] = "file_io_failed";
inline constexpr const char kErrorFlashFailed[] = "flash_failed";
inline constexpr const char kErrorFileNotFound[] = "file_not_found";
inline constexpr const char kErrorNoFilesSelected[] = "no_files_selected";
inline constexpr const char kErrorReadFailed[] = "read_failed";

// Parameter Strings
inline constexpr const char kParametersSizeBytes[] = "size_bytes";
inline constexpr const char kParametersFileName[] = "file_name";
inline constexpr const char kParametersImage[] = "image";

// EVerest Config Keys
inline constexpr const char kEverestConfActiveModules[] = "active_modules";
inline constexpr const char kEverestConfModule[] = "module";
inline constexpr const char kEverestConfConfigModule[] = "config_module";

// Backend Config Values
inline constexpr const char kConfEverestConfPath[] = "everest_config_path";
inline constexpr const char kConfFirmwareImageDir[] = "firmware_image_dir";

// Internal Info
inline constexpr const char kInfoEverestErrorPresentNotDetected[] = "everest_error_present_not_detected";

#endif // PROTOCOL_SCHEMA_HPP
