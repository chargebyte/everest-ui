// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { state } from '../state.js';
import { renderCaptureBlock } from '../ui/capture.js';

export function renderPcapPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  const pageConfig = loadPageConfig(MODULE_IDS.PCAP, parameterCatalog);
  const captureBlock = pageConfig.blocks.find((block) => block.kind === 'capture');
  const pcapState = state.pcap;

  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const capture = renderCaptureBlock(captureBlock);

  function syncCaptureView() {
    capture.setViewState(pcapState);
  }

  function clearLastCaptureResult() {
    if (pcapState.lastPcapUrl) {
      URL.revokeObjectURL(pcapState.lastPcapUrl);
      pcapState.lastPcapUrl = null;
      pcapState.lastPcapName = '';
    }
  }

  function setCaptureResult(parameters) {
    clearLastCaptureResult();

    const dataB64 = parameters.dataB64 || '';
    if (dataB64) {
      pcapState.lastPcapUrl = toBlobUrl(dataB64);
      pcapState.lastPcapName = fileNameFromPath(parameters.file) || `pcap_${Date.now()}.pcap`;
    }

    pcapState.recordingState = 'idle';
    pcapState.captureStartTs = null;
    pcapState.interfaceName = '';
    pcapState.captureValues = null;
  }

  function resetCaptureState() {
    pcapState.recordingState = 'idle';
    pcapState.captureStartTs = null;
    pcapState.interfaceName = '';
    pcapState.captureValues = null;
  }

  capture.bindSubmit(() => {
    clearLastCaptureResult();
    const values = capture.getValues(capture.requestResponseObject);
    pcapState.interfaceName = values.interface?.value || '';
    pcapState.captureValues = values;
    syncCaptureView();

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

  capture.bindDownload(() => {
    if (!pcapState.lastPcapUrl) {
      return;
    }

    const link = document.createElement('a');
    link.href = pcapState.lastPcapUrl;
    link.download = pcapState.lastPcapName || `pcap_${Date.now()}.pcap`;
    link.click();
  });

  capture.bindStop(() => {
    pcapState.recordingState = 'processing';
    syncCaptureView();

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
      pcapState.recordingState = 'running';
      syncCaptureView();
    }
  });

  pageElement.appendChild(capture.element);
  container.appendChild(pageElement);

  pcapState.connected = state.connection.connected === true;
  if (isCaptureActiveState(pcapState) && pcapState.captureValues) {
    capture.setValues(pcapState.captureValues);
  } else {
    pcapState.interfaceName = '';
  }
  capture.setInterfaceOptions(pcapState.interfaces);
  syncCaptureView();

  return {
    onMessage(message) {
      if (message.type === 'pcap.read_interfaces.result' && message.ok === true) {
        pcapState.interfaces = message.parameters?.interfaces || [];
        capture.setInterfaceOptions(pcapState.interfaces);
        if (isCaptureActiveState(pcapState) && pcapState.captureValues) {
          capture.setValues(pcapState.captureValues);
        }
        addLog('pcap.read_interfaces.result received');
        syncCaptureView();
        return;
      }

      if (message.type === 'pcap.write.ack' && message.ok === true) {
        addLog('pcap.write.ack received');
        pcapState.recordingState = 'running';
        pcapState.captureStartTs = Date.now();
        syncCaptureView();
        return;
      }

      if (message.type === 'pcap.read.result' && message.ok === true) {
        addLog('pcap.read.result received');
        setCaptureResult(message.parameters || {});
        syncCaptureView();
        return;
      }

      if (message.type === 'pcap.write.error') {
        const error = message.parameters?.error;
        const details = message.parameters?.details;
        addLog(`pcap.write.error: ${error}${details ? ` (${details})` : ''}`);
        resetCaptureState();
        syncCaptureView();
        return;
      }

      if (message.type === 'pcap.read.error') {
        const error = message.parameters?.error;
        addLog(`pcap.read.error: ${error}`);
        resetCaptureState();
        syncCaptureView();
        return;
      }

      if (message.type === 'pcap.read_interfaces.error') {
        addLog(`pcap.read_interfaces.error: ${message.parameters?.error || 'unknown_error'}`);
        return;
      }

      if (message.ok === false) {
        const error = message.parameters?.error || message.error || 'unknown_error';
        addLog(`pcap backend error: ${error}`);
        resetCaptureState();
        syncCaptureView();
      }
    },
    onConnectionChange(connected) {
      pcapState.connected = connected === true;
      if (pcapState.connected) {
        requestInterfaces();
      }
      if (!pcapState.connected && pcapState.recordingState === 'processing') {
        pcapState.recordingState = 'idle';
        pcapState.captureStartTs = null;
        pcapState.interfaceName = '';
      }
      syncCaptureView();
    },
    destroy() {
      capture.destroy();
    }
  };

  function requestInterfaces() {
    const request = buildRequest(
      pageConfig.actions.read_interfaces.group,
      pageConfig.actions.read_interfaces.action,
      {}
    );
    sendPcapRequest(
      sendPayload,
      addLog,
      request,
      pageConfig.actions.read_interfaces.group,
      pageConfig.actions.read_interfaces.action
    );
  }
}

function sendPcapRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}

function isCaptureActiveState(pcapState) {
  return pcapState.recordingState === 'running' || pcapState.recordingState === 'processing';
}

function toBlobUrl(base64Data) {
  const bytes = atob(base64Data || '');
  const out = new Uint8Array(bytes.length);
  for (let index = 0; index < bytes.length; index += 1) {
    out[index] = bytes.charCodeAt(index);
  }
  const blob = new Blob([out], { type: 'application/octet-stream' });
  return URL.createObjectURL(blob);
}

function fileNameFromPath(filePath) {
  if (!filePath) {
    return '';
  }

  return String(filePath).split(/[\\/]/).filter(Boolean).pop() || '';
}
