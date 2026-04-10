// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { kNoopPageLifecycle } from '../ui/pageLifecycle.js';

export function renderFirmwarePage(container, { addLog }) {
  container.innerHTML = `
    <div class="page">
      <h1>Firmware Update</h1>

      <section class="section">
        <h2>Controller Firmware</h2>
        <div class="form-grid">
          <span class="label">Current Version:</span>
          <span class="value-text">v1.4.2</span>

          <label class="label" for="firmware-file">Selected File:</label>
          <div class="firmware-file-row">
            <input class="input" id="firmware-file" value="controller_fw_v1.5.0.bin" />
            <button id="firmware-browse" class="btn btn-secondary">Browse...</button>
          </div>
        </div>

        <div class="controls">
          <button id="firmware-start" class="btn btn-start">Start Update</button>
        </div>
      </section>
    </div>
  `;

  const start = container.querySelector('#firmware-start');
  const browse = container.querySelector('#firmware-browse');

  browse.addEventListener('click', () => {
    addLog('Firmware browse clicked (placeholder)');
  });

  start.addEventListener('click', () => {
    addLog('Firmware update started. Do not power off device. (placeholder)');
  });

  return kNoopPageLifecycle;
}
