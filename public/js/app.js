// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { mountPasswordReset } from './ui/passwordReset.js';
import { formatAuthError } from './ui/authErrors.js';

import { eyeIcon } from './ui/eyeIcon.js';
import { createTransport } from './transport.js';
import { createRebootRecovery, createRebootOverlay, probeAuthStatus } from './ui/rebootRecovery.js';
import { renderLayout } from './ui/layout.js';
import { appendLog, state } from './state.js';
import { setUiLog, bindUiLogResize } from './ui/uiLog.js';
import { renderEverestPage } from './pages/everest.js';
import { renderSafetyPage } from './pages/safety.js';
import { renderOcppPage } from './pages/ocpp.js';
import {
  handlePcapConnectionChange,
  handlePcapBinaryMessage,
  handlePcapMessage,
  handlePcapRequestTimeout,
  isPcapMessage,
  renderPcapPage
} from './pages/pcap.js';
import { renderFirmwarePage } from './pages/firmware.js';
import { renderSystemLogsPage } from './pages/systemLogs.js';
import { renderNetworkPage } from './pages/network.js';
import { renderSshPage } from './pages/ssh.js';
import { MODULE_IDS } from './protocol/constants.js';
import { buildRequest } from './protocol/requestBuilder.js';

let disposePasswordReset = () => {};
let rebootRecovery = null;
let currentAppContext = null;

function getRebootRecovery() {
  if (!rebootRecovery) {
    const render = createRebootOverlay(createAppRoot(), () => rebootRecovery.manualReload());
    rebootRecovery = createRebootRecovery({
      sendPayload: (...args) => currentAppContext?.transport?.sendPayload(...args) || { ok: false },
      closeTransport: () => currentAppContext?.transport?.close(),
      render,
      onChange: () => currentAppContext?.state.page?.onRebootAvailabilityChange?.()
    });
  }
  return rebootRecovery;
}

async function init() {
  const appRoot = createAppRoot();
  const recovery = getRebootRecovery();
  if (recovery.resume() === 'waiting') return;
  let status;
  try {
    status = await readAuthStatus();
  } catch (error) {
    if (recovery.isActive()) { recovery.retryRecovery(); return; }
    throw error;
  }
  if (recovery.isActive() && !recovery.acceptReturnedStatus(status)) {
    recovery.retryRecovery();
    return;
  }
  const appTitle = normalizeAppTitle(status.appTitle);
  if (status.setupRequired) {
    renderAuthGate(appRoot, 'setup', appTitle);
    return;
  }
  if (!status.authenticated) {
    renderAuthGate(appRoot, 'login', appTitle, '', status.passwordReset);
    return;
  }
  if (status.uiBusy) {
    renderBusyGate(appRoot, appTitle);
    return;
  }
  await startAuthenticatedApp(appRoot, appTitle);
}

async function startAuthenticatedApp(appRoot, appTitle) {
  disposePasswordReset();
  document.title = appTitle;
  const appContext = await initializeApp(appRoot, appTitle);
  currentAppContext = appContext;
  startAppRuntime(appContext);
}

async function initializeApp(appRoot, appTitle) {
  const appLayout = createLayout(appRoot);
  bindUiLogResize(appLayout.uiLogResizeHandle, appLayout.uiLog);

  return {
    layout: appLayout,
    transport: null,
    parameterCatalog: await readParameterCatalog(),
    routes: createRoutes(),
    appTitle,
    state: {
      activeRoute: 'everest',
      initialRoute: 'everest',
      page: null
    }
  };
}

function createAppRoot() {
  return document.getElementById('app');
}

async function readAuthStatus() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3000);
  try { return await probeAuthStatus(controller.signal); }
  finally { clearTimeout(timeout); }
}

function normalizeAppTitle(appTitle) {
  return typeof appTitle === 'string' && appTitle.trim() !== ''
    ? appTitle.trim()
    : 'EVerest WebUI';
}

