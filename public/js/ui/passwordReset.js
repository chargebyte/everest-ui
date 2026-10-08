// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

import { formatAuthError } from './authErrors.js';

export function resetHint(reason, windowSeconds = 60) {
  if (['warm_boot', 'unavailable_this_boot', 'expired'].includes(reason)) {
    return `Reset window: ${windowSeconds} seconds after the WebUI starts on cold boot. Power-cycle and reopen this page to try again.`;
  }
  if (reason === 'boot_info_invalid') return 'Password reset is unavailable because the boot information could not be read.';
  if (reason === 'setup_required') return '';
  return 'Password reset is unavailable on this device.';
}

// The server supplies the remaining duration once. Only a failed user action
// refreshes status; ticking this component never makes network requests.
export function mountPasswordReset(container, initialStatus, {
  readStatus, reset, onSetup,
  confirm = (message) => window.confirm(message),
  now = () => performance.now(),
  schedule = (callback) => setInterval(callback, 250),
  cancel = (timer) => clearInterval(timer)
}) {
  container.innerHTML = `
    <div class="password-reset-row">
      <button class="password-reset-link" type="button" data-reset-button hidden>Reset password</button>
      <span class="password-reset-label" data-reset-label>Forgotten password?</span>
      <button class="password-reset-info" type="button" data-reset-info aria-label="Password reset information" aria-describedby="password-reset-description">
        <svg viewBox="0 0 16 16" aria-hidden="true" focusable="false">
          <circle cx="8" cy="8" r="6.5"></circle>
          <path d="M8 7v4M8 4.8v.1"></path>
        </svg>
      </button>
      <span class="password-reset-sr-only" id="password-reset-description" data-reset-description></span>
    </div>
    <p class="auth-error" data-reset-error></p>`;
  const button = container.querySelector('[data-reset-button]');
  const label = container.querySelector('[data-reset-label]');
  const info = container.querySelector('[data-reset-info]');
  const description = container.querySelector('[data-reset-description]');
  const errorNode = container.querySelector('[data-reset-error]');
  let timer = null;
  let disposed = false;
  let pending = false;
  let deadline = 0;
  let status;
  const stop = () => { if (timer !== null) cancel(timer); timer = null; };

  function tick() {
    const left = Math.max(0, Math.ceil((deadline - now()) / 1000));
    const available = status.available && left > 0;
    const configuredWindow = Number(status.windowSeconds);
    const windowSeconds = Number.isFinite(configuredWindow) ? Math.max(0, configuredWindow) : 60;
    button.hidden = !available;
    label.textContent = available ? 'Forgotten password' : 'Forgotten password?';
    button.disabled = pending;
    const infoText = available
      ? `Password reset is available during the ${windowSeconds}-second window after the WebUI starts following a cold boot. ${left} seconds remain.`
      : resetHint(status.available ? 'expired' : status.reason, windowSeconds);
    info.setAttribute('data-tooltip', infoText);
    description.textContent = infoText;
    if (!left) stop();
  }

  function display(next) {
    stop();
    status = next || { available: false, reason: 'disabled' };
    deadline = now() + Math.max(0, Number(status.remainingSeconds) || 0) * 1000;
    tick();
    if (status.available && deadline > now()) timer = schedule(tick);
  }

  const click = async () => {
    if (disposed || pending || !status.available || now() >= deadline) return;
    if (!confirm('Remove the current username and password? You will then create new login credentials.')) return;
    pending = true;
    stop();
    button.disabled = true;
    errorNode.textContent = '';
    try {
      await reset();
      if (!disposed) onSetup();
    } catch (error) {
      if (disposed) return;
      const message = formatAuthError(error.message);
      errorNode.textContent = message === error.message
        ? 'Password reset failed or is no longer available.'
        : message;
      try {
        const fresh = await readStatus();
        if (disposed) return;
        if (fresh.setupRequired) { onSetup(); return; }
        pending = false;
        display(fresh.passwordReset);
      } catch (_) {
        if (!disposed) {
          pending = false;
          display({ available: false, reason: 'disabled' });
          errorNode.textContent = 'Could not refresh password reset status. Reload the page to try again.';
        }
      }
    }
  };
  button.addEventListener('click', click);
  display(initialStatus);
  return () => {
    disposed = true;
    stop();
    button.removeEventListener('click', click);
  };
}
