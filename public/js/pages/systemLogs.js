// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { renderFilesDownloadBlock } from '../ui/filesDownload.js';
import { renderSystemLogExtractBlock } from '../ui/systemLogExtract.js';

export function renderSystemLogsPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  const pageConfig = loadPageConfig(MODULE_IDS.SYSTEM_LOGS, parameterCatalog);
  const systemLogFilesBlock = pageConfig.blocks.find((block) => block.kind === 'files_download');
  const systemLogExtractBlock = pageConfig.blocks.find((block) => block.kind === 'system_log_extract');

  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const systemLogFiles = renderFilesDownloadBlock(systemLogFilesBlock, {
    buttonLabel: 'Download Selected'
  });
  const systemLogExtract = renderSystemLogExtractBlock(systemLogExtractBlock, {
    onPopupBlocked() {
      addLog('system_logs.extract new tab blocked; displaying inline');
    }
  });

  systemLogFiles.bindDownload(() => {
    const downloadSystemLogsRequest = buildRequest(
      pageConfig.actions.download.group,
      pageConfig.actions.download.action,
      systemLogFiles.getDownloadRequestResponseObject()
    );
    sendSystemLogsRequest(
      sendPayload,
      addLog,
      downloadSystemLogsRequest,
      pageConfig.actions.download.group,
      pageConfig.actions.download.action
    );
  });

  systemLogExtract.bindGenerate((parameters) => {
    const extractSystemLogsRequest = buildRequest(
      pageConfig.actions.extract.group,
      pageConfig.actions.extract.action,
      createSystemLogExtractRequestParameters(parameters)
    );
    sendSystemLogsRequest(
      sendPayload,
      addLog,
      extractSystemLogsRequest,
      pageConfig.actions.extract.group,
      pageConfig.actions.extract.action
    );
  });

  pageElement.appendChild(systemLogFiles.element);
  pageElement.appendChild(systemLogExtract.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'system_logs.read.result') {
        addLog('system_logs.read.result received');
        systemLogFiles.setFiles(message.parameters?.files || {});
        return;
      }

      if (message.type === 'system_logs.download.result') {
        addLog('system_logs.download.result received');
        systemLogFiles.downloadFile(message.parameters || {});
        return;
      }

      if (message.type === 'system_logs.extract.result') {
        addLog('system_logs.extract.result received');
        systemLogExtract.setResult(message.parameters || {});
        return;
      }

      if (message.type === 'system_logs.read.error') {
        const error = message.parameters?.error;
        addLog(`system_logs.read.error: ${error}`);
      }

      if (message.type === 'system_logs.download.error') {
        const error = message.parameters?.error;
        addLog(`system_logs.download.error: ${error}`);
      }

      if (message.type === 'system_logs.extract.error') {
        const error = message.parameters?.error;
        systemLogExtract.clearPendingTab();
        addLog(`system_logs.extract.error: ${error}`);
      }
    },
    onConnectionChange(connected) {
      // request current System Logs after page is loaded and WS is connected
      if (connected === true) {
        const readSystemLogsRequest = buildRequest(
          pageConfig.actions.read.group,
          pageConfig.actions.read.action,
          {}
        );
        sendSystemLogsRequest(
          sendPayload,
          addLog,
          readSystemLogsRequest,
          pageConfig.actions.read.group,
          pageConfig.actions.read.action
        );
      }
    },
    destroy() {}
  };
}

function createSystemLogExtractRequestParameters(parameters) {
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

function sendSystemLogsRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
