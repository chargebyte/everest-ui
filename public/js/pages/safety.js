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
  const pageConfig = loadPageConfig(MODULE_IDS.SAFETY, parameterCatalog);
  const settingsMatrixBlock = pageConfig.blocks.find((block) => block.kind === 'settings_matrix');
  const readTemplate = renderSettingsMatrixBlock(settingsMatrixBlock, {
    buttonLabel: 'Save Configuration'
  });
  const pendingWrites = new Map();

  container.innerHTML = '';
  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const loadingElement = createSafetyLoadingElement();
  const noticeElement = document.createElement('p');
  noticeElement.className = 'safety-page-notice';
  noticeElement.hidden = true;
  const panesElement = document.createElement('div');
  panesElement.className = 'safety-controller-panes';
  pageElement.append(loadingElement, noticeElement, panesElement);
  container.appendChild(pageElement);

  function requestSettings() {
    loadingElement.hidden = false;
    setSafetyLoadingPending(loadingElement, true);
    setSafetyLoadingMessage(loadingElement, 'Loading safety controller settings...');
    noticeElement.hidden = true;
    panesElement.replaceChildren();
    const request = buildRequest(
      pageConfig.actions.read_settings.group,
      pageConfig.actions.read_settings.action,
      readTemplate.requestResponseObject
    );
    sendSafetyRequest(sendPayload, addLog, request,
      pageConfig.actions.read_settings.group, pageConfig.actions.read_settings.action);
  }

  function createControllerPane(controller, index) {
    const pane = document.createElement('section');
    pane.className = 'safety-controller-pane';

    const identity = document.createElement('h2');
    identity.className = 'safety-controller-identity';
    identity.textContent = `/dev/${controller.device_name} - owned by ${controller.driver_module} (${controller.bsp_instance})`;
    pane.appendChild(identity);

    const status = document.createElement('p');
    status.className = 'safety-controller-status';
    status.setAttribute('role', 'status');
    status.hidden = true;
    pane.appendChild(status);

    if (!controller.available) {
      status.textContent = controller.message || 'Safety Controller settings are unavailable.';
      status.classList.add('is-unavailable');
      status.hidden = false;
      return pane;
    }

    const paneConfig = structuredClone(settingsMatrixBlock);
    paneConfig.sections.forEach((section) => {
      section.id = `${section.id}-controller-${index}`;
    });
    const matrix = renderSettingsMatrixBlock(paneConfig, {
      buttonLabel: 'Save Configuration'
    });
    const settings = controller.settings || {};
    matrix.applyAvailableParameters(settings);
    matrix.setValues(mapResponse('settings_matrix', matrix.requestResponseObject, {
      parameters: settings
    }));
    matrix.setDisabled(!controller.writable);
    pane.appendChild(matrix.element);

    if (controller.message) {
      status.textContent = controller.message;
      status.classList.add(controller.writable ? 'is-unavailable' : 'is-read-only');
      status.hidden = false;
    }

    matrix.bindSubmit(() => {
      if (!controller.writable) {
        return;
      }
      const request = buildRequest(
        pageConfig.actions.write_settings.group,
        pageConfig.actions.write_settings.action,
        matrix.getValues(matrix.requestResponseObject)
      );
      request.parameters.device_name = controller.device_name;
      pendingWrites.set(request.requestId, { matrix, status, writable: controller.writable });
      status.hidden = true;
      matrix.setDisabled(true);
      const sent = sendSafetyRequest(sendPayload, addLog, request,
        pageConfig.actions.write_settings.group, pageConfig.actions.write_settings.action);
      if (!sent) {
        pendingWrites.delete(request.requestId);
        matrix.setDisabled(!controller.writable);
        status.textContent = 'The request could not be sent.';
        status.classList.add('is-unavailable');
        status.hidden = false;
      }
    });

    return pane;
  }

  return {
    onMessage(message) {
      if (message.type === 'safety.read_settings.result') {
        addLog('safety.read_settings.result received');
        loadingElement.hidden = true;
        setSafetyLoadingPending(loadingElement, false);
        const parameters = message.parameters || {};
        const controllers = parameters.controllers || [];
        if (parameters.resolution_error) {
          noticeElement.textContent = parameters.resolution_error;
          noticeElement.hidden = false;
        }
        if (controllers.length === 0 && !parameters.resolution_error) {
          noticeElement.textContent = 'No Safety Controller devices were found in the active EVerest configuration.';
          noticeElement.hidden = false;
        }
        controllers.forEach((controller, index) => {
          panesElement.appendChild(createControllerPane(controller, index));
        });
        return;
      }

      if (message.type === 'safety.write_settings.ack') {
        addLog('safety.write_settings.ack received');
        return;
      }

      if (message.type === 'safety.write_settings.result' ||
          message.type === 'safety.write_settings.error') {
        const pending = pendingWrites.get(message.requestId);
        if (!pending) {
          return;
        }
        pendingWrites.delete(message.requestId);
        pending.matrix.setDisabled(!pending.writable);
        pending.status.textContent = message.type === 'safety.write_settings.result'
          ? 'Safety Controller settings flashed successfully.'
          : message.parameters?.message ||
            `Unable to apply Safety Controller settings: ${message.parameters?.error || 'unknown error'}`;
        pending.status.classList.toggle('is-unavailable', message.type === 'safety.write_settings.error');
        pending.status.hidden = false;
      }

      if (message.type === 'safety.read_settings.error') {
        const error = message.parameters.error;
        addLog(`safety.read_settings.error: ${error}`);
        loadingElement.hidden = true;
        setSafetyLoadingMessage(loadingElement, `Unable to load Safety Controller settings: ${error}`);
        setSafetyLoadingPending(loadingElement, false);
      }
    },
    onConnectionChange(connected) {
      if (connected === true) {
        requestSettings();
      }
    },
    destroy() {
      pendingWrites.clear();
    }
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
  const result = sendPayload(request);
  addLog(`${group}.${action} ${result.ok ? 'sent' : 'rejected'}`);
  return result.ok;
}
