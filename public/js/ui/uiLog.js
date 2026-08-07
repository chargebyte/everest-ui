// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderUiLog() {
  return `
    <section class="ui-log">
      <div
        id="ui-log-resize-handle"
        class="log-resize-handle"
        title="Drag to resize Web UI Log"
      ></div>
      <h2>Web UI Log</h2>
      <pre id="ui-log" class="log-window"></pre>
    </section>
  `;
}

export function setUiLog(logEl, lines) {
  logEl.textContent = lines.join('\n');
  logEl.scrollTop = logEl.scrollHeight;
}

export function bindUiLogResize(handleEl, logWindowEl) {
  if (!handleEl || !logWindowEl) {
    return;
  }

  const minHeight = 90;
  const maxHeightRatio = 0.7;
  let dragging = false;
  let startY = 0;
  let startHeight = 0;

  function clampHeight(height) {
    const maxHeight = Math.floor(window.innerHeight * maxHeightRatio);
    return Math.max(minHeight, Math.min(maxHeight, height));
  }

  function onMouseMove(event) {
    if (!dragging) {
      return;
    }
    const deltaY = startY - event.clientY;
    const nextHeight = clampHeight(startHeight + deltaY);
    logWindowEl.style.height = `${nextHeight}px`;
  }

  function stopDrag() {
    if (!dragging) {
      return;
    }
    dragging = false;
    document.body.classList.remove('log-resizing');
    window.removeEventListener('mousemove', onMouseMove);
    window.removeEventListener('mouseup', stopDrag);
  }

  handleEl.addEventListener('mousedown', (event) => {
    dragging = true;
    startY = event.clientY;
    startHeight = logWindowEl.getBoundingClientRect().height;
    document.body.classList.add('log-resizing');
    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', stopDrag);
    event.preventDefault();
  });
}
