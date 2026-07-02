// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderSettingsMatrixBlock } from '../ui/settingsMatrix.js';
import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { mapResponse } from '../protocol/responseMapper.js';

export function renderSafetyPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  // load runtime parameter object from parameter catalog
  const pageConfig = loadPageConfig(MODULE_IDS.SAFETY, parameterCatalog);
  const settingsMatrixBlock = pageConfig.blocks.find((block) => block.kind === 'settings_matrix');

  // render UI elements
  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const settingsMatrix = renderSettingsMatrixBlock(settingsMatrixBlock, {
    buttonLabel: 'Save Configuration'
  });
  settingsMatrix.element.hidden = true;

  const loadingElement = createSafetyLoadingElement();
  pageElement.appendChild(loadingElement);

  settingsMatrix.bindSubmit(() => {
    const values = settingsMatrix.getValues(settingsMatrix.requestResponseObject);
    const writeSafetySettingsRequest = buildRequest(
      pageConfig.actions.write_settings.group,
      pageConfig.actions.write_settings.action,
      values
    );
    sendSafetyRequest(
      sendPayload,
      addLog,
      writeSafetySettingsRequest,
      pageConfig.actions.write_settings.group,
      pageConfig.actions.write_settings.action
    );
  });

  pageElement.appendChild(settingsMatrix.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'safety.read_settings.result') {
        addLog('safety.read_settings.result received');
        settingsMatrix.applyAvailableParameters(message.parameters);
        settingsMatrix.setValues(
          mapResponse('settings_matrix', settingsMatrix.requestResponseObject, message)
        );
        loadingElement.hidden = true;
        setSafetyLoadingPending(loadingElement, false);
        settingsMatrix.element.hidden = false;
        return;
      }

      if (message.type === 'safety.write_settings.ack') {
        addLog('safety.write_settings.ack received');
      }

      if (message.type === 'safety.read_settings.error') {
        const error = message.parameters.error;
        addLog(`safety.read_settings.error: ${error}`);
        setSafetyLoadingMessage(loadingElement, `Unable to load safety controller settings: ${error}`);
        setSafetyLoadingPending(loadingElement, false);
      }

      if (message.type === 'safety.write_settings.error') {
        const error = message.parameters.error
        addLog(`safety.write_settings.error: ${error}`);
      }
    },
    onConnectionChange(connected) {
      // request current Safety configuration after page is loaded and WS is connected
      if (connected === true) {
        loadingElement.hidden = false;
        setSafetyLoadingPending(loadingElement, true);
        setSafetyLoadingMessage(loadingElement, 'Loading safety controller settings...');
        const values = settingsMatrix.getValues(settingsMatrix.requestResponseObject);
        const readSafetySettingsRequest = buildRequest(
          pageConfig.actions.read_settings.group,
          pageConfig.actions.read_settings.action,
          values
        );
        sendSafetyRequest(
          sendPayload,
          addLog,
          readSafetySettingsRequest,
          pageConfig.actions.read_settings.group,
          pageConfig.actions.read_settings.action
        );
      }
    },
    destroy() {}
  };
}

function createSafetyLoadingElement() {
  const loadingElement = document.createElement('section');
  loadingElement.className = 'section loading-state';
  loadingElement.innerHTML = `
    <span class="loading-spinner" aria-hidden="true"></span>
    <span class="loading-message">Loading safety controller settings...</span>
  `;
  return loadingElement;
}

function setSafetyLoadingMessage(loadingElement, message) {
  const messageElement = loadingElement.querySelector('.loading-message');
  if (messageElement) {
    messageElement.textContent = message;
  }
}

function setSafetyLoadingPending(loadingElement, pending) {
  const spinnerElement = loadingElement.querySelector('.loading-spinner');
  if (spinnerElement) {
    spinnerElement.hidden = !pending;
  }
}

function sendSafetyRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
