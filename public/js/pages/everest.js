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
    buttonLabel: 'Save Configuration'
  });
  const configLoader = renderConfigLoaderBlock(configLoaderBlock, {
    downloadButtonLabel: 'Download config.yaml',
    uploadButtonLabel: 'Upload and Apply',
    onFileError(errorMessage) {
      addLog(`everest.upload_config file error: ${errorMessage}`);
    }
  });

  settingsTable.bindSubmit(() => {
    const values = settingsTable.getValues(settingsTable.requestResponseObject);
    const writeEverestConfigRequest = buildRequest(
      pageConfig.actions.write_config_parameters.group,
      pageConfig.actions.write_config_parameters.action,
      values
    );
    sendEverestRequest(
      sendPayload,
      addLog,
      writeEverestConfigRequest,
      pageConfig.actions.write_config_parameters.group,
      pageConfig.actions.write_config_parameters.action
    );
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
        addLog('everest.read_config_parameters.result received');
        configLoader.clearStatus();
        settingsTable.applyAvailableModules(message.parameters?._available_modules);
        settingsTable.setValues(
          mapResponse('settings_table', settingsTable.requestResponseObject, message)
        );
        return;
      }

      if (message.type === 'everest.write_config_parameters.ack') {
        addLog('everest.write_config_parameters.ack received');
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
        const error = message.parameters.error
        addLog(`everest.read_config_parameters.error: ${error}`);
      }

      if (message.type === 'everest.write_config_parameters.error') {
        const error = message.parameters.error
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
      // request current EVerest configuration after page is loaded and WS is connected
      if (connected === true) {
        const values = settingsTable.getValues(settingsTable.requestResponseObject);
        const readEverestConfigRequest = buildRequest(
          pageConfig.actions.read_config_parameters.group,
          pageConfig.actions.read_config_parameters.action,
          values
        );
        sendEverestRequest(
          sendPayload,
          addLog,
          readEverestConfigRequest,
          pageConfig.actions.read_config_parameters.group,
          pageConfig.actions.read_config_parameters.action
        );
      }
    },
    destroy() {}
  };
}

function sendEverestRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
