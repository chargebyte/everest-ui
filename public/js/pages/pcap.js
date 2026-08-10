// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { state } from '../state.js';
import { renderCaptureBlock } from '../ui/capture.js';

export function isPcapMessage(message) {
  return typeof message?.type === 'string' && message.type.startsWith('pcap.');
}

export function handlePcapMessage(message, addLog) {
  const pcapState = state.pcap;
  if (!isPcapMessage(message)) {
    return false;
  }

  if (message.type === 'pcap.read_interfaces.result' && message.ok === true) {
    pcapState.interfaces = message.parameters?.interfaces || [];
    addLog('pcap.read_interfaces.result received');
    return true;
  }

  if (message.type === 'pcap.write.ack' && message.ok === true) {
    if (!requestMatches(message, pcapState.activeCaptureRequestId)) {
      return true;
    }
    addLog('pcap.write.ack received');
    pcapState.recordingState = 'running';
    pcapState.captureStartTs = Date.now();
    return true;
  }

  if (message.type === 'pcap.read.result' && message.ok === true) {
    if (!requestMatches(message, pcapState.pendingReadRequestId)) {
      return true;
    }
    addLog('pcap.read.result received');
    setCaptureResult(message.parameters || {});
    return true;
  }

  if (message.type === 'pcap.write.error') {
    if (!requestMatches(message, pcapState.activeCaptureRequestId)) {
      return true;
    }
    const error = message.parameters?.error;
    const details = message.parameters?.details;
    addLog(`pcap.write.error: ${error}${details ? ` (${details})` : ''}`);
    if (error === 'pcap_limit_reached') {
      pcapState.recordingState = 'ready';
      return true;
    }
    resetCaptureState();
    return true;
  }

  if (message.type === 'pcap.read.error') {
    if (!requestMatches(message, pcapState.pendingReadRequestId)) {
      return true;
    }
    addLog(`pcap.read.error: ${message.parameters?.error}`);
    resetCaptureState();
    return true;
  }

  if (message.type === 'pcap.read_interfaces.error') {
    addLog(`pcap.read_interfaces.error: ${message.parameters?.error || 'unknown_error'}`);
    return true;
  }

  if (message.ok === false &&
      (requestMatches(message, pcapState.activeCaptureRequestId) ||
       requestMatches(message, pcapState.pendingReadRequestId))) {
    const error = message.parameters?.error || message.error || 'unknown_error';
    addLog(`pcap backend error: ${error}`);
    resetCaptureState();
  }
  return true;
}

export function handlePcapConnectionChange(connected) {
  state.pcap.connected = connected === true;
  if (!state.pcap.connected) {
    resetCaptureState();
  }
}

export function handlePcapRequestTimeout(requestId, moduleAction) {
  const [group, action] = String(moduleAction || '').split(':');
  if (group !== 'pcap') {
    return false;
  }

  const pcapState = state.pcap;
  if ((action === 'write' && requestMatches({ requestId }, pcapState.activeCaptureRequestId)) ||
      (action === 'read' && requestMatches({ requestId }, pcapState.pendingReadRequestId))) {
    resetCaptureState();
    return true;
  }
  return false;
}

function clearLastCaptureResult() {
  if (state.pcap.lastPcapUrl) {
    URL.revokeObjectURL(state.pcap.lastPcapUrl);
    state.pcap.lastPcapUrl = null;
    state.pcap.lastPcapName = '';
  }
}

function setCaptureResult(parameters) {
  clearLastCaptureResult();

  const dataB64 = parameters.dataB64 || '';
  if (dataB64) {
    state.pcap.lastPcapUrl = toBlobUrl(dataB64);
    state.pcap.lastPcapName = fileNameFromPath(parameters.file) || `pcap_${Date.now()}.pcap`;
  }

  state.pcap.recordingState = 'idle';
  state.pcap.captureStartTs = null;
  state.pcap.interfaceName = '';
  state.pcap.captureValues = null;
  state.pcap.activeCaptureRequestId = null;
  state.pcap.pendingReadRequestId = null;
}

function resetCaptureState() {
  state.pcap.recordingState = 'idle';
  state.pcap.captureStartTs = null;
  state.pcap.interfaceName = '';
  state.pcap.captureValues = null;
  state.pcap.activeCaptureRequestId = null;
  state.pcap.pendingReadRequestId = null;
}

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

  capture.bindSubmit(() => {
    clearLastCaptureResult();
    const values = capture.getValues(capture.requestResponseObject);
    pcapState.interfaceName = values.interface?.value || '';
    pcapState.captureValues = values;

    const writePcapRequest = buildRequest(
      pageConfig.actions.write.group,
      pageConfig.actions.write.action,
      values
    );
    pcapState.recordingState = 'starting';
    pcapState.activeCaptureRequestId = writePcapRequest.requestId;
    pcapState.pendingReadRequestId = null;
    syncCaptureView();
    const sent = sendPcapRequest(
      sendPayload,
      addLog,
      writePcapRequest,
      pageConfig.actions.write.group,
      pageConfig.actions.write.action
    );
    if (!sent) {
      resetCaptureState();
      syncCaptureView();
    }
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
    const previousState = pcapState.recordingState;
    pcapState.recordingState = 'processing';
    syncCaptureView();

    const readPcapRequest = buildRequest(
      pageConfig.actions.read.group,
      pageConfig.actions.read.action,
      {}
    );
    pcapState.pendingReadRequestId = readPcapRequest.requestId;
    const ok = sendPcapRequest(
      sendPayload,
      addLog,
      readPcapRequest,
      pageConfig.actions.read.group,
      pageConfig.actions.read.action
    );
    if (!ok) {
      pcapState.pendingReadRequestId = null;
      pcapState.recordingState = previousState;
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
      if (!isPcapMessage(message)) {
        return;
      }
      capture.setInterfaceOptions(pcapState.interfaces);
      if (isCaptureActiveState(pcapState) && pcapState.captureValues) {
        capture.setValues(pcapState.captureValues);
      }
      syncCaptureView();
    },
    onConnectionChange(connected) {
      handlePcapConnectionChange(connected);
      if (pcapState.connected) {
        requestInterfaces();
      }
      syncCaptureView();
    },
    onPcapStateChange() {
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
  return pcapState.recordingState !== 'idle';
}

function requestMatches(message, requestId) {
  return requestId !== null && requestId !== undefined &&
    String(message.requestId) === String(requestId);
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
