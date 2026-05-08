// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { renderUpdateBlock } from '../ui/update.js';

export function renderFirmwarePage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  const pageConfig = loadPageConfig(MODULE_IDS.FIRMWARE, parameterCatalog);
  const updaterBlock = pageConfig.blocks.find((block) => block.kind === 'updater');

  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const firmwareState = {
    connected: false,
    rebootRequired: false
  };

  const updateBlock = renderUpdateBlock(updaterBlock, {
    namePrefix: 'firmware',
    title: pageConfig.title,
    buttonLabel: 'Start Update',
    rebootButtonLabel: 'Reboot',
    onFileError(errorMessage) {
      addLog(`firmware file upload error: ${errorMessage}`);
    }
  });

  updateBlock.bindUpdate(() => {
    if (firmwareState.connected !== true) {
      addLog('firmware.update_image rejected: websocket not connected');
      return;
    }

    const values = updateBlock.getValues(updateBlock.requestResponseObject);
    if (!hasSelectedImage(values)) {
      addLog('firmware.update_image rejected: no firmware image selected');
      return;
    }

    updateBlock.setProgress('Awaiting acknowledgement...');

    const updateImageRequest = buildRequest(
      pageConfig.actions.update_image.group,
      pageConfig.actions.update_image.action,
      values
    );

    const ok = sendFirmwareRequest(
      sendPayload,
      addLog,
      updateImageRequest,
      pageConfig.actions.update_image.group,
      pageConfig.actions.update_image.action
    );

    if (!ok) {
      updateBlock.setProgress(null);
    }
  });

  updateBlock.bindReboot(() => {
    const rebootAction = pageConfig.actions.reboot;

    if (!rebootAction) {
      addLog('firmware.reboot not configured in parameter catalog');
      return;
    }

    if (firmwareState.connected !== true) {
      addLog('firmware.reboot rejected: websocket not connected');
      return;
    }

    const rebootRequest = buildRequest(
      rebootAction.group,
      rebootAction.action,
      {}
    );

    sendFirmwareRequest(
      sendPayload,
      addLog,
      rebootRequest,
      rebootAction.group,
      rebootAction.action
    );
  });

  pageElement.appendChild(updateBlock.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'firmware.read_version.result' && message.ok === true) {
        addLog('firmware.read_version.result received');
        updateBlock.setValues(buildUpdaterValues(
          updateBlock,
          extractFirmwareVersion(message.parameters)
        ));
        return;
      }

      if (message.type === 'firmware.update_image.ack' && message.ok === true) {
        addLog('firmware.update_image.ack received');
        firmwareState.rebootRequired = false;
        updateBlock.setRebootRequired(false);
        updateBlock.setProgress('Update started');
        return;
      }

      if (message.type === 'firmware.update_image.progress') {
        addLog('firmware.update_image.progress received');
        updateBlock.setProgress({
          progress: message.parameters?.progress,
          stage: message.parameters?.stage
        });
        return;
      }

      if (message.type === 'firmware.update_image.result' && message.ok === true) {
        addLog('firmware.update_image.result received');

        const version = extractFirmwareVersion(message.parameters);
        if (version !== '') {
          updateBlock.setValues(buildUpdaterValues(updateBlock, version));
        }

        firmwareState.rebootRequired = message.parameters?.restart_required === true;
        updateBlock.setSelectedFile(null);
        updateBlock.setRebootRequired(firmwareState.rebootRequired);

        if (!firmwareState.rebootRequired) {
          updateBlock.setProgress(null);
        }

        return;
      }

      if (message.type === 'firmware.reboot.ack' && message.ok === true) {
        addLog('firmware.reboot.ack received');
        updateBlock.setProgress('Reboot requested');
        return;
      }

      if (message.type === 'firmware.read_version.error') {
        const error = message.parameters?.error;
        addLog(`firmware.read_version.error: ${error}`);
        return;
      }

      if (message.type === 'firmware.update_image.error') {
        const error = message.parameters?.error;
        addLog(`firmware.update_image.error: ${error}`);
        firmwareState.rebootRequired = false;
        updateBlock.setRebootRequired(false);
        updateBlock.setProgress(null);
        return;
      }

      if (message.type === 'firmware.reboot.error') {
        const error = message.parameters?.error;
        addLog(`firmware.reboot.error: ${error}`);
        updateBlock.setRebootRequired(true);
        updateBlock.setProgress('Reboot failed');
        return;
      }

      if (message.ok === false) {
        const error = message.parameters?.error || message.error || 'unknown_error';
        addLog(`firmware backend error: ${error}`);
        firmwareState.rebootRequired = false;
        updateBlock.setRebootRequired(false);
        updateBlock.setProgress(null);
      }
    },
    onConnectionChange(connected) {
      firmwareState.connected = connected === true;

      if (firmwareState.connected === true) {
        const readVersionRequest = buildRequest(
          pageConfig.actions.read_version.group,
          pageConfig.actions.read_version.action,
          {}
        );
        sendFirmwareRequest(
          sendPayload,
          addLog,
          readVersionRequest,
          pageConfig.actions.read_version.group,
          pageConfig.actions.read_version.action
        );
      }
    },
    destroy() {}
  };
}

function sendFirmwareRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}

function buildUpdaterValues(updateBlock, version) {
  const values = updateBlock.getValues(updateBlock.requestResponseObject);
  const firmwareEntry = Object.values(values)[0];

  if (!firmwareEntry?.value || typeof firmwareEntry.value !== 'object') {
    firmwareEntry.value = {
      version: '',
      file_name: '',
      dataB64: ''
    };
  }

  firmwareEntry.value.version = version;
  return values;
}

function hasSelectedImage(values) {
  const firmwareEntry = Object.values(values || {})[0];
  const imageValue = firmwareEntry?.value;

  return typeof imageValue?.file_name === 'string' &&
    imageValue.file_name.trim() !== '' &&
    typeof imageValue?.dataB64 === 'string' &&
    imageValue.dataB64.trim() !== '';
}

function extractFirmwareVersion(parameters) {
  if (typeof parameters === 'string') {
    return parameters;
  }

  if (!parameters || typeof parameters !== 'object') {
    return '';
  }

  const imageVersion = parameters.image?.version;
  const candidates = [
    imageVersion,
    parameters.version,
    parameters.installed_version,
    parameters.current_version,
    parameters.firmware_version,
    parameters.result
  ];

  for (const candidate of candidates) {
    if (typeof candidate === 'string' && candidate.trim() !== '') {
      return candidate;
    }

    if (Number.isFinite(candidate)) {
      return String(candidate);
    }
  }

  if (Object.keys(parameters).length === 1) {
    const onlyValue = Object.values(parameters)[0];
    if (typeof onlyValue === 'string' || Number.isFinite(onlyValue)) {
      return String(onlyValue);
    }
  }

  return '';
}
