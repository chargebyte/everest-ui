// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderSideBar } from './sideBar.js';
import { renderUiLog } from './uiLog.js';
import { renderTopBar } from './topBar.js';

export function renderLayout(root) {
  root.innerHTML = `
    <div class="app-shell">
      ${renderTopBar()}
      ${renderSideBar()}

      <section class="content">
        <div id="page-outlet"></div>
      </section>

      ${renderUiLog()}
    </div>
  `;

  return {
    pageOutlet: root.querySelector('#page-outlet'),
    logoutButton: root.querySelector('#logout-button'),
    uiLog: root.querySelector('#ui-log'),
    uiLogResizeHandle: root.querySelector('#ui-log-resize-handle'),
    navItems: root.querySelectorAll('.nav-item')
  };
}
