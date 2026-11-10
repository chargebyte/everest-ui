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
  const verificationDevices = new Set();
  const matrices = [];
  let verificationInProgress = false;
  let readRetryTimer = null;
  let connected = false;
  let featuresReady = false;
  let loaded = false;
  let metadataReady = false;
  let pendingReadId = null;
  let preserveRead = false;
  const controllerPanes = new Map();
  let nextPaneIndex = 0;

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

  function requestSettings(message = 'Loading safety controller settings...') {
    if (!connected || !featuresReady || pendingReadId !== null) return;
    if (readRetryTimer !== null) {
      clearTimeout(readRetryTimer);
      readRetryTimer = null;
    }
    loadingElement.hidden = false;
    setSafetyLoadingPending(loadingElement, true);
    setSafetyLoadingMessage(loadingElement, message);
    noticeElement.hidden = true;
    preserveRead = loaded;
    metadataReady = false;
    if (!preserveRead) {
      panesElement.replaceChildren();
      matrices.length = 0;
      controllerPanes.clear();
    }
    setAllMatricesDisabled(true);
    const request = buildRequest(
      pageConfig.actions.read_settings.group,
      pageConfig.actions.read_settings.action,
      readTemplate.requestResponseObject
    );
    pendingReadId = request.requestId;
    if (!sendSafetyRequest(sendPayload, addLog, request,
      pageConfig.actions.read_settings.group, pageConfig.actions.read_settings.action)) {
      pendingReadId = null;
      scheduleReadRetry(message);
    }
  }

  function setAllMatricesDisabled(disabled) {
    matrices.forEach(({ matrix, writable, reloadRequired }) => {
      matrix.setDisabled(disabled || !writable || reloadRequired || !connected || !featuresReady ||
        !metadataReady || verificationInProgress || pendingWrites.size > 0);
    });
  }

  function scheduleReadRetry(message) {
    if (readRetryTimer !== null || !connected || !featuresReady) {
      return;
    }
    readRetryTimer = setTimeout(() => {
      readRetryTimer = null;
      requestSettings(message);
    }, 2000);
  }

  function createControllerPane(controller) {
    const index = nextPaneIndex++;
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
    const paneState = { pane, status, deviceName: controller.device_name, matrix: null, writable: false };
    controllerPanes.set(controller.device_name, paneState);

    if (!controller.available) {
      status.textContent = controller.message || 'Safety Controller settings are unavailable.';
      status.classList.add('is-unavailable');
      status.hidden = false;
      return pane;
    }

    const settings = controller.settings || {};
    const paneConfig = filterSafetyEstopInstances(settingsMatrixBlock, settings);
    paneConfig.sections.forEach((section) => {
      section.id = `${section.id}-controller-${index}`;
    });
    const matrix = renderSettingsMatrixBlock(paneConfig, {
      buttonLabel: 'Save Configuration'
    });
    matrix.applyAvailableParameters(settings);
    matrix.setValues(mapResponse('settings_matrix', matrix.requestResponseObject, {
      parameters: settings
    }));
    matrix.setDisabled(!controller.writable);
    const matrixState = Object.assign(paneState, { matrix, writable: controller.writable });
    matrices.push(matrixState);
    pane.appendChild(matrix.element);

    if (controller.message) {
      status.textContent = controller.message;
      status.classList.add(controller.writable ? 'is-unavailable' : 'is-read-only');
      status.hidden = false;
    }

    matrix.bindSubmit(() => {
      if (!matrixState.writable || matrixState.reloadRequired || !connected || !featuresReady || !metadataReady ||
          verificationInProgress) {
        return;
      }
      const request = buildRequest(
        pageConfig.actions.write_settings.group,
        pageConfig.actions.write_settings.action,
        matrix.getValues(matrix.requestResponseObject)
      );
      request.parameters.device_name = controller.device_name;
      if (pendingWrites.size > 0) {
        return;
      }
      pendingWrites.set(String(request.requestId), { matrix, status, matrixState });
      status.hidden = true;
      setAllMatricesDisabled(true);
      const sent = sendSafetyRequest(sendPayload, addLog, request,
        pageConfig.actions.write_settings.group, pageConfig.actions.write_settings.action);
      if (!sent) {
        pendingWrites.delete(String(request.requestId));
        setAllMatricesDisabled(false);
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
        if (message.requestId !== pendingReadId) return;
        pendingReadId = null;
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
        const present = new Set();
        controllers.forEach((controller) => {
          present.add(controller.device_name);
          const existing = controllerPanes.get(controller.device_name);
          if (preserveRead && existing?.matrix && !verificationDevices.has(controller.device_name)) {
            existing.writable = controller.available === true && controller.writable === true;
            existing.status.textContent = controller.message || (!controller.available
              ? 'Safety Controller settings are unavailable.' : '');
            existing.status.hidden = !existing.status.textContent;
          } else {
            if (existing?.matrix) matrices.splice(matrices.indexOf(existing), 1);
            existing?.pane.remove();
            panesElement.appendChild(createControllerPane(controller));
          }
        });
        for (const [name, existing] of controllerPanes) {
          if (!present.has(name)) {
            existing.writable = false;
            existing.status.textContent = 'This Safety Controller is no longer available. Your edits are retained.';
            existing.status.hidden = false;
          }
        }
        loaded = true;
        metadataReady = true;
        if (verificationInProgress) {
          pendingWrites.clear();
          verificationDevices.clear();
          verificationInProgress = false;
        }
        setAllMatricesDisabled(false);
        return;
      }

      if (message.type === 'safety.write_settings.ack') {
        const pending = pendingWrites.get(String(message.requestId));
        if (pending && message.final === false) {
          pending.status.textContent = 'Applying Safety Controller settings...';
          pending.status.classList.remove('is-unavailable');
          pending.status.hidden = false;
        } else {
          addLog('safety.write_settings.ack received');
        }
        return;
      }

      if (message.type === 'safety.write_settings.result' ||
          message.type === 'safety.write_settings.error') {
        const pending = pendingWrites.get(String(message.requestId));
        if (!pending) {
          return;
        }
        pendingWrites.delete(String(message.requestId));
        if (safetyWriteRequiresReload(message.parameters)) {
          pending.matrixState.reloadRequired = true;
        }
        setAllMatricesDisabled(false);
        pending.status.textContent = message.type === 'safety.write_settings.result'
          ? 'Safety Controller settings flashed successfully.'
          : message.parameters?.message || getSafetyWriteErrorMessage(message.parameters);
        pending.status.classList.toggle('is-unavailable', message.type === 'safety.write_settings.error');
        pending.status.hidden = false;
      }

      if (message.type === 'safety.read_settings.error') {
        if (message.requestId !== pendingReadId) return;
        pendingReadId = null;
        const error = message.parameters.error;
        addLog(`safety.read_settings.error: ${error}`);
        if (error === 'safety_controller_busy') {
          const messageText = 'A Safety Controller write is in progress; waiting to verify settings...';
          loadingElement.hidden = false;
          setSafetyLoadingMessage(loadingElement, messageText);
          setSafetyLoadingPending(loadingElement, true);
          scheduleReadRetry(messageText);
          return;
        }
        loadingElement.hidden = true;
        setSafetyLoadingMessage(loadingElement, `Unable to load Safety Controller settings: ${error}`);
        setSafetyLoadingPending(loadingElement, false);
      }
    },
    onConnectionChange(connected) {
      setConnected(connected);
    },
    onFeatureAvailabilityChange(ready) {
      featuresReady = ready;
      if (!ready && loaded) {
        noticeElement.textContent = 'Waiting for connection and feature availability. Your edits are retained.';
        noticeElement.hidden = false;
      }
      setAllMatricesDisabled(false);
      if (ready) requestSettings();
    },
    onRequestTimeout({ requestId, moduleAction }) {
      if (moduleAction === 'safety:read_settings' && String(pendingReadId) === String(requestId)) {
        pendingReadId = null;
        addLog(`safety.read_settings timed out: ${requestId}`);
        scheduleReadRetry(verificationInProgress
          ? 'The write may still be processing; verifying the controller settings...'
          : 'Unable to refresh controller availability; retrying. Your edits are retained.');
        return;
      }
      if (moduleAction !== 'safety:write_settings' || !pendingWrites.has(String(requestId))) {
        return;
      }
      addLog(`safety.write_settings timed out: ${requestId}`);
      requireVerification();
      requestSettings('The write is still being processed; verifying the controller settings...');
    },
    destroy() {
      connected = false;
      if (readRetryTimer !== null) {
        clearTimeout(readRetryTimer);
        readRetryTimer = null;
      }
      pendingWrites.clear();
    }
  };

  function setConnected(value) {
    connected = value;
    if (!connected) {
      metadataReady = false;
      pendingReadId = null;
      if (pendingWrites.size > 0) requireVerification();
      if (readRetryTimer !== null) clearTimeout(readRetryTimer);
      readRetryTimer = null;
    } else if (featuresReady) {
      requestSettings();
    }
    setAllMatricesDisabled(false);
  }

  function requireVerification() {
    verificationInProgress = true;
    pendingWrites.forEach(({ matrixState }) => verificationDevices.add(matrixState.deviceName));
  }
}

