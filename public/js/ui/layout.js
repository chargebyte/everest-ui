// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderSideBar } from './sideBar.js';
import { renderSystemLog } from './systemLog.js';
import { renderTopBar } from './topBar.js';

export function renderLayout(root) {
  root.innerHTML = `
    <div class="app-shell">
      ${renderTopBar()}
      ${renderSideBar()}

      <section class="content">
        <div id="page-outlet"></div>
      </section>

      ${renderSystemLog()}
    </div>
  `;

  return {
    pageOutlet: root.querySelector('#page-outlet'),
    systemLog: root.querySelector('#system-log'),
    systemLogResizeHandle: root.querySelector('#system-log-resize-handle'),
    navItems: root.querySelectorAll('.nav-item')
  };
}
