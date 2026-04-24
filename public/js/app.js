// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { createTransport } from './transport.js';
import { renderLayout } from './ui/layout.js';
import { appendLog, state } from './state.js';
import { setSystemLog, bindSystemLogResize } from './ui/systemLog.js';
import { renderEverestPage } from './pages/everest.js';
import { renderSafetyPage } from './pages/safety.js';
import { renderErrorLogsPage } from './pages/errorLogs.js';

async function init() {
  const appContext = await initializeApp();
  startAppRuntime(appContext);
}

async function initializeApp() {
  const appRoot = createAppRoot();
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

function createLayout(appRoot) {
  return renderLayout(appRoot);
}

function createAppTransport(appContext) {
  return createTransport({
    wsPath: '/ws',
    onOpen() {
      appContext.state.page?.onConnectionChange?.(true);
    },
    onClose() {
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
  const response = await fetch('/config/parameter_catalog.json', { cache: 'no-cache' });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

function createRoutes() {
  return {
    everest: renderEverestPage,
    safety: renderSafetyPage,
    logs: renderErrorLogsPage
  };
}

function startAppRuntime(appContext) {
  appContext.transport = createAppTransport(appContext);
  setupNavTracking(appContext);
  handleInitialRender(appContext);
  connectTransport(appContext);
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