function escapeHtml(value) {
  return value.replace(/[&<>"']/g, (character) => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  }[character]));
}

function renderAuthGate(appRoot, mode, appTitle = 'EVerest WebUI', message = '', resetStatus = null) {
  disposePasswordReset();
  const title = mode === 'setup' ? `Create ${appTitle} user` : `${appTitle} Login`;
  const button = mode === 'setup' ? 'Create user' : 'Login';
  document.title = title;
  appRoot.innerHTML = `
    <div class="auth-shell">
      <div class="auth-frame">
        <form class="auth-panel" id="auth-form">
          <div class="auth-brand">
            <img class="auth-logo" src="assets/chargebyte_logo.jpg" alt="chargebyte logo" />
          </div>
          <div class="auth-heading">
            <h1>${escapeHtml(title)}</h1>
          </div>
          <div class="auth-field">
            <label for="auth-username">Username</label>
            <input id="auth-username" name="username" autocomplete="username" required />
          </div>
          <div class="auth-field">
            <label for="auth-password">Password</label>
            <div class="auth-password-field">
              <input id="auth-password" name="password" type="password" autocomplete="${mode === 'setup' ? 'new-password' : 'current-password'}" required />
              <button class="auth-password-reveal" id="auth-password-reveal" type="button"
                aria-label="Show password while pressed" aria-controls="auth-password" title="Show password while pressed">${eyeIcon}</button>
            </div>
          </div>
          ${mode === 'login' ? '<div id="password-reset"></div>' : ''}
          <p class="auth-error" id="auth-error"></p>
          <button class="auth-button" type="submit">${button}</button>
        </form>
      </div>
    </div>
  `;

  if (mode === 'login') {
    disposePasswordReset = mountPasswordReset(appRoot.querySelector('#password-reset'), resetStatus, {
      readStatus: readAuthStatus,
      reset: () => sendAuthRequest('/auth/reset', { confirm: true }),
      onSetup: () => renderAuthGate(appRoot, 'setup', appTitle)
    });
  }
  const errorNode = appRoot.querySelector('#auth-error');
  const passwordInput = appRoot.querySelector('#auth-password');
  const revealButton = appRoot.querySelector('#auth-password-reveal');
  function setPasswordVisible(visible) {
    passwordInput.type = visible ? 'text' : 'password';
  }
  revealButton.addEventListener('pointerdown', (event) => {
    if (event.button === 0 && event.isPrimary) {
      setPasswordVisible(true);
    }
  });
  ['pointerup', 'pointerleave', 'pointercancel', 'blur'].forEach((eventName) => {
    revealButton.addEventListener(eventName, () => setPasswordVisible(false));
  });
  revealButton.addEventListener('keydown', (event) => {
    if (event.key === ' ' || event.key === 'Enter') {
      event.preventDefault();
      setPasswordVisible(true);
    }
  });
  revealButton.addEventListener('keyup', (event) => {
    if (event.key === ' ' || event.key === 'Enter') {
      event.preventDefault();
      setPasswordVisible(false);
    }
  });
  errorNode.textContent = message;
  appRoot.querySelector('#auth-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    setPasswordVisible(false);
    errorNode.textContent = '';
    const form = event.currentTarget;
    const username = form.elements.username.value.trim();
    const password = form.elements.password.value;
    try {
      if (mode === 'setup') {
        await sendAuthRequest('/auth/setup', { username, password });
      }
      await sendAuthRequest('/auth/login', { username, password });
      const status = await readAuthStatus();
      const effectiveTitle = normalizeAppTitle(status.appTitle || appTitle);
      if (status.uiBusy) {
        renderBusyGate(appRoot, effectiveTitle);
        return;
      }
      await startAuthenticatedApp(appRoot, effectiveTitle);
    } catch (error) {
      errorNode.textContent = formatAuthError(error.message);
    }
  });
}

function renderBusyGate(appRoot, appTitle = 'EVerest WebUI') {
  disposePasswordReset();
  document.title = `${appTitle} Busy`;
  appRoot.innerHTML = `
    <div class="auth-shell">
      <div class="auth-frame">
        <div class="auth-panel auth-panel--info">
          <div class="auth-brand">
            <img class="auth-logo" src="assets/chargebyte_logo.jpg" alt="chargebyte logo" />
          </div>
          <div class="auth-heading">
            <h1>${escapeHtml(appTitle)}</h1>
          </div>
          <p class="auth-info">
            This Web UI is currently in use from another browser session.
          </p>
          <p class="auth-info auth-info-soft">
            Close the active session or reload this page later.
          </p>
          <button class="auth-button" id="busy-reload-button" type="button">Reload</button>
        </div>
      </div>
    </div>
  `;

  appRoot.querySelector('#busy-reload-button')?.addEventListener('click', () => {
    window.location.reload();
  });
}

async function sendAuthRequest(path, payload) {
  const response = await fetch(path, {
    method: 'POST',
    credentials: 'same-origin',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(payload)
  });
  if (response.ok) {
    return response.json();
  }

  let error = `HTTP ${response.status}`;
  try {
    const body = await response.json();
    if (typeof body.error === 'string' && body.error.trim() !== '') {
      error = body.error;
    }
  } catch (_) {
    // Keep the HTTP status fallback.
  }
  throw new Error(error);
}

function createLayout(appRoot) {
  return renderLayout(appRoot);
}

function createAppTransport(appContext) {
  return createTransport({
    wsPath: '/ws',
    onOpen() {
      state.connection.connected = true;
      getRebootRecovery().onConnected();
      handlePcapConnectionChange(true);
      const networkRequest = buildRequest(MODULE_IDS.NETWORK, 'read_interfaces', {
        expert_mode: { backend_path: 'expert_mode', value_type: 'boolean', value: state.network.expertMode }
      });
      state.network.interfacesRequestPending = true;
      const networkResult = appContext.transport.sendPayload(networkRequest);
      if (!networkResult.ok) {
        state.network.interfacesRequestPending = false;
      }
      appContext.state.page?.onConnectionChange?.(true);
    },
    onClose(event) {
      state.connection.connected = false;
      state.network.interfacesRequestPending = false;
      handlePcapConnectionChange(false);
      const expectedReboot = getRebootRecovery().onDisconnected();
      appContext.state.page?.onConnectionChange?.(false);
      if (expectedReboot) return;
      if (event?.reason === 'credentials reset' || event?.reason === 'session expired') {
        appContext.transport?.close();
        init().catch(() => window.location.reload());
        return;
      }
      if (event?.reason === 'ui already in use') {
        renderBusyGate(createAppRoot(), appContext.appTitle);
      }
    },
    onMessage(message) {
      if (getRebootRecovery().onMessage(message)) return;
      if (isPcapMessage(message)) {
        handlePcapMessage(message, (logMessage) => addLog(appContext, logMessage));
      }
      if (message.type === 'network.read_interfaces.result') {
        state.network.interfacesRequestPending = false;
        const available = message.parameters?.available !== false;
        state.network.available = available;
        state.network.interfaces = Array.isArray(message.parameters?.interfaces)
          ? message.parameters.interfaces
          : [];
        appContext.layout.setPageAvailable('network', available);
        if (!available && appContext.state.activeRoute === MODULE_IDS.NETWORK) {
          setActiveRoute(appContext, appContext.state.initialRoute);
          renderRoute(appContext, appContext.state.initialRoute);
        }
      } else if (message.type === 'network.read_interfaces.error') {
        state.network.interfacesRequestPending = false;
      }
      appContext.state.page?.onMessage?.(message);
    },
    onBinaryMessage(data, transportApi) {
      handlePcapBinaryMessage(data, (logMessage) => addLog(appContext, logMessage), transportApi);
      appContext.state.page?.onPcapStateChange?.();
    },
    onRequestTimeout({ requestId, moduleAction }) {
      if (getRebootRecovery().onRequestTimeout({ requestId, moduleAction })) return;
      addLog(appContext, `request timeout: ${moduleAction} (${requestId})`);
      if (handlePcapRequestTimeout(requestId, moduleAction)) {
        appContext.state.page?.onPcapStateChange?.();
      }
      appContext.state.page?.onRequestTimeout?.({ requestId, moduleAction });
    }
  });
}

async function readParameterCatalog() {
  const response = await fetch('/config/parameter_catalog.json', {
    cache: 'no-cache',
    credentials: 'same-origin'
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

function createRoutes() {
  return {
    everest: renderEverestPage,
    safety: renderSafetyPage,
    ocpp: renderOcppPage,
    pcap: renderPcapPage,
    firmware: renderFirmwarePage,
    system_logs: renderSystemLogsPage,
    network: renderNetworkPage,
    ssh: renderSshPage
  };
}

function startAppRuntime(appContext) {
  appContext.transport = createAppTransport(appContext);
  bindLogout(appContext);
  setupNavTracking(appContext);
  handleInitialRender(appContext);
  connectTransport(appContext);
}

function bindLogout(appContext) {
  appContext.layout.logoutButton?.addEventListener('click', async () => {
    appContext.transport?.close();
    try {
      const response = await fetch('/auth/logout', {
        method: 'POST',
        credentials: 'same-origin'
      });
      if (!response.ok) throw new Error(`Logout failed: HTTP ${response.status}`);
      await init();
    } catch (_) {
      window.location.reload();
    }
  });
}

function setupNavTracking(appContext) {
  bindNavClicks(appContext);
  updateActiveNav(appContext);
}

function handleInitialRender(appContext) {
  renderRoute(appContext, appContext.state.initialRoute);
}

function connectTransport(appContext) {
  appContext.transport.connect();
}

function bindNavClicks(appContext) {
  appContext.layout.navItems.forEach((item) => {
    item.addEventListener('click', () => {
      const route = item.dataset.page;
      if (!route || route === appContext.state.activeRoute) {
        return;
      }
      setActiveRoute(appContext, route);
      renderRoute(appContext, route);
    });
  });
}

function setActiveRoute(appContext, route) {
  appContext.state.activeRoute = route;
  updateActiveNav(appContext);
}

function renderRoute(appContext, route) {
  appContext.state.page?.destroy?.();

  const renderPage = appContext.routes[route];
  if (!renderPage) {
    throw new Error(`Unsupported route: ${route}`);
  }

  appContext.state.page = renderPage(appContext.layout.pageOutlet, {
    parameterCatalog: appContext.parameterCatalog,
    appTitle: appContext.appTitle,
    rebootRecovery: getRebootRecovery(),
    sendPayload(payload) {
      return appContext.transport.sendPayload(payload);
    },
    addLog(message) {
      addLog(appContext, message);
    }
  });

  if (appContext.transport?.isOpen()) {
    appContext.state.page?.onConnectionChange?.(true);
  }
}

function updateActiveNav(appContext) {
  appContext.layout.navItems.forEach((item) => {
    item.classList.toggle('active', item.dataset.page === appContext.state.activeRoute);
  });
}

function addLog(appContext, message) {
  appendLog(message);
  setUiLog(appContext.layout.uiLog, state.uiLog.lines);
}

init();
