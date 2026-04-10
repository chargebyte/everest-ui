// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export const MODULE_IDS = Object.freeze({
  EVEREST: 'everest',
  SAFETY: 'safety',
  OCPP: 'ocpp',
  PCAP: 'pcap',
  FIRMWARE: 'firmware',
  LOGS: 'logs'
});

export const EVEREST_ACTIONS = Object.freeze({
  READ_CONFIG_PARAMETERS: 'read_config_parameters',
  WRITE_CONFIG_PARAMETERS: 'write_config_parameters',
  DOWNLOAD_CONFIG: 'download_config',
  UPLOAD_CONFIG: 'upload_config'
});

export const RESPONSE_KINDS = Object.freeze({
  RESULT: 'result',
  ACK: 'ack',
  ERROR: 'error',
  PROGRESS: 'progress'
});
