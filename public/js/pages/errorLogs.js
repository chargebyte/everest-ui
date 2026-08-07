// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { renderFilesDownloadBlock } from '../ui/filesDownload.js';
import { renderJournalExtractBlock } from '../ui/journalExtract.js';

export function renderErrorLogsPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  const pageConfig = loadPageConfig(MODULE_IDS.LOGS, parameterCatalog);
  const filesDownloadBlock = pageConfig.blocks.find((block) => block.kind === 'files_download');
  const journalExtractBlock = pageConfig.blocks.find((block) => block.kind === 'journal_extract');

  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const filesDownload = renderFilesDownloadBlock(filesDownloadBlock, {
    buttonLabel: 'Download Selected'
  });
  const journalExtract = renderJournalExtractBlock(journalExtractBlock);

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

  journalExtract.bindGenerate((parameters) => {
    const extractRequest = buildRequest(
      pageConfig.actions.extract.group,
      pageConfig.actions.extract.action,
      createRequestParameters(parameters)
    );
    sendLogsRequest(
      sendPayload,
      addLog,
      extractRequest,
      pageConfig.actions.extract.group,
      pageConfig.actions.extract.action
    );
  });

  pageElement.appendChild(filesDownload.element);
  pageElement.appendChild(journalExtract.element);
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

      if (message.type === 'logs.extract.result') {
        addLog('logs.extract.result received');
        journalExtract.setResult(message.parameters || {});
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

      if (message.type === 'logs.extract.error') {
        const error = message.parameters?.error;
        addLog(`logs.extract.error: ${error}`);
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

function createRequestParameters(parameters) {
  return {
    boot: {
      backend_path: 'boot',
      value_type: 'string',
      value: parameters.boot
    },
    service: {
      backend_path: 'service',
      value_type: 'boolean',
      value: parameters.service
    },
    output: {
      backend_path: 'output',
      value_type: 'string',
      value: parameters.output
    }
  };
}

function sendLogsRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
