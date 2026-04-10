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
inline constexpr const char kKeyOk[] = "ok";
inline constexpr const char kKeyError[] = "error";
inline constexpr const char kKeyType[] = "type";
inline constexpr const char kKeyResponseId[] = "responseId";
inline constexpr const char kKeyDataB64[] = "dataB64";
inline constexpr const char kKeyFile[] = "file";
inline constexpr const char kKeyProgress[] = "progress";
inline constexpr const char kKeyStage[] = "stage";
inline constexpr const char kKeyResult[] = "result";
inline constexpr const char kKeyRestartRequired[] = "restart_required";
inline constexpr const char kKeyApplied[] = "applied";

// Common group/action values
inline constexpr const char kGroupPcap[] = "pcap";
inline constexpr const char kGroupEverest[] = "everest";
inline constexpr const char kGroupSafety[] = "safety";
inline constexpr const char kGroupOcpp[] = "ocpp";
inline constexpr const char kGroupFirmware[] = "firmware";
inline constexpr const char kGroupLogs[] = "logs";

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
inline constexpr const char kActionDownload[] = "download";

// Parameter keys
inline constexpr const char kKeyGeneral[] = "general";
inline constexpr const char kKeyInterface[] = "interface";
inline constexpr const char kKeyConfigYaml[] = "config_yaml";

// EVerest keys
inline constexpr const char kKeyCsmsServerAddress[] = "csms_server_address";
inline constexpr const char kKeyUserId[] = "user_id";
inline constexpr const char kKeySecurityProfile[] = "security_profile";
inline constexpr const char kKeyEnergyConsumptionPoint[] = "energy_consumption_point";
inline constexpr const char kKeyEvse15118D20[] = "Evse15118D20";
inline constexpr const char kKeySupportedScheduledMode[] = "supported_scheduled_mode";
inline constexpr const char kKeySupportedDynamicMode[] = "supported_dynamic_mode";
inline constexpr const char kKeyEvseManager[] = "EvseManager";
inline constexpr const char kKeyDisableAuthentication[] = "disable_authentication";
inline constexpr const char kKeyProfinet[] = "profinet";
inline constexpr const char kKeyChargerName[] = "charger_name";
inline constexpr const char kKeyUpdateTime[] = "update_time_s";
inline constexpr const char kKeyBidirectionalEnable[] = "bidirectional_enable";
inline constexpr const char kKeyProfinetTimeout[] = "timeout_ms";
inline constexpr const char kKeyDevice[] = "device";
inline constexpr const char kKeyDebugMode[] = "debug_mode";

// Safety keys
inline constexpr const char kKeyPt1000[] = "pt1000";
inline constexpr const char kKeyContactors[] = "contactors";
inline constexpr const char kKeyEstops[] = "estops";

// OCPP keys
inline constexpr const char kKeyInternalCtrlr[] = "InternalCtrlr";
inline constexpr const char kKeyEnabled[] = "enabled";

// Firmware keys
inline constexpr const char kKeyImage[] = "image";
inline constexpr const char kKeyFileName[] = "file_name";

// Template keys
inline constexpr const char kTemplatePcapRead[] = "pcap:read";
inline constexpr const char kTemplatePcapWrite[] = "pcap:write";
inline constexpr const char kTemplateEverestReadConfigParameters[] =
    "everest:read_config_parameters";
inline constexpr const char kTemplateEverestWriteConfigParameters[] =
    "everest:write_config_parameters";
inline constexpr const char kTemplateEverestDownloadConfig[] =
    "everest:download_config";
inline constexpr const char kTemplateEverestUploadConfig[] =
    "everest:upload_config";
inline constexpr const char kTemplateSafetyReadSettings[] = "safety:read_settings";
inline constexpr const char kTemplateSafetyWriteSettings[] = "safety:write_settings";
inline constexpr const char kTemplateOcppReadSettings[] = "ocpp:read_settings";
inline constexpr const char kTemplateOcppWriteSettings[] = "ocpp:write_settings";
inline constexpr const char kTemplateFirmwareReadVersion[] = "firmware:read_version";
inline constexpr const char kTemplateFirmwareUpdateImage[] = "firmware:update_image";
inline constexpr const char kTemplateLogsRead[] = "logs:read";
inline constexpr const char kTemplateLogsDownload[] = "logs:download";

// Response types
inline constexpr const char kTypeAck[] = "ack";
inline constexpr const char kTypePcapWriteAck[] = "pcap.write.ack";
inline constexpr const char kTypePcapReadResult[] = "pcap.read.result";
inline constexpr const char kTypeEverestReadConfigParametersResult[] =
    "everest.read_config_parameters.result";
inline constexpr const char kTypeEverestWriteConfigParametersAck[] =
    "everest.write_config_parameters.ack";
inline constexpr const char kTypeEverestDownloadConfigResult[] =
    "everest.download_config.result";
inline constexpr const char kTypeEverestUploadConfigAck[] =
    "everest.upload_config.ack";
inline constexpr const char kTypeSafetyReadSettingsResult[] =
    "safety.read_settings.result";
inline constexpr const char kTypeSafetyWriteSettingsAck[] =
    "safety.write_settings.ack";
inline constexpr const char kTypeOcppReadSettingsResult[] =
    "ocpp.read_settings.result";
inline constexpr const char kTypeOcppWriteSettingsAck[] =
    "ocpp.write_settings.ack";
inline constexpr const char kTypeFirmwareReadVersionResult[] =
    "firmware.read_version.result";
inline constexpr const char kTypeFirmwareUpdateImageAck[] =
    "firmware.update_image.ack";
inline constexpr const char kTypeFirmwareUpdateImageProgress[] =
    "firmware.update_image.progress";
inline constexpr const char kTypeFirmwareUpdateImageResult[] =
    "firmware.update_image.result";
inline constexpr const char kTypeLogsReadResult[] = "logs.read.result";
inline constexpr const char kTypeLogsDownloadResult[] = "logs.download.result";

// Error strings
inline constexpr const char kErrorInvalidParams[] = "invalid_params";
inline constexpr const char kErrorInvalidJson[] = "invalid_json";
inline constexpr const char kErrorNotRecording[] = "not_recording";
inline constexpr const char kErrorPcapBusy[] = "pcap_busy";
inline constexpr const char kErrorPcapStopFailed[] = "pcap_stop_failed";
inline constexpr const char kErrorFileIoFailed[] = "file_io_failed";
inline constexpr const char kErrorNotAvailable[] = "not_available";
inline constexpr const char kErrorBusy[] = "busy";
inline constexpr const char kErrorApplyFailed[] = "apply_failed";
inline constexpr const char kErrorInternalError[] = "internal_error";
inline constexpr const char kErrorInvalidImage[] = "invalid_image";
inline constexpr const char kErrorUpdateInProgress[] = "update_in_progress";
inline constexpr const char kErrorFlashFailed[] = "flash_failed";
inline constexpr const char kErrorFileNotFound[] = "file_not_found";
inline constexpr const char kErrorNoFilesSelected[] = "no_files_selected";
inline constexpr const char kErrorReadFailed[] = "read_failed";

#endif // PROTOCOL_SCHEMA_HPP
