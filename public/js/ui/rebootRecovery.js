// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

import { buildRequest } from '../protocol/requestBuilder.js';

const storageKey = 'everest_ui_reboot_recovery_v1';
const uuid = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const validWait = value => Number.isInteger(value) && value > 0 && value <= 2147483;
const confirmed = state => state.operationAccepted === true || state.restrictionObserved === true;
export const isLoginStatus = status => typeof status?.loginAllowed === 'boolean' &&
  typeof status.loginDisabledReason === 'string' && typeof status.authenticated === 'boolean' &&
  typeof status.setupRequired === 'boolean' && (status.loginAllowed
    ? status.loginDisabledReason === '' : status.loginDisabledReason !== '');

function browserStorage() {
  try { return globalThis.sessionStorage; } catch { return null; }
}

export async function probeAuthStatus(signal) {
  const response = await fetch('/auth/status', { cache: 'no-store', credentials: 'same-origin', signal });
  if (!response.ok) throw new Error(`Auth status failed: HTTP ${response.status}`);
  const status = await response.json();
  if (!isLoginStatus(status)) throw new Error('Invalid authentication status');
  return status;
}

export function createRebootRecovery({ sendPayload, closeTransport, render = () => {}, onChange = () => {},
  onAvailable = () => {}, readStatus = probeAuthStatus, storage = browserStorage(), now = Date.now,
  schedule = setTimeout, cancelTimer = clearTimeout }) {
  let settings = null;
  let settingsRequest = null;
  let settingsTimer = null;
  let connected = false;
  let pending = null;
  let recentFailure = null;
  let timer = null;
  let deadlineTimer = null;
  let abort = null;
  let epoch = 0;
  let polling = false;
  let transportClosed = false;
  let probeAgain = false;
  // Discard the old presentation-only dismissal; it must never override backend restrictions.
  try { storage?.removeItem('everest_ui_reboot_dismissal_v1'); } catch { /* Optional storage. */ }

  function persist() {
    try {
      if (pending) storage?.setItem(storageKey, JSON.stringify(pending));
      else storage?.removeItem(storageKey);
    } catch { /* In-memory progress remains available. */ }
  }

  function show() {
    render(pending ? { ...pending } : null);
    onChange();
  }

  function stopTransport() {
    connected = false;
    settingsRequest = null;
    cancelTimer(settingsTimer);
    if (transportClosed) return;
    transportClosed = true;
    closeTransport();
  }

  function cancelProbe() {
    polling = false;
    epoch++;
    cancelTimer(timer);
    cancelTimer(deadlineTimer);
    timer = deadlineTimer = null;
    abort?.abort();
    abort = null;
    probeAgain = false;
  }

  function clear() {
    cancelProbe();
    pending = null;
    recentFailure = null;
    persist();
    show();
  }

  function outcomeScreen() {
    if (!pending || pending.phase === 'resetting') return false;
    pending.phase = confirmed(pending) ? 'blocked' : 'unconfirmed';
    persist();
    show();
    return true;
  }

  function observeStatus(status, { probeStartedAt = null } = {}) {
    if (!isLoginStatus(status)) return false;
    if (!status.loginAllowed) {
      if (!pending) {
        pending = { version: 1, kind: status.rebootOperation === 'factory_reset' ? 'factory_reset' : 'reboot',
          requestId: -1, phase: 'blocked', bootId: status.bootId || '', waitSeconds: 30, waitStarted: now() };
      }
      pending.restrictionObserved = true;
      pending.reason = status.loginDisabledReason;
      if (status.loginDisabledReason === 'waiting_for_reboot') pending.operationAccepted = true;
      if (status.rebootOperation === 'factory_reset') {
        pending.kind = 'factory_reset';
        pending.completed = status.loginDisabledReason === 'waiting_for_reboot';
      }
      if (pending.phase === 'resetting') pending.phase = 'waiting';
      if (pending.phase === 'unconfirmed') pending.phase = 'blocked';
      persist();
      waitForReturn();
      show();
      return true;
    }
    if (!pending) return false;
    const freshAfterDeadline = probeStartedAt !== null && pending.waitStarted !== null &&
      probeStartedAt >= pending.waitStarted + pending.waitSeconds * 1000;
    const freshAfterRejection = probeStartedAt !== null && pending.rejected === true &&
      probeStartedAt >= pending.rejectionObservedAt;
    const unconfirmedReturn = !confirmed(pending) &&
      (pending.statusCheckOnly === true || freshAfterDeadline || freshAfterRejection);
    // A real restriction requires session invalidation. An unaccepted request must not trap a valid session.
    if (!status.authenticated || unconfirmedReturn) {
      const notice = status.authenticated && !pending.statusCheckOnly
        ? pending.knownFailure ? 'factory_reset_failed' : 'reboot_not_confirmed' : undefined;
      stopTransport();
      clear();
      onAvailable(status, { notice });
      return true;
    }
    return false;
  }

  async function probe() {
    if (!pending || pending.phase === 'resetting') return;
    const generation = epoch;
    const probeStartedAt = now();
    timer = null;
    const controller = new AbortController();
    abort = controller;
    const timeout = schedule(() => controller.abort(), 3000);
    try {
      const status = await readStatus(controller.signal);
      if (generation !== epoch || !pending) return;
      observeStatus(status, { probeStartedAt });
    } catch { /* Preserve local progress while the device or web service is unavailable. */ }
    finally {
      cancelTimer(timeout);
      if (abort === controller) abort = null;
    }
    if (generation === epoch && pending) {
      timer = schedule(probe, probeAgain ? 0 : 2000);
      probeAgain = false;
    }
  }

  function checkImmediately() {
    if (abort) { probeAgain = true; return; }
    cancelTimer(timer);
    timer = schedule(probe, 0);
  }

  function waitForReturn() {
    if (!pending) return;
    if (pending.phase === 'resetting') pending.phase = 'waiting';
    if (pending.waitStarted === null) pending.waitStarted = now();
    if (pending.phase === 'waiting' && now() >= pending.waitStarted + pending.waitSeconds * 1000) {
      pending.phase = confirmed(pending) ? 'blocked' : 'unconfirmed';
    }
    persist();
    if (!polling) {
      polling = true;
      if (pending.phase === 'waiting') {
        deadlineTimer = schedule(outcomeScreen, Math.max(0, pending.waitStarted + pending.waitSeconds * 1000 - now()));
      }
      timer = schedule(probe, 0);
    }
    stopTransport();
    show();
  }

  function canStart() { return connected && settings !== null && pending === null; }

  function begin(kind, requestId, phase) {
    recentFailure = null;
    pending = { version: 1, kind, requestId, phase, bootId: settings.boot_id,
      waitSeconds: settings.wait_timeout_seconds, waitStarted: null, disconnected: false,
      operationAccepted: false, restrictionObserved: false };
    persist();
    show();
  }

  function loadSettings() {
    if (!connected || pending || settingsRequest !== null) return;
    const request = buildRequest('system', 'read_reboot_settings', {});
    if (sendPayload(request).ok) settingsRequest = request.requestId;
    else settingsTimer = schedule(loadSettings, 2000);
  }

  return {
    canStart,
    isActive: () => pending !== null,
    observeStatus,
    onLoginRestricted(reason) {
      observeStatus({ loginAllowed: false, loginDisabledReason: reason, authenticated: false, setupRequired: false });
    },
    onStatusUnavailable() {
      if (!pending) {
        pending = { version: 1, kind: 'reboot', requestId: -1, phase: 'unconfirmed', bootId: '',
          waitSeconds: 30, waitStarted: now(), statusCheckOnly: true,
          operationAccepted: false, restrictionObserved: false };
      }
      waitForReturn();
    },
    onConnected() {
      transportClosed = false;
      if (pending) { stopTransport(); return; }
      connected = true;
      settings = null;
      loadSettings();
      onChange();
    },
    onDisconnected() {
      connected = false;
      settingsRequest = null;
      cancelTimer(settingsTimer);
      if (!pending && recentFailure && now() <= recentFailure.expires) {
        pending = { ...recentFailure };
        delete pending.expires;
        recentFailure = null;
      }
      if (!pending) { onChange(); return false; }
      pending.disconnected = true;
      waitForReturn();
      return true;
    },
    requestReboot(definition) {
      if (!canStart()) return false;
      const request = buildRequest(definition.group, definition.action, {});
      begin('reboot', request.requestId, 'waiting');
      if (!sendPayload(request, { expectResponse: false }).ok) { clear(); return false; }
      waitForReturn();
      return true;
    },
    requestFactoryReset(request) {
      if (!canStart()) return { ok: false, error: 'reboot_settings_unavailable' };
      begin('factory_reset', request.requestId, 'resetting');
      const result = sendPayload(request);
      if (!result.ok) clear();
      return result;
    },
    expectFactoryReboot() {
      if (canStart()) {
        begin('factory_reset', -1, 'waiting');
        pending.operationAccepted = true;
        pending.completed = true;
        waitForReturn();
      }
    },
    onMessage(message) {
      if (settingsRequest !== null && message.requestId === settingsRequest && message.final !== false) {
        settingsRequest = null;
        const values = message.parameters;
        if (message.ok && message.type === 'system.read_reboot_settings.result' &&
            validWait(values?.wait_timeout_seconds) && typeof values.boot_id === 'string' &&
            (values.boot_id === '' || uuid.test(values.boot_id))) settings = values;
        else { settings = null; settingsTimer = schedule(loadSettings, 2000); }
        onChange();
        return true;
      }
      if (!pending || String(message.requestId) !== String(pending.requestId) || message.final === false) return false;
      if (message.parameters?.reboot_expected === true) {
        pending.operationAccepted = true;
        pending.completed = pending.kind === 'factory_reset';
        if (pending.phase === 'unconfirmed') pending.phase = 'blocked';
        waitForReturn();
      } else if (!message.ok && !confirmed(pending)) {
        pending.knownFailure = pending.kind === 'factory_reset' && message.parameters?.error === 'factory_reset_failed';
        if (pending.phase === 'resetting' && !pending.disconnected) {
          const failure = pending.knownFailure ? { ...pending, expires: now() + pending.waitSeconds * 1000 } : null;
          clear();
          recentFailure = failure;
        } else {
          pending.rejected = true;
          pending.rejectionObservedAt = now();
          waitForReturn();
          checkImmediately();
        }
      }
      return false;
    },
    onRequestTimeout({ requestId }) {
      if (settingsRequest !== null && String(requestId) === String(settingsRequest)) {
        settingsRequest = null;
        settingsTimer = schedule(loadSettings, 2000);
        return true;
      }
      if (pending && String(requestId) === String(pending.requestId)) { waitForReturn(); return true; }
      return false;
    },
    resume() {
      if (!pending) {
        try {
          const value = JSON.parse(storage?.getItem(storageKey) || 'null');
          if (value?.version === 1 && ['reboot', 'factory_reset'].includes(value.kind) &&
              ['resetting', 'waiting', 'blocked', 'unconfirmed', 'returning'].includes(value.phase) && validWait(value.waitSeconds) &&
              typeof value.bootId === 'string' && (value.bootId === '' || uuid.test(value.bootId)) &&
              (value.waitStarted === null || (Number.isFinite(value.waitStarted) && value.waitStarted >= 0)) &&
              Number.isFinite(value.requestId)) {
            pending = value;
            // Preserve evidence from older saved contexts conservatively, without treating a request as acceptance.
            pending.operationAccepted = value.operationAccepted === true || value.completed === true;
            pending.restrictionObserved = value.restrictionObserved === true || value.loginBlocked === true;
          }
        } catch { /* Saved operation context conveys progress, never authorization. */ }
      }
      if (!pending) return 'none';
      if (pending.phase === 'returning') pending.phase = 'waiting';
      if (pending.phase === 'blocked' && !confirmed(pending)) pending.phase = 'unconfirmed';
      waitForReturn();
      return 'waiting';
    },
    retryRecovery: waitForReturn,
    manualReload: outcomeScreen,
    destroy() { cancelProbe(); cancelTimer(settingsTimer); }
  };
}

