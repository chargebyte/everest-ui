// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { kNoopPageLifecycle } from '../ui/pageLifecycle.js';

export function renderErrorLogsPage(container, { addLog }) {
  container.innerHTML = `
    <div class="page">
      <h1>Error Logs</h1>

      <section class="section">
        <h2>Available Log Files</h2>

        <div class="logs-table-wrap">
          <table class="logs-table">
            <thead>
              <tr>
                <th><input id="logs-select-all" type="checkbox" /></th>
                <th>File Name</th>
                <th>Size</th>
                <th>Last Modified</th>
              </tr>
            </thead>
            <tbody>
              <tr>
                <td><input class="log-row-check" type="checkbox" /></td>
                <td>error_2026_02_18.log</td>
                <td>1.2 MB</td>
                <td>18.02.2026 14:22</td>
              </tr>
              <tr>
                <td><input class="log-row-check" type="checkbox" /></td>
                <td>error_2026_02_19.log</td>
                <td>980 KB</td>
                <td>19.02.2026 09:15</td>
              </tr>
              <tr>
                <td><input class="log-row-check" type="checkbox" /></td>
                <td>system_2026_02_19.log</td>
                <td>2.4 MB</td>
                <td>19.02.2026 17:41</td>
              </tr>
              <tr>
                <td><input class="log-row-check" type="checkbox" /></td>
                <td>safety_2026_02_20.log</td>
                <td>640 KB</td>
                <td>20.02.2026 08:03</td>
              </tr>
              <tr>
                <td><input class="log-row-check" type="checkbox" /></td>
                <td>ocpp_2026_02_20.log</td>
                <td>1.1 MB</td>
                <td>20.02.2026 11:37</td>
              </tr>
            </tbody>
          </table>
        </div>
      </section>

      <div class="controls settings-action-controls">
        <button id="download-logs" class="btn btn-start">Download Selected</button>
      </div>
    </div>
  `;

  const selectAll = container.querySelector('#logs-select-all');
  const rowChecks = Array.from(container.querySelectorAll('.log-row-check'));
  const downloadBtn = container.querySelector('#download-logs');

  function selectedCount() {
    return rowChecks.filter((c) => c.checked).length;
  }

  function syncDownloadEnabled() {
    downloadBtn.disabled = selectedCount() === 0;
  }

  selectAll.addEventListener('change', () => {
    rowChecks.forEach((checkbox) => {
      checkbox.checked = selectAll.checked;
    });
    syncDownloadEnabled();
  });

  rowChecks.forEach((checkbox) => {
    checkbox.addEventListener('change', () => {
      const allChecked = rowChecks.every((c) => c.checked);
      selectAll.checked = allChecked;
      syncDownloadEnabled();
    });
  });

  downloadBtn.addEventListener('click', () => {
    const count = selectedCount();
    addLog(`Error logs download triggered (${count} file(s), placeholder)`);
  });

  syncDownloadEnabled();

  return kNoopPageLifecycle;
}
