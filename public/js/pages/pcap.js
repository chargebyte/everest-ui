// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { state } from '../state.js';
import { renderSettingsSection } from '../ui/settingsTable.js';
import { MODULE_IDS } from '../protocol/constants.js';

function formatElapsedMs(ms) {
  const total = Math.max(0, Math.floor(ms / 1000));
  const h = String(Math.floor(total / 3600)).padStart(2, '0');
  const m = String(Math.floor((total % 3600) / 60)).padStart(2, '0');
  const s = String(total % 60).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

function toBlobUrl(base64Data) {
  const bytes = atob(base64Data || '');
  const out = new Uint8Array(bytes.length);
  for (let i = 0; i < bytes.length; i += 1) {
    out[i] = bytes.charCodeAt(i);
  }
  const blob = new Blob([out], { type: 'application/octet-stream' });
  return URL.createObjectURL(blob);
}

export function renderPcapPage(container, { sendPayload, buildCatalogRequest, addLog }) {
  const pcapState = state.pcap;
  const connectionState = state.connection;
  const captureConfiguration = renderSettingsSection({
    title: 'Capture Configuration',
    rows: [
      { type: 'text', id: 'iface', label: 'Interface:', value: 'lo' }
    ]
  });

  container.innerHTML = `
    <div class="page">
      <h1>PCAP Trace Recording</h1>

      ${captureConfiguration}

      <section class="section">
        <h2>Capture Control</h2>
        <div class="status-row">
          <span class="label">Status:</span>
          <span id="capture-status" class="status-value"></span>
          <span class="label">Elapsed Time:</span>
          <span id="elapsed-time">00:00:00</span>
        </div>

        <div class="controls">
          <button id="start-btn" class="btn btn-start">Start Recording</button>
          <button id="stop-btn" class="btn btn-stop">Stop Recording</button>
          <button id="download-btn" class="btn btn-download">Download PCAP</button>
        </div>
      </section>
    </div>
  `;

  const ifaceInput = container.querySelector('#iface');
  const startBtn = container.querySelector('#start-btn');
  const stopBtn = container.querySelector('#stop-btn');
  const downloadBtn = container.querySelector('#download-btn');
  const statusEl = container.querySelector('#capture-status');
  const elapsedEl = container.querySelector('#elapsed-time');

  let elapsedTimer = null;

  function stopElapsed() {
    if (elapsedTimer) {
      clearInterval(elapsedTimer);
      elapsedTimer = null;
    }
  }

  function startElapsed() {
    stopElapsed();
    elapsedTimer = setInterval(() => {
      if (!pcapState.captureStartTs) {
        elapsedEl.textContent = '00:00:00';
        return;
      }
      elapsedEl.textContent = formatElapsedMs(Date.now() - pcapState.captureStartTs);
    }, 500);
  }

  function updateStateView() {
    const s = pcapState.recordingState;
    let dotClass = 'idle';
    let text = 'Idle';

    if (s === 'running') {
      dotClass = 'running';
      text = 'Capturing...';
    } else if (s === 'processing') {
      dotClass = 'processing';
      text = 'Processing...';
    }

    statusEl.innerHTML = `<span class="dot ${dotClass}"></span>${text}`;
    startBtn.disabled = !connectionState.connected || s !== 'idle';
    stopBtn.disabled = !connectionState.connected || s !== 'running';
    downloadBtn.disabled = !pcapState.lastPcapUrl || s === 'running' || s === 'processing';

    if (s === 'running') {
      startElapsed();
    } else {
      stopElapsed();
      if (s === 'idle') {
        elapsedEl.textContent = '00:00:00';
      }
    }
  }

  function clearDownload() {
    if (pcapState.lastPcapUrl) {
      URL.revokeObjectURL(pcapState.lastPcapUrl);
      pcapState.lastPcapUrl = null;
      pcapState.lastPcapName = '';
    }
  }

  startBtn.addEventListener('click', () => {
    const iface = ifaceInput.value.trim();
    if (!iface) {
      addLog('pcap.write rejected: interface is required');
      return;
    }

    clearDownload();
    let payload = null;
    try {
      payload = buildCatalogRequest(MODULE_IDS.PCAP, 'write', {
        interface: iface
      });
    } catch (err) {
      addLog(`pcap.write failed to build: ${err instanceof Error ? err.message : String(err)}`);
      return;
    }

    const ok = sendPayload(payload);

    if (!ok) {
      addLog('pcap.write rejected: backend not connected');
      return;
    }
    addLog('pcap.write sent');
    updateStateView();
  });

  stopBtn.addEventListener('click', () => {
    pcapState.recordingState = 'processing';
    updateStateView();

    let payload = null;
    try {
      payload = buildCatalogRequest(MODULE_IDS.PCAP, 'read', {});
    } catch (err) {
      pcapState.recordingState = 'idle';
      updateStateView();
      addLog(`pcap.read failed to build: ${err instanceof Error ? err.message : String(err)}`);
      return;
    }

    const ok = sendPayload(payload);

    if (!ok) {
      pcapState.recordingState = 'idle';
      updateStateView();
      addLog('pcap.read rejected: backend not connected');
      return;
    }

    addLog('pcap.read sent');
  });

  downloadBtn.addEventListener('click', () => {
    if (!pcapState.lastPcapUrl) {
      return;
    }

    const link = document.createElement('a');
    link.href = pcapState.lastPcapUrl;
    link.download = pcapState.lastPcapName || `pcap_${Date.now()}.pcap`;
    link.click();
  });

  function onMessage(msg) {
    if (msg.type === 'pcap.write.ack' && msg.ok === true) {
      pcapState.recordingState = 'running';
      pcapState.captureStartTs = Date.now();
      addLog('pcap.write acknowledged: capture started');
      updateStateView();
      return;
    }

    if (msg.type === 'pcap.read.result' && msg.ok === true) {
      if (pcapState.lastPcapUrl) {
        URL.revokeObjectURL(pcapState.lastPcapUrl);
      }
      pcapState.lastPcapUrl = toBlobUrl(msg.dataB64 || '');
      pcapState.lastPcapName = `pcap_${Date.now()}.pcap`;
      pcapState.captureStartTs = null;
      pcapState.recordingState = 'idle';
      addLog('pcap.read completed: capture ready for download');
      updateStateView();
      return;
    }

    if (msg.ok === false) {
      pcapState.captureStartTs = null;
      pcapState.recordingState = 'idle';
      const err = msg.error || 'unknown_error';
      addLog(`pcap backend error: ${err}`);
      updateStateView();
    }
  }

  function onConnectionChange(connected) {
    if (!connected) {
      pcapState.captureStartTs = null;
      if (pcapState.recordingState === 'processing') {
        pcapState.recordingState = 'idle';
      }
    }
    updateStateView();
  }

  updateStateView();
  return {
    onMessage,
    onConnectionChange,
    destroy() {
      stopElapsed();
      clearDownload();
    }
  };
}
