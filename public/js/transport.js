// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function createTransport({
  wsPath,
  onOpen,
  onClose,
  onMessage,
  onRequestTimeout
}) {
  let ws = null;
  let retryTimer = null;
  let manualClose = false;
  const kPendingRequestTimeoutMs = 50000;
  const pendingRequests = new Map();

  function isOpen() {
    return ws && ws.readyState === WebSocket.OPEN;
  }

  function send(obj) {
    if (!isOpen()) {
      return false;
    }
    ws.send(JSON.stringify(obj));
    return true;
  }

  function sendPayload(payload) {
    const requestId = payload && payload.requestId;
    const hasValidRequestId = (
      (typeof requestId === 'number' && Number.isFinite(requestId)) ||
      (typeof requestId === 'string' && requestId.trim() !== '')
    );
    if (!hasValidRequestId) {
      return { ok: false, payload, error: 'missing_request_id' };
    }

    const requestIdKey = String(requestId);
    const moduleId = typeof payload.group === 'string' ? payload.group : '';
    const actionId = typeof payload.action === 'string' ? payload.action : '';
    pendingRequests.set(requestIdKey, {
      payload,
      moduleAction: `${moduleId}:${actionId}`,
      sentAtMs: Date.now(),
      attempts: 1,
      timeoutId: schedulePendingTimeout(requestIdKey)
    });

    const ok = send(payload);
    if (!ok) {
      clearPendingRequest(requestIdKey);
      return { ok: false, payload };
    }

    return { ok: true, payload };
  }

  function schedulePendingTimeout(requestIdKey) {
    return setTimeout(() => {
      handlePendingTimeout(requestIdKey);
    }, kPendingRequestTimeoutMs);
  }

  function clearPendingRequest(requestIdKey) {
    const pendingRequest = pendingRequests.get(requestIdKey);
    if (pendingRequest?.timeoutId) {
      clearTimeout(pendingRequest.timeoutId);
    }
    pendingRequests.delete(requestIdKey);
  }

  function refreshPendingRequestTimeout(requestIdKey) {
    const pendingRequest = pendingRequests.get(requestIdKey);
    if (!pendingRequest) {
      return;
    }

    if (pendingRequest.timeoutId) {
      clearTimeout(pendingRequest.timeoutId);
    }

    pendingRequest.timeoutId = schedulePendingTimeout(requestIdKey);
  }

  function handlePendingTimeout(requestIdKey) {
    const pendingRequest = pendingRequests.get(requestIdKey);
    if (!pendingRequest) {
      return;
    }

    onRequestTimeout({
      requestId: requestIdKey,
      moduleAction: pendingRequest.moduleAction
    });
    clearPendingRequest(requestIdKey);
  }

  function scheduleReconnect() {
    if (manualClose) {
      return;
    }
    if (retryTimer) {
      return;
    }
    retryTimer = setTimeout(() => {
      retryTimer = null;
      connect();
    }, 2000);
  }

  function connect() {
    manualClose = false;
    if (isOpen()) {
      return;
    }

    const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${scheme}://${window.location.host}${wsPath}`;
    ws = new WebSocket(url);

    ws.onopen = () => {
      onOpen(url);
      if (retryTimer) {
        clearTimeout(retryTimer);
        retryTimer = null;
      }
    };

    ws.onclose = (event) => {
      pendingRequests.forEach((_, requestIdKey) => {
        clearPendingRequest(requestIdKey);
      });
      onClose({
        code: event.code,
        reason: event.reason,
        wasClean: event.wasClean
      });
      if (event.reason !== 'ui already in use') {
        scheduleReconnect();
      }
    };

    ws.onmessage = (event) => {
      let msg = null;
      try {
        msg = JSON.parse(event.data);
      } catch (_) {
        return;
      }

      // Only accept responses that correlate to a pending request.
      const responseRequestId = msg.requestId;
      if (responseRequestId === undefined || responseRequestId === null) {
        if (msg.responseId) {
          send({ type: 'ack', responseId: msg.responseId });
        }
        onMessage(msg);
        return;
      }

      const requestIdKey = String(responseRequestId);
      if (msg.responseId) {
        send({ type: 'ack', responseId: msg.responseId });
      }
      if (!pendingRequests.has(requestIdKey)) {
        if (typeof msg.type === 'string' && msg.type.endsWith('.error')) {
          onMessage(msg);
        }
        return;
      }

      // ACK only valid, correlated responses.
      const isFinal = msg.final !== false;
      if (isFinal) {
        clearPendingRequest(requestIdKey);
      } else {
        refreshPendingRequestTimeout(requestIdKey);
      }

      onMessage(msg);
    };
  }

  function close() {
    manualClose = true;
    if (retryTimer) {
      clearTimeout(retryTimer);
      retryTimer = null;
    }
    pendingRequests.forEach((_, requestIdKey) => {
      clearPendingRequest(requestIdKey);
    });
    if (ws) {
      ws.close();
      ws = null;
    }
  }

  return {
    connect,
    close,
    sendPayload,
    isOpen
  };
}