export function filterSafetyEstopInstances(block, settings) {
  const config = structuredClone(block);
  config.sections = config.sections.filter(section => {
    if (section.id !== 'estops') return true;
    section.instances = section.instances.filter(instance => Object.hasOwn(settings, `estops_${instance.id}`));
    return section.instances.length > 0;
  });
  return config;
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

export function safetyWriteRequiresReload(parameters = {}) {
  return ['unavailable', 'refreshed'].includes(parameters?.cache_state);
}

export function getSafetyWriteErrorMessage(parameters = {}) {
  const error = parameters.error || 'unknown error';
  if (error === 'safety_controller_busy') {
    return 'Unable to apply Safety Controller settings: another system operation is in progress.';
  }
  if (error !== 'safety_controller_flash_failed') {
    return `Unable to apply Safety Controller settings: ${error}`;
  }

  const outcomeMessages = {
    start_failed: 'Flash command could not start.',
    timeout: 'Flash command timed out.',
    unexpected_exit: 'Flash command terminated unexpectedly.',
  };
  const status = outcomeMessages[parameters.flash_outcome] ||
    (parameters.flash_outcome === 'nonzero_exit' && Number.isInteger(parameters.exit_code)
      ? `Flash command exited with code ${parameters.exit_code}.`
      : 'Flash command failed.');
  const cacheStatus = parameters.cache_state === 'unavailable'
    ? ' Cached settings are unavailable; a controller readback is required. Check the backend logs.'
    : parameters.cache_state === 'refreshed'
      ? ' Current settings were read back from the controller. Reload this page before editing again.'
      : '';
  return `Unable to apply Safety Controller settings: ${status}${cacheStatus}`;
}
