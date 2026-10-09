// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';

const kGroup = MODULE_IDS.SSH;

export function renderSshPage(container, { sendPayload, addLog }) {
  container.innerHTML = '';

  const page = document.createElement('div');
  page.className = 'page';
  page.innerHTML = `
    <h1>SSH Configuration</h1>
    <section class="section">
      <h2>Status</h2>
      <div class="form-grid ssh-status-grid">
        <span class="label">SSH</span>
        <span id="ssh-status" class="network-status">Loading...</span>
      </div>
      <div class="network-actions">
        <button class="btn ssh-action-button" id="ssh-enable" type="button">Enable SSH</button>
        <button class="btn btn-secondary network-reset ssh-action-button" id="ssh-disable" type="button">Disable SSH</button>
      </div>
    </section>
    <section class="section">
      <h2>Root Password</h2>
      <div class="form-grid ssh-password-grid">
        <label class="label" for="ssh-password">Password</label>
        <div class="ssh-password-field">
          <input class="input ssh-password-control" id="ssh-password" type="password" autocomplete="new-password" />
          <button class="btn btn-peek ssh-password-reveal" id="ssh-password-reveal" type="button" aria-label="Show password while pressed" title="Show password while pressed">◉</button>
        </div>
        <label class="label" for="ssh-password-confirm">Repeat password</label>
        <div class="ssh-password-field">
          <input class="input ssh-password-control" id="ssh-password-confirm" type="password" autocomplete="new-password" />
          <button class="btn btn-peek ssh-password-reveal" id="ssh-password-confirm-reveal" type="button" aria-label="Show password while pressed" title="Show password while pressed">◉</button>
        </div>
      </div>
      <p id="ssh-warning" class="network-settings-warning" hidden></p>
      <div class="network-actions">
        <button class="btn" id="ssh-set-password" type="button">Set Password</button>
      </div>
    </section>
  `;
  container.appendChild(page);

  const statusElement = page.querySelector('#ssh-status');
  const enableButton = page.querySelector('#ssh-enable');
  const disableButton = page.querySelector('#ssh-disable');
  const passwordInput = page.querySelector('#ssh-password');
  const passwordConfirmInput = page.querySelector('#ssh-password-confirm');
  const setPasswordButton = page.querySelector('#ssh-set-password');
  const warningElement = page.querySelector('#ssh-warning');

  let connected = false;
  let statusLoaded = false;
  let sshAvailable = false;
  // Tracks the request that is waiting for a backend response, so repeated clicks
  // cannot send overlapping SSH operations.
  let pendingAction = '';
  // When the user enables SSH with filled password fields, SSH is enabled only
  // after the backend confirms that the password was set successfully.
  let pendingEnableAfterPassword = false;

  function formatSendStatus(result) {
    if (result.ok) {
      return 'sent';
    }
    const details = [result.error, result.websocketState].filter(Boolean).join(', ');
    return `rejected${details ? `: ${details}` : ''}`;
  }

  function setWarning(message) {
    warningElement.textContent = message || '';
    warningElement.hidden = !message;
  }

  function setBusy(action) {
    pendingAction = action;
    updateControls();
  }

  function clearBusy(action = pendingAction) {
    if (pendingAction === action) {
      pendingAction = '';
      pendingEnableAfterPassword = false;
      updateControls();
    }
  }

  function passwordValues() {
    return {
      password: passwordInput.value,
      confirmation: passwordConfirmInput.value
    };
  }

  function hasPasswordInput() {
    const values = passwordValues();
    return values.password !== '' || values.confirmation !== '';
  }

  function validatePassword({ allowEmpty = false } = {}) {
    const values = passwordValues();
    // Enabling SSH is allowed without changing the password. Any non-empty input
    // still has to pass the same checks as an explicit password change.
    if (allowEmpty && values.password === '' && values.confirmation === '') {
      return true;
    }
    if (values.password !== values.confirmation) {
      setWarning('Passwords do not match.');
      return false;
    }
    if (values.password.length > 256) {
      setWarning('Use a password with at most 256 characters.');
      return false;
    }
    return true;
  }

  function updatePasswordValidation() {
    const values = passwordValues();
    passwordInput.classList.remove('password-input-match', 'password-input-mismatch');
    passwordConfirmInput.classList.remove('password-input-match', 'password-input-mismatch');

    if (values.password === '' && values.confirmation === '') {
      updateControls();
      return;
    }

    const className = values.password === values.confirmation
      ? 'password-input-match'
      : 'password-input-mismatch';
    passwordInput.classList.add(className);
    passwordConfirmInput.classList.add(className);
    updateControls();
  }

  function updateStatus(parameters = {}) {
    const socketActive = parameters.socket_active === true;
    const socketEnabled = parameters.socket_enabled === true;

    statusLoaded = true;
    // Password changes are only exposed after the backend reports that SSH is
    // either currently active or enabled for boot.
    sshAvailable = socketActive || socketEnabled;
    statusElement.textContent = sshAvailable ? 'On' : 'Off';
    updateControls();
  }

  function updateControls() {
    const busy = pendingAction !== '';
    enableButton.hidden = statusLoaded && sshAvailable;
    disableButton.hidden = !statusLoaded || !sshAvailable;
    enableButton.disabled = !connected || busy;
    disableButton.disabled = !connected || busy;
    setPasswordButton.disabled = !connected || busy || !statusLoaded || !sshAvailable ||
      !hasPasswordInput() || passwordInput.value !== passwordConfirmInput.value ||
      passwordInput.value.length > 256;
    passwordInput.disabled = !connected || busy;
    passwordConfirmInput.disabled = !connected || busy;
  }

  function sendAction(action, parameters = {}) {
    const requestResponseObject = {};
    Object.entries(parameters).forEach(([key, value]) => {
      // The shared request builder expects each parameter to carry the backend
      // path and type metadata, even for simple one-field SSH actions.
      requestResponseObject[key] = {
        backend_path: key,
        value_type: 'string',
        value
      };
    });

    const request = buildRequest(kGroup, action, requestResponseObject);
    const result = sendPayload(request);
    addLog(`${kGroup}.${action} ${formatSendStatus(result)}`);
    if (!result.ok) {
      setWarning(`Unable to send SSH ${action}: ${formatSendStatus(result)}`);
    }
    return result;
  }

  function requestStatus() {
    const result = sendAction('read');
    if (result.ok) {
      setBusy('read');
    }
  }

  function sendPassword() {
    if (!validatePassword()) {
      updateControls();
      return false;
    }
    const result = sendAction('set_password', { password: passwordInput.value });
    if (result.ok) {
      setBusy('set_password');
      setWarning('Setting SSH password.');
    }
    return result.ok;
  }

  function sendEnable() {
    const result = sendAction('enable');
    if (result.ok) {
      setBusy('enable');
      setWarning('Enabling SSH.');
      return true;
    }
    pendingEnableAfterPassword = false;
    updateControls();
    return false;
  }

  function clearPasswordFields() {
    passwordInput.value = '';
    passwordConfirmInput.value = '';
    passwordInput.type = 'password';
    passwordConfirmInput.type = 'password';
    updatePasswordValidation();
  }

  function bindHoldToReveal(button, input) {
    const show = () => {
      if (!input.disabled) {
        input.type = 'text';
      }
    };
    const hide = () => {
      input.type = 'password';
    };
    button.addEventListener('mousedown', show);
    button.addEventListener('mouseup', hide);
    button.addEventListener('mouseleave', hide);
    button.addEventListener('blur', hide);
    button.addEventListener('touchstart', show, { passive: true });
    button.addEventListener('touchend', hide);
    button.addEventListener('touchcancel', hide);
  }

  enableButton.addEventListener('click', () => {
    if (!connected || pendingAction) {
      return;
    }
    if (!validatePassword({ allowEmpty: true })) {
      updateControls();
      return;
    }
    if (hasPasswordInput()) {
      pendingEnableAfterPassword = true;
      if (!sendPassword()) {
        pendingEnableAfterPassword = false;
        updateControls();
      } else {
        setWarning('Setting SSH password. SSH will be enabled afterwards.');
      }
      return;
    }
    sendEnable();
  });

  disableButton.addEventListener('click', () => {
    if (!connected || pendingAction) {
      return;
    }
    const result = sendAction('disable');
    if (result.ok) {
      setBusy('disable');
      setWarning('Disabling SSH and stopping active sessions.');
    }
  });

  setPasswordButton.addEventListener('click', () => {
    if (!connected || pendingAction || !sshAvailable) {
      return;
    }
    sendPassword();
  });

  [passwordInput, passwordConfirmInput].forEach((input) => {
    input.addEventListener('input', updatePasswordValidation);
  });
  bindHoldToReveal(page.querySelector('#ssh-password-reveal'), passwordInput);
  bindHoldToReveal(page.querySelector('#ssh-password-confirm-reveal'), passwordConfirmInput);
  updateControls();

  return {
    onMessage(message) {
      if (message.type === 'ssh.read.result') {
        addLog('ssh.read.result received');
        clearBusy('read');
        updateStatus(message.parameters || {});
        return;
      }

      if (message.type === 'ssh.enable.ack') {
        addLog('ssh.enable.ack received');
        clearBusy('enable');
        setWarning('SSH enabled.');
        requestStatus();
        return;
      }

      if (message.type === 'ssh.disable.ack') {
        addLog('ssh.disable.ack received');
        clearBusy('disable');
        setWarning('SSH disabled.');
        requestStatus();
        return;
      }

      if (message.type === 'ssh.set_password.ack') {
        addLog('ssh.set_password.ack received');
        clearPasswordFields();
        if (pendingEnableAfterPassword) {
          pendingAction = '';
          if (!sendEnable()) {
            setWarning('SSH password set, but enabling SSH could not be sent.');
          }
          return;
        }
        clearBusy('set_password');
        setWarning('SSH password set.');
        requestStatus();
        return;
      }

      if (typeof message.type === 'string' &&
          message.type.startsWith('ssh.') &&
          message.type.endsWith('.error')) {
        const error = (message.parameters && message.parameters.error) || 'ssh operation failed';
        addLog(`${message.type}: ${error}`);
        clearBusy();
        setWarning(error);
      }
    },
    onConnectionChange(isConnected) {
      connected = isConnected;
      updateControls();
      if (connected) {
        requestStatus();
      }
    },
    onRequestTimeout({ moduleAction }) {
      if (typeof moduleAction === 'string' && moduleAction.startsWith(`${kGroup}:`)) {
        clearBusy();
        setWarning('SSH request timed out.');
      }
    },
    destroy() {}
  };
}
