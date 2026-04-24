// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { renderFilesDownloadBlock } from '../ui/filesDownload.js';

const kDevelopmentLogFiles = [
  { name: 'error_2026_02_18.log', size: '1.2 MB', lastModified: '18.02.2026 14:22' },
  { name: 'error_2026_02_19.log', size: '980 KB', lastModified: '19.02.2026 09:15' },
  { name: 'system_2026_02_19.log', size: '2.4 MB', lastModified: '19.02.2026 17:41' },
  { name: 'safety_2026_02_20.log', size: '640 KB', lastModified: '20.02.2026 08:03' },
  { name: 'ocpp_2026_02_20.log', size: '1.1 MB', lastModified: '20.02.2026 11:37' }
];

export function renderErrorLogsPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  const pageConfig = loadPageConfig(MODULE_IDS.LOGS, parameterCatalog);
  const filesDownloadBlock = pageConfig.blocks.find((block) => block.kind === 'files_download');

  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const filesDownload = renderFilesDownloadBlock(filesDownloadBlock, {
    buttonLabel: 'Download Selected'
  });

  filesDownload.setFiles(kDevelopmentLogFiles);
  filesDownload.bindDownload((selectedFiles) => {
    addLog(`Error logs download triggered (${selectedFiles.length} file(s), placeholder)`);
  });

  pageElement.appendChild(filesDownload.element);
  container.appendChild(pageElement);

  return {
    onMessage() {},
    onConnectionChange(connected) {
      // request current Error Logs after page is loaded and WS is connected
      if (connected === true) {
        const readLogsRequest = buildRequest(
          pageConfig.actions.read.group,
          pageConfig.actions.read.action,
          {}
        );
        console.log(readLogsRequest);
        sendLogsRequest(
          sendPayload,
          addLog,
          readLogsRequest,
          pageConfig.actions.read.group,
          pageConfig.actions.read.action
        );
      }
    },
    destroy() {}
  };
}

function sendLogsRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
