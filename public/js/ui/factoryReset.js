// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

import { buildRequest } from '../protocol/requestBuilder.js';
import { MODULE_IDS } from '../protocol/constants.js';

export function renderFactoryResetPane({ sendPayload, addLog, actions, onBusyChange = () => {},
  confirm = (message) => window.confirm(message) }) {
  const element = document.createElement('section');
  element.className = 'section factory-reset';
  element.innerHTML = `
    <h2>Factory Reset</h2>
    <p id="factory-reset-status" role="status" aria-live="polite">Loading...</p>
    <div class="controls">
      <button class="btn btn-secondary" id="factory-reset-start" type="button" disabled>Factory Reset</button>
      <button class="btn btn-secondary" id="factory-reset-reboot" type="button" disabled>Reboot</button>
    </div>
  `;
  const status = element.querySelector('#factory-reset-status');
  const resetButton = element.querySelector('#factory-reset-start');
  const rebootButton = element.querySelector('#factory-reset-reboot');
  let connected = false;
  let externalBusy = false;
  let unknown = true;
  let backend = { state: 'idle', can_reset: false, reboot_available: false };
  let operation = null;
  let statusRequest = null;
  let polling = null;
  let rebooting = false;
  let destroyed = false;

  function isBusy() {
    return unknown || operation !== null || backend.state === 'running' || rebooting;
  }

  function sync() {
    const blocked = !connected || externalBusy || isBusy();
    resetButton.disabled = blocked || backend.can_reset !== true;
    rebootButton.disabled = blocked || backend.reboot_available !== true;
    onBusyChange(isBusy());
  }

  function send(action, parameters = {}) {
    const definition = actions?.[action];
    const request = buildRequest(definition?.group || MODULE_IDS.FACTORY_RESET,
      definition?.action || action, {});
    request.parameters = parameters;
    const result = sendPayload(request);
    if (!result.ok) {
      addLog(`factory_reset.${action} could not be sent`);
      return null;
    }
    return request.requestId;
  }

  function readStatus() {
    if (destroyed || !connected || statusRequest !== null) return;
    statusRequest = send('read_status');
  }

  function refreshOutcome() {
    if (backend.state === 'succeeded') {
      status.textContent = 'Factory reset completed. Reboot the device.';
    } else if (backend.state === 'failed') {
      status.textContent = 'Factory reset failed. You can still choose to reboot the device.';
    } else if (backend.state === 'running') {
      status.textContent = 'Factory reset in progress...';
    } else {
      status.textContent = backend.can_reset ? 'Ready' : 'Factory reset is not currently available.';
    }
  }

  resetButton.addEventListener('click', () => {
    if (resetButton.disabled || !connected || externalBusy || isBusy()) return;
    if (!confirm('Are you sure you want to reset this device to factory defaults?')) return;
    const requestId = send('reset', { confirm: true });
    if (requestId === null) return;
    operation = { requestId, action: 'reset' };
    statusRequest = null; // Ignore an older eligibility reply already in flight.
    status.textContent = 'Factory reset in progress...';
    sync();
  });
  rebootButton.addEventListener('click', () => {
    if (rebootButton.disabled || !connected || externalBusy || isBusy()) return;
    const requestId = send('reboot');
    if (requestId === null) return;
    operation = { requestId, action: 'reboot' };
    status.textContent = 'Requesting reboot...';
    sync();
  });

  sync();
  return {
    element,
    isBusy,
    setMaintenanceBusy(value) {
      externalBusy = value === true;
      sync();
    },
    onMessage(message) {
      if (!message.type?.startsWith('factory_reset.')) return;
      if (message.requestId === statusRequest && message.final !== false) {
        statusRequest = null;
        if (message.ok && message.type === 'factory_reset.read_status.result') {
          backend = message.parameters;
          unknown = false;
          refreshOutcome();
        } else {
          unknown = true;
          status.textContent = 'Factory reset status is unavailable.';
        }
        sync();
        return;
      }
      if (operation?.requestId !== message.requestId || message.final === false) return;
      const action = operation.action;
      operation = null;
      if (action === 'reset') {
        backend = { state: message.ok ? 'succeeded' : 'failed', can_reset: false,
          reboot_available: message.parameters?.restart_required === true };
        if (backend.reboot_available) {
          refreshOutcome();
        } else {
          backend.state = 'idle';
          status.textContent = 'Factory reset was not started.';
        }
        unknown = message.parameters?.restart_required !== true;
        readStatus();
      } else if (message.ok) {
        rebooting = true;
        status.textContent = 'Reboot requested.';
      } else {
        unknown = true;
        status.textContent = 'Unable to request reboot.';
        readStatus();
      }
      sync();
    },
    onConnectionChange(value) {
      connected = value === true;
      clearInterval(polling);
      polling = null;
      unknown = true;
      statusRequest = null;
      operation = null;
      if (connected) {
        readStatus();
        polling = setInterval(readStatus, 5000);
      } else {
        status.textContent = 'Disconnected. Waiting to confirm factory reset status.';
      }
      sync();
    },
    onRequestTimeout({ requestId, moduleAction }) {
      if (!moduleAction?.startsWith('factory_reset:')) return;
      const matchesStatus = statusRequest !== null && String(requestId) === String(statusRequest);
      const matchesOperation = operation !== null && String(requestId) === String(operation.requestId);
      if (!matchesStatus && !matchesOperation) return;
      if (matchesStatus) statusRequest = null;
      if (matchesOperation) operation = null;
      unknown = true;
      status.textContent = 'Factory reset outcome is unknown. Checking status...';
      sync();
      readStatus();
    },
    destroy() {
      destroyed = true;
      clearInterval(polling);
    }
  };
}