export function createRebootOverlay(appRoot, onReload) {
  let element = null;
  let previousFocus = null;
  return state => {
    if (!state) {
      element?.remove(); element = null;
      appRoot.inert = false;
      previousFocus?.focus?.();
      return;
    }
    if (!element) {
      previousFocus = document.activeElement;
      element = document.createElement('div');
      element.className = 'reboot-recovery-overlay';
      element.innerHTML = `<section class="reboot-recovery-dialog" role="alertdialog" aria-modal="true"
        aria-labelledby="reboot-recovery-title" aria-describedby="reboot-recovery-message" tabindex="-1">
        <h2 id="reboot-recovery-title"></h2>
        <p id="reboot-recovery-message" role="status" aria-live="polite"></p>
        <button class="btn btn-start" type="button" id="reboot-recovery-reload" hidden>Reload</button>
      </section>`;
      document.body.appendChild(element);
      appRoot.inert = true;
      element.querySelector('button').addEventListener('click', onReload);
      element.addEventListener('keydown', event => {
        if (event.key === 'Tab') {
          event.preventDefault();
          const button = element.querySelector('button');
          (button.hidden ? element.querySelector('section') : button).focus();
        }
      });
      element.querySelector('section').focus();
    }
    const resetting = state.phase === 'resetting';
    const blocked = state.phase === 'blocked';
    const unconfirmed = state.phase === 'unconfirmed';
    const failedAvailability = state.knownFailure && !confirmed(state);
    element.className = `reboot-recovery-overlay${blocked ? ' reboot-login-blocked' : ''}`;
    const context = state.kind === 'factory_reset' && state.completed
      ? 'Factory reset completed. ' : '';
    let title = 'Waiting for device reboot';
    let message = `${context}Waiting for the device to reboot. You will need to log in again.`;
    if (resetting) {
      title = 'Factory reset in progress';
      message = 'The device will reboot automatically after factory reset. You will need to log in again.';
    } else if (blocked) {
      title = 'Login not allowed';
      message = `${context}${state.reason && state.reason !== 'waiting_for_reboot'
        ? 'Login is temporarily unavailable. Waiting for backend status.' : 'Login not allowed: waiting for device reboot.'} Automatic checks will continue.`;
    } else if (failedAvailability) {
      title = 'Checking device availability';
      message = 'Factory reset failed. Checking device availability.';
    } else if (unconfirmed) {
      title = state.statusCheckOnly ? 'Checking device availability' : 'Reboot not confirmed';
      message = `Unable to confirm ${state.statusCheckOnly ? 'device status' : 'reboot'}. Checking device availability.`;
    }
    element.querySelector('h2').textContent = title;
    if (element.querySelector('p').textContent !== message) element.querySelector('p').textContent = message;
    element.querySelector('button').hidden = resetting || blocked || unconfirmed;
  };
}

export function showRecoveryNotice(root, notice) {
  const messages = {
    reboot_not_confirmed: 'Reboot was not confirmed. The backend still permits access.',
    factory_reset_failed: 'Factory reset failed. The backend still permits access.'
  };
  if (!messages[notice]) return;
  root.querySelector('.recovery-notice')?.remove();
  const element = document.createElement('p');
  element.className = 'recovery-notice';
  element.setAttribute('role', 'status');
  element.textContent = messages[notice];
  (root.querySelector('.content') || root.querySelector('.auth-panel') || root).prepend(element);
}
