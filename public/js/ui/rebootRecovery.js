// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

import { buildRequest } from '../protocol/requestBuilder.js';

const storageKey = 'everest_ui_reboot_recovery_v1';
const uuid = /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const validWait = (value) => Number.isInteger(value) && value > 0 && value <= 2147483;

function browserStorage() {
  try { return globalThis.sessionStorage; } catch { return null; }
}

export async function probeAuthStatus(signal) {
  const response = await fetch('/auth/status', { cache: 'no-store', credentials: 'same-origin', signal });
  if (!response.ok) throw new Error(`Auth status failed: HTTP ${response.status}`);
  return response.json();
}

export function createRebootRecovery({ sendPayload, closeTransport, render = () => {}, onChange = () => {},
  readStatus = probeAuthStatus, storage = browserStorage(), now = Date.now,
  schedule = setTimeout, cancelTimer = clearTimeout, reload = () => window.location.reload() }) {
  let settings = null;
  let settingsRequest = null;
  let settingsTimer = null;
  let connected = false;
  let pending = null;
  let recentFailure = null;
  let timer = null;
  let abort = null;
  let epoch = 0;

  function persist() {
    try {
      if (pending) storage?.setItem(storageKey, JSON.stringify(pending));
      else storage?.removeItem(storageKey);
    } catch { /* In-memory recovery still works when browser storage is unavailable. */ }
  }

  function show() {
    render(pending ? { ...pending, timedOut: pending.waitStarted !== null &&
      now() >= pending.waitStarted + pending.waitSeconds * 1000 } : null);
    onChange();
  }

  function cancelProbe() {
    epoch++;
    cancelTimer(timer);
    timer = null;
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

  async function probe() {
    if (!pending || pending.phase !== 'waiting') return;
    const generation = epoch;
    const controller = new AbortController();
    abort = controller;
    const timeout = schedule(() => controller.abort(), 3000);
    try {
      const status = await readStatus(controller.signal);
      if (generation !== epoch || !pending) return;
      if (uuid.test(pending.bootId) && uuid.test(status.bootId) && status.bootId !== pending.bootId &&
          typeof status.setupRequired === 'boolean' && status.authenticated === false) {
        pending.phase = 'returning';
        persist();
        show();
        reload();
        return;
      }
    } catch { /* Network failures are expected while the device is rebooting. */ }
    finally {
      cancelTimer(timeout);
      if (abort === controller) abort = null;
    }
    if (generation === epoch && pending?.phase === 'waiting') {
      show();
      timer = schedule(probe, 2000);
    }
  }

  function waitForReturn() {
    if (!pending) return;
    cancelProbe();
    pending.phase = 'waiting';
    if (pending.waitStarted === null) pending.waitStarted = now();
    persist();
    show();
    timer = schedule(probe, 0);
  }

  function canStart() { return connected && settings !== null && pending === null; }

  function begin(kind, requestId, phase) {
    recentFailure = null;
    pending = { version: 1, kind, requestId, phase, bootId: settings.boot_id,
      waitSeconds: settings.wait_timeout_seconds, waitStarted: null, disconnected: false };
    persist();
    show();
    if (phase === 'waiting') waitForReturn();
  }

  function loadSettings() {
    if (!connected || pending || settingsRequest !== null) return;
    const request = buildRequest('system', 'read_reboot_settings', {});
    const result = sendPayload(request);
    if (result.ok) settingsRequest = request.requestId;
    else settingsTimer = schedule(loadSettings, 2000);
  }

  return {
    canStart,
    isActive: () => pending !== null,
    onConnected() {
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
      if (pending.phase === 'returning') { closeTransport(); return true; }
      pending.disconnected = true;
      waitForReturn();
      closeTransport();
      return true;
    },
    requestReboot(definition) {
      if (!canStart()) return false;
      const request = buildRequest(definition.group, definition.action, {});
      begin('reboot', request.requestId, 'waiting');
      if (!sendPayload(request, { expectResponse: false }).ok) { clear(); return false; }
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
      if (canStart()) begin('factory_reset', -1, 'waiting');
    },
    onMessage(message) {
      if (message.requestId === settingsRequest && message.final !== false) {
        settingsRequest = null;
        const values = message.parameters;
        if (message.ok && message.type === 'system.read_reboot_settings.result' &&
            validWait(values?.wait_timeout_seconds) && typeof values.boot_id === 'string' &&
            (values.boot_id === '' || uuid.test(values.boot_id))) {
          settings = values;
        } else {
          settings = null;
          settingsTimer = schedule(loadSettings, 2000);
        }
        onChange();
        return true;
      }
      if (!pending || String(message.requestId) !== String(pending.requestId) || message.final === false) return false;
      if (!message.ok && !pending.disconnected) {
        const failure = pending.kind === 'factory_reset' && message.parameters?.error === 'factory_reset_failed'
          ? { ...pending, expires: now() + pending.waitSeconds * 1000 } : null;
        clear();
        recentFailure = failure;
      } else if (pending.kind === 'factory_reset' && message.parameters?.reboot_expected === true) {
        waitForReturn();
      }
      return false;
    },
    onRequestTimeout({ requestId }) {
      if (settingsRequest !== null && String(requestId) === String(settingsRequest)) {
        settingsRequest = null;
        settingsTimer = schedule(loadSettings, 2000);
        return true;
      }
      if (pending && String(requestId) === String(pending.requestId)) {
        if (pending.phase !== 'returning') waitForReturn();
        return true;
      }
      return false;
    },
    resume() {
      if (!pending) {
        try {
          const value = JSON.parse(storage?.getItem(storageKey) || 'null');
          if (value?.version === 1 && ['reboot', 'factory_reset'].includes(value.kind) &&
              ['resetting', 'waiting', 'returning'].includes(value.phase) && validWait(value.waitSeconds) &&
              typeof value.bootId === 'string' && (value.bootId === '' || uuid.test(value.bootId)) &&
              (value.waitStarted === null || (Number.isFinite(value.waitStarted) && value.waitStarted >= 0)) &&
              Number.isFinite(value.requestId)) pending = value;
        } catch { /* Ignore malformed or inaccessible saved state. */ }
      }
      if (!pending) return 'none';
      if (pending.phase === 'returning') { show(); return 'authenticate'; }
      waitForReturn();
      return 'waiting';
    },
    acceptReturnedStatus(status) {
      if (!pending || pending.phase !== 'returning' || !uuid.test(pending.bootId) ||
          !uuid.test(status.bootId) || status.bootId === pending.bootId || status.authenticated !== false ||
          typeof status.setupRequired !== 'boolean') return false;
      clear();
      return true;
    },
    retryRecovery: waitForReturn,
    manualReload() { clear(); reload(); },
    destroy() { cancelProbe(); cancelTimer(settingsTimer); }
  };
}

export function createRebootOverlay(appRoot, onReload) {
  let element = null;
  let previousFocus = null;
  return (state) => {
    if (!state) {
      element?.remove();
      element = null;
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
      element.addEventListener('keydown', (event) => {
        if (event.key === 'Tab') {
          event.preventDefault();
          const button = element.querySelector('button');
          (button.hidden ? element.querySelector('section') : button).focus();
        }
      });
      element.querySelector('section').focus();
    }
    const resetting = state.phase === 'resetting';
    element.querySelector('h2').textContent = resetting ? 'Factory reset in progress' : 'Device rebooting';
    const message = resetting
      ? 'The device will reboot automatically after factory reset. You will need to log in again.'
      : state.timedOut
        ? 'The device is taking longer than expected to return. Automatic checks will continue. You can also reload manually.'
        : 'Waiting for the device to return. The UI will reload automatically. You will need to log in again.';
    if (element.querySelector('p').textContent !== message) element.querySelector('p').textContent = message;
    element.querySelector('button').hidden = !state.timedOut;
  };
}
