// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export const ERR = Object.freeze({
  BACKEND_NOT_CONNECTED: 'backend not connected',
  NO_FILE_SELECTED: 'no file selected',
  MISSING_DATAB64: 'missing dataB64',
  UNKNOWN_ERROR: 'unknown_error'
});

export const UI = Object.freeze({
  NO_FILE_SELECTED: 'No file selected'
});

export function actionRef(moduleId, actionId) {
  return `${moduleId}.${actionId}`;
}

export function requestSent(moduleId, actionId) {
  return `${actionRef(moduleId, actionId)} sent`;
}

export function requestRejected(moduleId, actionId, reason = ERR.BACKEND_NOT_CONNECTED) {
  return `${actionRef(moduleId, actionId)} rejected: ${reason}`;
}

export function requestFailed(moduleId, actionId, err) {
  const detail = err instanceof Error ? err.message : String(err);
  return `${actionRef(moduleId, actionId)} failed: ${detail}`;
}

export function backendError(moduleId, actionId, errorCode) {
  return `${moduleId} backend error (${actionId}): ${errorCode || ERR.UNKNOWN_ERROR}`;
}

export function actionAcknowledged(moduleId, actionId) {
  return `${actionRef(moduleId, actionId)} acknowledged`;
}

export function resultApplied(moduleId, actionId) {
  return `${actionRef(moduleId, actionId)} result applied`;
}

export function resultReceived(moduleId, actionId) {
  return `${actionRef(moduleId, actionId)} result received`;
}

export function localFileSelected(fileName) {
  return `config.yaml local file selected: ${fileName}`;
}
