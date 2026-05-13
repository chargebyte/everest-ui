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
    rebootRequired: false,
    uploadStarted: false,
    uploadInProgress: false,
    awaitingChunkAck: false,
    nextChunkIndex: 0,
    chunkCount: 0,
    uploadFinished: false,
    updateStarted: false
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

  updateBlock.bindUpdate(async () => {
    if (firmwareState.connected !== true) {
      addLog('firmware.upload_image.start rejected: websocket not connected');
      return;
    }

    if (!updateBlock.hasSelectedFile()) {
      addLog('firmware.upload_image.start rejected: no firmware image selected');
      return;
    }

    const selectedFileInfo = updateBlock.getSelectedFileInfo();
    if (!selectedFileInfo) {
      addLog('firmware.upload_image.start rejected: file metadata missing');
      return;
    }

    firmwareState.rebootRequired = false;
    firmwareState.uploadStarted = false;
    firmwareState.uploadInProgress = true;
    firmwareState.awaitingChunkAck = false;
    firmwareState.nextChunkIndex = 0;
    firmwareState.chunkCount = selectedFileInfo.chunk_count || 0;
    firmwareState.uploadFinished = false;
    firmwareState.updateStarted = false;

    updateBlock.setRebootRequired(false);
    updateBlock.setProgress('Starting upload...');

    const startRequest = buildRequestFromUpdateBlock(updateBlock, pageConfig.actions['upload_image.start']);
    const ok = sendFirmwareRequest(
      sendPayload,
      addLog,
      startRequest,
      pageConfig.actions['upload_image.start'].group,
      pageConfig.actions['upload_image.start'].action
    );

    if (!ok) {
      resetUploadState(firmwareState);
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
    async onMessage(message) {
      if (message.type === 'firmware.read_version.result' && message.ok === true) {
        addLog('firmware.read_version.result received');
        updateBlock.setValues(buildUpdaterValues(
          updateBlock,
          extractFirmwareVersion(message.parameters)
        ));
        return;
      }

      if (message.type === 'firmware.upload_image.start.ack' && message.ok === true) {
        addLog('firmware.upload_image.start.ack received');
        firmwareState.uploadStarted = true;
        firmwareState.awaitingChunkAck = false;
        updateBlock.setProgress('Uploading chunk 1 of ' + firmwareState.chunkCount);
        try {
          await sendNextUploadChunk({
            pageConfig,
            firmwareState,
            updateBlock,
            sendPayload,
            addLog
          });
        } catch (error) {
          addLog(`firmware.upload_image.chunk preparation failed: ${error instanceof Error ? error.message : String(error)}`);
          resetUploadState(firmwareState);
          updateBlock.setProgress(null);
        }
        return;
      }

      if (message.type === 'firmware.upload_image.chunk.ack' && message.ok === true) {
        addLog('firmware.upload_image.chunk.ack received');
        firmwareState.awaitingChunkAck = false;
        firmwareState.nextChunkIndex += 1;

        if (firmwareState.nextChunkIndex >= firmwareState.chunkCount) {
          updateBlock.setProgress('Finishing upload...');
          const finishAction = pageConfig.actions['upload_image.finish'];
          const finishRequest = buildRequest(
            finishAction.group,
            finishAction.action,
            {}
          );

          const ok = sendFirmwareRequest(
            sendPayload,
            addLog,
            finishRequest,
            finishAction.group,
            finishAction.action
          );

          if (!ok) {
            resetUploadState(firmwareState);
            updateBlock.setProgress(null);
          }
          return;
        }

        updateBlock.setProgress(
          `Uploading chunk ${firmwareState.nextChunkIndex + 1} of ${firmwareState.chunkCount}`
        );
        try {
          await sendNextUploadChunk({
            pageConfig,
            firmwareState,
            updateBlock,
            sendPayload,
            addLog
          });
        } catch (error) {
          addLog(`firmware.upload_image.chunk preparation failed: ${error instanceof Error ? error.message : String(error)}`);
          resetUploadState(firmwareState);
          updateBlock.setProgress(null);
        }
        return;
      }

      if (message.type === 'firmware.upload_image.finish.ack' && message.ok === true) {
        addLog('firmware.upload_image.finish.ack received');
        firmwareState.uploadFinished = true;
        firmwareState.updateStarted = true;
        updateBlock.setProgress('Awaiting acknowledgement...');

        const updateRequest = buildRequest(
          pageConfig.actions.update_image.group,
          pageConfig.actions.update_image.action,
          {}
        );

        const ok = sendFirmwareRequest(
          sendPayload,
          addLog,
          updateRequest,
          pageConfig.actions.update_image.group,
          pageConfig.actions.update_image.action
        );

        if (!ok) {
          firmwareState.updateStarted = false;
          updateBlock.setProgress(null);
        }
        return;
      }

      if (message.type === 'firmware.update_image.ack' && message.ok === true) {
        addLog('firmware.update_image.ack received');
        firmwareState.rebootRequired = false;
        updateBlock.setRebootRequired(false);
        updateBlock.setProgress('Update started');
        return;
      }

      if (
        (message.type === 'firmware.update_image.progress' ||
         message.type === 'firmware.update_image.progress.result') &&
        message.ok === true
      ) {
        addLog(`${message.type} received`);
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
        resetUploadState(firmwareState);
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

      if (isFirmwareUploadError(message.type)) {
        const error = message.parameters?.error;
        addLog(`${message.type}: ${error}`);
        resetUploadState(firmwareState);
        firmwareState.rebootRequired = false;
        updateBlock.setRebootRequired(false);
        updateBlock.setProgress(null);
        return;
      }

      if (message.type === 'firmware.update_image.error') {
        const error = message.parameters?.error;
        addLog(`firmware.update_image.error: ${error}`);
        resetUploadState(firmwareState);
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
        resetUploadState(firmwareState);
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
      } else {
        resetUploadState(firmwareState);
      }
    },
    destroy() {}
  };
}

async function sendNextUploadChunk({
  pageConfig,
  firmwareState,
  updateBlock,
  sendPayload,
  addLog
}) {
  if (firmwareState.connected !== true || firmwareState.uploadInProgress !== true) {
    return;
  }

  if (firmwareState.awaitingChunkAck) {
    return;
  }

  const chunkAction = pageConfig.actions['upload_image.chunk'];
  const chunkValues = updateBlock.getValues(updateBlock.requestResponseObject);
  const firmwareEntry = Object.values(chunkValues)[0];

  if (!firmwareEntry?.value || typeof firmwareEntry.value !== 'object') {
    throw new Error('Missing firmware updater entry for chunk upload');
  }

  const chunkPayload = await updateBlock.readSelectedFileChunk(firmwareState.nextChunkIndex);
  firmwareEntry.value.chunk_index = chunkPayload.chunk_index;
  firmwareEntry.value.dataB64 = chunkPayload.dataB64;

  const chunkRequest = buildRequest(
    chunkAction.group,
    chunkAction.action,
    chunkValues
  );

  firmwareState.awaitingChunkAck = true;
  const ok = sendFirmwareRequest(
    sendPayload,
    addLog,
    chunkRequest,
    chunkAction.group,
    chunkAction.action
  );

  if (!ok) {
    firmwareState.awaitingChunkAck = false;
    throw new Error('firmware.upload_image.chunk rejected');
  }
}

function sendFirmwareRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}

function buildRequestFromUpdateBlock(updateBlock, actionConfig) {
  const values = updateBlock.getValues(updateBlock.requestResponseObject);
  return buildRequest(actionConfig.group, actionConfig.action, values);
}

function buildUpdaterValues(updateBlock, version) {
  const values = updateBlock.getValues(updateBlock.requestResponseObject);
  const firmwareEntry = Object.values(values)[0];

  if (!firmwareEntry?.value || typeof firmwareEntry.value !== 'object') {
    firmwareEntry.value = {
      version: '',
      file_name: '',
      size_bytes: 0,
      chunk_size_bytes: 0,
      chunk_count: 0,
      chunk_index: 0,
      dataB64: ''
    };
  }

  firmwareEntry.value.version = version;
  return values;
}

function isFirmwareUploadError(messageType) {
  return messageType === 'firmware.upload_image.start.error' ||
    messageType === 'firmware.upload_image.chunk.error' ||
    messageType === 'firmware.upload_image.finish.error';
}

function resetUploadState(firmwareState) {
  firmwareState.uploadStarted = false;
  firmwareState.uploadInProgress = false;
  firmwareState.awaitingChunkAck = false;
  firmwareState.nextChunkIndex = 0;
  firmwareState.chunkCount = 0;
  firmwareState.uploadFinished = false;
  firmwareState.updateStarted = false;
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
