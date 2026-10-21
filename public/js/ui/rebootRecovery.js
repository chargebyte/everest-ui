// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

import { buildRequest } from '../protocol/requestBuilder.js';

const storageKey = 'everest_ui_reboot_recovery_v1';
const uuid = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const validWait = value => Number.isInteger(value) && value > 0 && value <= 2147483;
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
  }

  function clear() {
    cancelProbe();
    pending = null;
    recentFailure = null;
    persist();
    show();
  }

  function blockedScreen() {
    if (!pending || pending.phase === 'resetting') return false;
    pending.phase = 'blocked';
    persist();
    show();
    return true;
  }

  function observeStatus(status) {
    if (!isLoginStatus(status)) return false;
    if (!status.loginAllowed) {
      if (!pending) {
        pending = { version: 1, kind: status.rebootOperation === 'factory_reset' ? 'factory_reset' : 'reboot',
          requestId: -1, phase: 'blocked', bootId: status.bootId || '', waitSeconds: 30, waitStarted: now() };
      }
      pending.loginBlocked = true;
      pending.reason = status.loginDisabledReason;
      if (status.rebootOperation === 'factory_reset') {
        pending.kind = 'factory_reset';
        pending.completed = status.loginDisabledReason === 'waiting_for_reboot';
      }
      if (pending.phase === 'resetting') pending.phase = 'waiting';
      persist();
      waitForReturn();
      show();
      return true;
    }
    if (!pending) return false;
    // The backend, not the browser's boot heuristic, authorizes return after reboot or service restart.
    if (!status.authenticated || (pending.reason === 'login_state_unavailable' && !pending.completed)) {
      stopTransport();
      clear();
      onAvailable(status);
      return true;
    }
    return false;
  }

  async function probe() {
    if (!pending || pending.phase === 'resetting') return;
    const generation = epoch;
    const controller = new AbortController();
    abort = controller;
    const timeout = schedule(() => controller.abort(), 3000);
    try {
      const status = await readStatus(controller.signal);
      if (generation !== epoch || !pending) return;
      observeStatus(status);
    } catch { /* Preserve local progress while the device or web service is unavailable. */ }
    finally {
      cancelTimer(timeout);
      if (abort === controller) abort = null;
    }
    if (generation === epoch && pending) timer = schedule(probe, 2000);
  }

  function waitForReturn() {
    if (!pending) return;
    if (pending.phase === 'resetting') pending.phase = 'waiting';
    if (pending.waitStarted === null) pending.waitStarted = now();
    if (pending.phase === 'waiting' && now() >= pending.waitStarted + pending.waitSeconds * 1000) {
      pending.phase = 'blocked';
    }
    persist();
    if (!polling) {
      polling = true;
      if (pending.phase === 'waiting') {
        deadlineTimer = schedule(blockedScreen, Math.max(0, pending.waitStarted + pending.waitSeconds * 1000 - now()));
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
      waitSeconds: settings.wait_timeout_seconds, waitStarted: null, disconnected: false };
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
      if (canStart()) { begin('factory_reset', -1, 'waiting'); waitForReturn(); }
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
        pending.completed = true;
        waitForReturn();
      } else if (!message.ok && !pending.disconnected && !pending.loginBlocked) {
        const failure = pending.kind === 'factory_reset' && message.parameters?.error === 'factory_reset_failed'
          ? { ...pending, expires: now() + pending.waitSeconds * 1000 } : null;
        clear();
        recentFailure = failure;
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
              ['resetting', 'waiting', 'blocked', 'returning'].includes(value.phase) && validWait(value.waitSeconds) &&
              typeof value.bootId === 'string' && (value.bootId === '' || uuid.test(value.bootId)) &&
              (value.waitStarted === null || (Number.isFinite(value.waitStarted) && value.waitStarted >= 0)) &&
              Number.isFinite(value.requestId)) pending = value;
        } catch { /* Saved operation context conveys progress, never authorization. */ }
      }
      if (!pending) return 'none';
      if (pending.phase === 'returning') pending.phase = 'waiting';
      waitForReturn();
      return 'waiting';
    },
    retryRecovery: waitForReturn,
    manualReload: blockedScreen,
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
    element.className = `reboot-recovery-overlay${blocked ? ' reboot-login-blocked' : ''}`;
    element.querySelector('h2').textContent = resetting ? 'Factory reset in progress'
      : blocked ? 'Login not allowed' : 'Waiting for device reboot';
    const context = state.kind === 'factory_reset' && state.completed
      ? 'Factory reset completed. ' : '';
    const message = resetting
      ? 'The device will reboot automatically after factory reset. You will need to log in again.'
      : blocked
        ? `${context}${state.reason && state.reason !== 'waiting_for_reboot'
          ? 'Login is temporarily unavailable. Waiting for backend status.' : 'Login not allowed: waiting for device reboot.'} Automatic checks will continue.`
        : `${context}Waiting for the device to reboot. You will need to log in again.`;
    if (element.querySelector('p').textContent !== message) element.querySelector('p').textContent = message;
    element.querySelector('button').hidden = resetting || blocked;
  };
}
