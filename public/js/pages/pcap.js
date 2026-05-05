// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { renderCaptureBlock } from '../ui/capture.js';

export function renderPcapPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  const pageConfig = loadPageConfig(MODULE_IDS.PCAP, parameterCatalog);
  const captureBlock = pageConfig.blocks.find((block) => block.kind === 'capture');

  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const capture = renderCaptureBlock(captureBlock);

  capture.bindSubmit(() => {
    const values = capture.getValues(capture.requestResponseObject);
    const writePcapRequest = buildRequest(
      pageConfig.actions.write.group,
      pageConfig.actions.write.action,
      values
    );
    sendPcapRequest(
      sendPayload,
      addLog,
      writePcapRequest,
      pageConfig.actions.write.group,
      pageConfig.actions.write.action
    );
  });

  capture.bindStop(() => {
    capture.setRecordingState('processing');

    const readPcapRequest = buildRequest(
      pageConfig.actions.read.group,
      pageConfig.actions.read.action,
      {}
    );
    const ok = sendPcapRequest(
      sendPayload,
      addLog,
      readPcapRequest,
      pageConfig.actions.read.group,
      pageConfig.actions.read.action
    );

    if (!ok) {
      capture.setRecordingState('running');
    }
  });

  pageElement.appendChild(capture.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'pcap.write.ack' && message.ok === true) {
        addLog('pcap.write.ack received');
        capture.setRecordingState('running');
        return;
      }

      if (message.type === 'pcap.read.result' && message.ok === true) {
        addLog('pcap.read.result received');
        capture.setCaptureResult(message.parameters || message);
        return;
      }

      if (message.type === 'pcap.write.error') {
        const error = message.parameters?.error;
        addLog(`pcap.write.error: ${error}`);
        capture.reset();
        return;
      }

      if (message.type === 'pcap.read.error') {
        const error = message.parameters?.error;
        addLog(`pcap.read.error: ${error}`);
        capture.reset();
        return;
      }

      if (message.ok === false) {
        const error = message.parameters?.error || message.error || 'unknown_error';
        addLog(`pcap backend error: ${error}`);
        capture.reset();
      }
    },
    onConnectionChange(connected) {
      capture.setConnectionState(connected);
    },
    destroy() {
      capture.destroy();
    }
  };
}

function sendPcapRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  console.log(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
