// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

export function createFeatureAvailability({ setPageAvailable, getActiveRoute, returnToDefault,
  requestFeatures, onReadyChange = () => {}, schedule = setTimeout, cancel = clearTimeout }) {
  let features = {};
  let ready = false;
  let connected = false;
  let requestId = null;
  let retryTimer = null;
  const allows = (route) => !['network', 'safety'].includes(route) || features[route] === true;
  const isReady = () => ready;
  function publish() {
    for (const route of ['network', 'safety']) {
      setPageAvailable(route, allows(route));
    }
    if (ready && !allows(getActiveRoute())) returnToDefault();
    onReadyChange(ready);
  }
  function stopRetry() {
    if (retryTimer !== null) cancel(retryTimer);
    retryTimer = null;
  }
  function failed() {
    requestId = null;
    ready = false;
    publish();
    if (connected && retryTimer === null) {
      retryTimer = schedule(() => { retryTimer = null; refresh(); }, 2000);
    }
  }
  function refresh() {
    if (!connected || requestId !== null) return;
    const result = requestFeatures();
    if (result.ok) requestId = String(result.payload.requestId);
    else failed();
  }
  function disconnect() {
    connected = false;
    ready = false;
    requestId = null;
    stopRetry();
    publish();
  }
  publish();
  return {
    allows, isReady,
    connect() {
      disconnect();
      connected = true;
      refresh();
    },
    disconnect,
    destroy: disconnect,
    onMessage(message) {
      if (!['system.read_features.result', 'system.read_features.error'].includes(message.type)) return false;
      if (requestId === null || String(message.requestId) !== requestId) return true;
      const values = message.parameters;
      if (message.type.endsWith('.error') || typeof values?.network !== 'boolean' ||
          typeof values?.safety !== 'boolean') {
        failed();
      } else {
        requestId = null;
        stopRetry();
        features = { network: values.network, safety: values.safety };
        ready = true;
        publish();
      }
      return true;
    },
    onRequestTimeout({ requestId: expiredId, moduleAction }) {
      if (moduleAction !== 'system:read_features') return false;
      if (requestId !== null && String(expiredId) === requestId) failed();
      return true;
    }
  };
}
