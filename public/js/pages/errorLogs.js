// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { renderFilesDownloadBlock } from '../ui/filesDownload.js';

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

  filesDownload.bindDownload(() => {
    const downloadLogsRequest = buildRequest(
      pageConfig.actions.download.group,
      pageConfig.actions.download.action,
      filesDownload.getDownloadRequestResponseObject()
    );
    sendLogsRequest(
      sendPayload,
      addLog,
      downloadLogsRequest,
      pageConfig.actions.download.group,
      pageConfig.actions.download.action
    );
  });

  pageElement.appendChild(filesDownload.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'logs.read.result') {
        addLog('logs.read.result received');
        filesDownload.setFiles(message.parameters?.files || {});
        return;
      }

      if (message.type === 'logs.download.result') {
        addLog('logs.download.result received');
        filesDownload.downloadFile(message.parameters || {});
        return;
      }

      if (message.type === 'logs.read.error') {
        const error = message.parameters?.error;
        addLog(`logs.read.error: ${error}`);
      }

      if (message.type === 'logs.download.error') {
        const error = message.parameters?.error;
        addLog(`logs.download.error: ${error}`);
      }
    },
    onConnectionChange(connected) {
      // request current Error Logs after page is loaded and WS is connected
      if (connected === true) {
        const readLogsRequest = buildRequest(
          pageConfig.actions.read.group,
          pageConfig.actions.read.action,
          {}
        );
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
