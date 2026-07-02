// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { createTransport } from './transport.js';
import { renderLayout } from './ui/layout.js';
import { appendLog, state } from './state.js';
import { setSystemLog, bindSystemLogResize } from './ui/systemLog.js';
import { renderEverestPage } from './pages/everest.js';
import { renderSafetyPage } from './pages/safety.js';
import { renderOcppPage } from './pages/ocpp.js';
import { renderPcapPage } from './pages/pcap.js';
import { renderFirmwarePage } from './pages/firmware.js';
import { renderErrorLogsPage } from './pages/errorLogs.js';

async function init() {
  const appRoot = createAppRoot();
  const status = await readAuthStatus();
  if (status.setupRequired) {
    renderAuthGate(appRoot, 'setup');
    return;
  }
  if (!status.authenticated) {
    renderAuthGate(appRoot, 'login');
    return;
  }
  await startAuthenticatedApp(appRoot);
}

async function startAuthenticatedApp(appRoot) {
  const appContext = await initializeApp(appRoot);
  startAppRuntime(appContext);
}

async function initializeApp(appRoot) {
  const appLayout = createLayout(appRoot);
  bindSystemLogResize(appLayout.systemLogResizeHandle, appLayout.systemLog);

  return {
    layout: appLayout,
    transport: null,
    parameterCatalog: await readParameterCatalog(),
    routes: createRoutes(),
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
  const response = await fetch('/auth/status', {
    cache: 'no-cache',
    credentials: 'same-origin'
  });
  if (!response.ok) {
    throw new Error(`Auth status failed: HTTP ${response.status}`);
  }
  return response.json();
}

function renderAuthGate(appRoot, mode, message = '') {
  const title = mode === 'setup' ? 'Create EVerest WebUI user' : 'EVerest WebUI Login';
  const button = mode === 'setup' ? 'Create user' : 'Login';
  appRoot.innerHTML = `
    <div class="auth-shell">
      <div class="auth-frame">
        <form class="auth-panel" id="auth-form">
          <div class="auth-brand">
            <img class="auth-logo" src="assets/chargebyte_logo.jpg" alt="chargebyte logo" />
          </div>
          <div class="auth-heading">
            <h1>${title}</h1>
          </div>
          <div class="auth-field">
            <label for="auth-username">Username</label>
            <input id="auth-username" name="username" autocomplete="username" required />
          </div>
          <div class="auth-field">
            <label for="auth-password">Password</label>
            <input id="auth-password" name="password" type="password" autocomplete="${mode === 'setup' ? 'new-password' : 'current-password'}" required />
          </div>
          <p class="auth-error" id="auth-error"></p>
          <button class="auth-button" type="submit">${button}</button>
        </form>
      </div>
    </div>
  `;

  const errorNode = appRoot.querySelector('#auth-error');
  errorNode.textContent = message;
  appRoot.querySelector('#auth-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    errorNode.textContent = '';
    const form = event.currentTarget;
    const username = form.elements.username.value.trim();
    const password = form.elements.password.value;
    try {
      if (mode === 'setup') {
        await sendAuthRequest('/auth/setup', { username, password });
      }
      await sendAuthRequest('/auth/login', { username, password });
      await startAuthenticatedApp(appRoot);
    } catch (error) {
      errorNode.textContent = formatAuthError(error.message);
    }
  });
}

function formatAuthError(error) {
  const messages = {
    invalid_credentials: 'Username or password is incorrect.',
    missing_credentials: 'Enter a username and password.',
    invalid_json: 'The login request could not be processed.',
    setup_not_required: 'A WebUI user already exists.',
    setup_required: 'Create the WebUI user before logging in.',
    'Invalid username': 'Use only letters, numbers, dots, dashes, or underscores for the username.',
    'Invalid password': 'Use a password with at least 8 characters.'
  };

  return messages[error] || error || 'Authentication failed.';
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
      appContext.state.page?.onConnectionChange?.(true);
    },
    onClose() {
      state.connection.connected = false;
      appContext.state.page?.onConnectionChange?.(false);
    },
    onMessage(message) {
      appContext.state.page?.onMessage?.(message);
    },
    onRequestTimeout({ requestId, moduleAction }) {
      addLog(appContext, `request timeout: ${moduleAction} (${requestId})`);
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
    logs: renderErrorLogsPage
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
    await fetch('/auth/logout', {
      method: 'POST',
      credentials: 'same-origin'
    });
    renderAuthGate(createAppRoot(), 'login');
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
    sendPayload(payload) {
      return appContext.transport.sendPayload(payload).ok;
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
  setSystemLog(appContext.layout.systemLog, state.logs.lines);
}

init();
