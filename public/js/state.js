// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export const state = {
  connection: {
    connected: false,
    requestId: 0
  },
  logs: {
    lines: []
  },
  pcap: {
    recordingState: 'idle',
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
  state.logs.lines.push(`[${stamp}] ${message}`);
  if (state.logs.lines.length > 300) {
    state.logs.lines.shift();
  }
}
