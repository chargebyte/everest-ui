// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderSettingsTableBlock } from '../ui/settingsTable.js';
import { renderConfigLoaderBlock } from '../ui/configLoader.js';
import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { mapResponse } from '../protocol/responseMapper.js';

export function renderEverestPage(container, {
  appTitle,
  parameterCatalog,
  sendPayload,
  addLog
}) {
  // load runtime parameter object from parameter catalog
  const pageConfig = loadPageConfig(MODULE_IDS.EVEREST, parameterCatalog);
  const settingsTableBlock = pageConfig.blocks.find((block) => block.kind === 'settings_table');
  const configLoaderBlock = pageConfig.blocks.find((block) => block.kind === 'config_loader');

  // render UI elements
  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  const pageTitle =
    typeof appTitle === 'string' && appTitle.trim() !== ''
      ? `${appTitle.trim()} Configuration`
      : pageConfig.title;
  pageElement.innerHTML = `<h1>${pageTitle}</h1>`;

  const settingsTable = renderSettingsTableBlock(settingsTableBlock, {
    buttonLabel: 'Save Configuration',
    reloadButtonLabel: 'Reload Configuration',
    showUnassignedValueHint: true
  });
  const configLoader = renderConfigLoaderBlock(configLoaderBlock, {
    downloadButtonLabel: 'Download config.yaml',
    uploadButtonLabel: 'Upload and Apply',
    onFileError(errorMessage) {
      addLog(`everest.upload_config file error: ${errorMessage}`);
    }
  });

  let lastLoadedValues = null;
  let pendingWriteRequestId = null;
  let pendingReloadRequestId = null;
  let latestReadRequestId = null;

  settingsTable.bindSubmit(() => {
    const values = settingsTable.getValues(settingsTable.requestResponseObject);
    const writeEverestConfigRequest = buildRequest(
      pageConfig.actions.write_config_parameters.group,
      pageConfig.actions.write_config_parameters.action,
      values
    );
    settingsTable.setStatus('Applying configuration...', 'info');
    settingsTable.setApplyBusy(true);
    pendingWriteRequestId = writeEverestConfigRequest.requestId;
    if (!sendEverestRequest(
      sendPayload,
      addLog,
      writeEverestConfigRequest,
      pageConfig.actions.write_config_parameters.group,
      pageConfig.actions.write_config_parameters.action
    )) {
      pendingWriteRequestId = null;
      settingsTable.setApplyBusy(false);
      settingsTable.setStatus('Configuration could not be sent.', 'error');
    }
  });

  settingsTable.bindReload(() => {
    const currentValues = settingsTable.getValues(settingsTable.requestResponseObject);
    if (hasUnsavedSettings(currentValues, lastLoadedValues) &&
        !window.confirm('Reload configuration and discard unsaved changes?')) {
      return;
    }

    const readEverestConfigRequest = buildReadConfigRequest(
      pageConfig,
      settingsTable
    );
    settingsTable.setStatus('Reloading configuration...', 'info');
    settingsTable.setReloadBusy(true);
    pendingReloadRequestId = readEverestConfigRequest.requestId;
    latestReadRequestId = readEverestConfigRequest.requestId;
    addLog('everest configuration reload requested');
    if (!sendEverestRequest(
      sendPayload,
      addLog,
      readEverestConfigRequest,
      pageConfig.actions.read_config_parameters.group,
      pageConfig.actions.read_config_parameters.action
    )) {
      pendingReloadRequestId = null;
      settingsTable.setReloadBusy(false);
      settingsTable.setStatus('Configuration reload could not be sent.', 'error');
    }
  });

  configLoader.bindDownload(() => {
    const downloadEverestConfigRequest = buildRequest(
      pageConfig.actions.download_config.group,
      pageConfig.actions.download_config.action,
      {}
    );
    sendEverestRequest(
      sendPayload,
      addLog,
      downloadEverestConfigRequest,
      pageConfig.actions.download_config.group,
      pageConfig.actions.download_config.action
    );
  });

  configLoader.bindUpload(() => {
    configLoader.setStatus('Applying uploaded configuration...', 'info');
    const uploadAction = pageConfig.actions.upload_config;
    const uploadEverestConfigRequest = buildRequest(
      uploadAction.group,
      uploadAction.action,
      configLoader.getUploadRequestResponseObject(uploadAction)
    );
    sendEverestRequest(
      sendPayload,
      addLog,
      uploadEverestConfigRequest,
      uploadAction.group,
      uploadAction.action
    );
  });

  pageElement.appendChild(settingsTable.element);
  pageElement.appendChild(configLoader.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'everest.read_config_parameters.result') {
        if (!isMatchingRequestId(message.requestId, latestReadRequestId)) {
          addLog('everest.read_config_parameters.result ignored as stale');
          return;
        }
        addLog('everest.read_config_parameters.result received');
        configLoader.clearStatus();
        settingsTable.applyAvailableModules(message.parameters?._available_modules);
        const mappedValues = mapResponse('settings_table', settingsTable.requestResponseObject, message);
        settingsTable.setValues(mappedValues);
        lastLoadedValues = settingsTable.getValues(settingsTable.requestResponseObject);
        if (isMatchingRequest(message, pendingReloadRequestId)) {
          pendingReloadRequestId = null;
          settingsTable.setReloadBusy(false);
          settingsTable.setStatus('Configuration reloaded.', 'success');
          addLog('everest configuration reloaded');
        }
        return;
      }

      if (message.type === 'everest.write_config_parameters.ack') {
        if (!isMatchingRequest(message, pendingWriteRequestId)) {
          return;
        }
        addLog('everest.write_config_parameters.ack received');
        pendingWriteRequestId = null;
        lastLoadedValues = settingsTable.getValues(settingsTable.requestResponseObject);
        settingsTable.setApplyBusy(false);
        settingsTable.setStatus('Configuration saved.', 'success');
        return;
      }

      if (message.type === 'everest.download_config.result') {
        addLog('everest.download_config.result received');
        configLoader.downloadConfigFile(message.parameters || {});
        return;
      }

      if (message.type === 'everest.upload_config.ack') {
        addLog('everest.upload_config.ack received');
        configLoader.setStatus('Configuration uploaded and applied.', 'success');
        configLoader.clearSelection();
        return;
      }

      if (message.type === 'everest.read_config_parameters.error') {
        const error = message.parameters?.error || 'configuration reload failed';
        if (!isMatchingRequestId(message.requestId, latestReadRequestId)) {
          addLog(`everest.read_config_parameters.error ignored as stale: ${error}`);
          return;
        }
        if (isMatchingRequest(message, pendingReloadRequestId)) {
          pendingReloadRequestId = null;
          settingsTable.setReloadBusy(false);
          settingsTable.setStatus(error, 'error');
        }
        addLog(`everest.read_config_parameters.error: ${error}`);
      }

      if (message.type === 'everest.write_config_parameters.error') {
        const error = message.parameters?.error || 'configuration save failed';
        if (isMatchingRequest(message, pendingWriteRequestId)) {
          pendingWriteRequestId = null;
          settingsTable.setApplyBusy(false);
          settingsTable.setStatus(error, 'error');
        }
        addLog(`everest.write_config_parameters.error: ${error}`);
      }

      if (message.type === 'everest.download_config.error') {
        const error = message.parameters.error
        addLog(`everest.download_config.error: ${error}`);
      }

      if (message.type === 'everest.upload_config.error') {
        const error = message.parameters.error
        configLoader.setStatus(error, 'error');
        addLog(`everest.upload_config.error: ${error}`);
      }
    },
    onConnectionChange(connected) {
      if (connected === false) {
        if (pendingWriteRequestId !== null) {
          pendingWriteRequestId = null;
          settingsTable.setApplyBusy(false);
          settingsTable.setStatus('Configuration save interrupted by connection loss.', 'error');
        }
        if (pendingReloadRequestId !== null) {
          pendingReloadRequestId = null;
          settingsTable.setReloadBusy(false);
          settingsTable.setStatus('Configuration reload interrupted by connection loss.', 'error');
        }
        return;
      }

      // request current EVerest configuration after page is loaded and WS is connected
      if (connected === true) {
        const readEverestConfigRequest = buildReadConfigRequest(pageConfig, settingsTable);
        latestReadRequestId = readEverestConfigRequest.requestId;
        sendEverestRequest(
          sendPayload,
          addLog,
          readEverestConfigRequest,
          pageConfig.actions.read_config_parameters.group,
          pageConfig.actions.read_config_parameters.action
        );
      }
    },
    onRequestTimeout({ requestId, moduleAction }) {
      if (moduleAction === 'everest:write_config_parameters' &&
          isMatchingRequestId(requestId, pendingWriteRequestId)) {
        pendingWriteRequestId = null;
        settingsTable.setApplyBusy(false);
        settingsTable.setStatus('Configuration save timed out. Reload to verify the result.', 'error');
        addLog('everest.write_config_parameters timed out');
      }
      if (moduleAction === 'everest:read_config_parameters' &&
          isMatchingRequestId(requestId, pendingReloadRequestId)) {
        pendingReloadRequestId = null;
        settingsTable.setReloadBusy(false);
        settingsTable.setStatus('Configuration reload timed out.', 'error');
        addLog('everest.read_config_parameters reload timed out');
      }
    },
    destroy() {}
  };
}

export function hasUnsavedSettings(currentValues, lastLoadedValues) {
  if (!lastLoadedValues) {
    return false;
  }
  return JSON.stringify(currentValues) !== JSON.stringify(lastLoadedValues);
}

function buildReadConfigRequest(pageConfig, settingsTable) {
  const values = settingsTable.getValues(settingsTable.requestResponseObject);
  return buildRequest(
    pageConfig.actions.read_config_parameters.group,
    pageConfig.actions.read_config_parameters.action,
    values
  );
}

function isMatchingRequest(message, requestId) {
  return requestId !== null && isMatchingRequestId(message.requestId, requestId);
}

function isMatchingRequestId(requestId, expectedRequestId) {
  return expectedRequestId !== null && String(requestId) === String(expectedRequestId);
}

function sendEverestRequest(sendPayload, addLog, request, group, action) {
  const result = sendPayload(request);
  const ok = result.ok;
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
