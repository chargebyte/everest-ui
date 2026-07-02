// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderTopBar() {
  return `
    <header class="app-header">
      <img class="brand-logo" src="assets/chargebyte_logo.jpg" alt="chargebyte logo" />
      <button id="logout-button" class="logout-button" type="button">Logout</button>
    </header>
  `;
}
