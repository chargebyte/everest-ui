// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export const state = {
  connection: {
    connected: false,
    requestId: 0
  },
  uiLog: {
    lines: []
  },
  pcap: {
    connected: false,
    interfaceName: '',
    captureValues: null,
    interfaces: [],
    recordingState: 'idle',
    activeCaptureRequestId: null,
    pendingReadRequestId: null,
    captureStartTs: null,
    lastPcapUrl: null,
    lastPcapName: ''
  }
};

export function nextRequestId() {
  state.connection.requestId += 1;
  return state.connection.requestId;
}

export function appendLog(message) {
  const stamp = new Date().toISOString().replace('T', ' ').replace('Z', '');
  state.uiLog.lines.push(`[${stamp}] ${message}`);
  if (state.uiLog.lines.length > 300) {
    state.uiLog.lines.shift();
  }
}
