import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const source = readFileSync('public/js/ui/featureAvailability.js', 'utf8');
const { createFeatureAvailability } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

function fixture() {
  let route = 'everest';
  let requestId = 0;
  let sendFails = false;
  const visible = {};
  const timers = new Map();
  const readiness = [];
  let redirects = 0;
  const features = createFeatureAvailability({
    setPageAvailable: (page, value) => { visible[page] = value; },
    getActiveRoute: () => route,
    returnToDefault() { route = 'everest'; redirects++; },
    onReadyChange: value => readiness.push(value),
    requestFeatures: () => ({ ok: !sendFails, payload: { requestId: ++requestId } }),
    schedule(callback, delay) { assert.equal(delay, 2000); timers.set(callback, delay); return callback; },
    cancel: id => timers.delete(id)
  });
  return {
    features, visible, timers, readiness,
    setRoute: value => { route = value; },
    setSendFailure: value => { sendFails = value; },
    get route() { return route; },
    get redirects() { return redirects; },
    get requests() { return requestId; },
    reply(parameters = { network: true, safety: true }, type = 'result', id = requestId) {
      features.onMessage({ type: `system.read_features.${type}`, requestId: id, parameters });
    },
    retry() {
      assert.equal(timers.size, 1);
      const callback = timers.keys().next().value;
      timers.delete(callback);
      callback();
    }
  };
}

test('initial discovery fails closed and retries without duplicate requests', () => {
  const f = fixture();
  assert.deepEqual(f.visible, { network: false, safety: false });
  assert.equal(f.features.isReady(), false);
  assert.equal(f.features.allows('ssh'), true);
  f.features.connect();
  f.reply({}, 'error');
  assert.deepEqual(f.visible, { network: false, safety: false });
  f.retry();
  assert.equal(f.requests, 2);
  assert.equal(f.timers.size, 0);
  f.reply();
  assert.equal(f.features.isReady(), true);
  assert.deepEqual(f.visible, { network: true, safety: true });
});

test('ordinary reconnect retains route and navigation until fresh flags explicitly disable it', () => {
  const f = fixture();
  f.features.connect();
  f.reply();
  f.setRoute('safety');
  f.features.disconnect();
  assert.equal(f.features.isReady(), false);
  assert.deepEqual(f.visible, { network: true, safety: true });
  f.features.connect();
  assert.equal(f.route, 'safety');
  f.reply();
  assert.equal(f.route, 'safety');
  assert.equal(f.redirects, 0);
  f.features.connect();
  f.reply({ network: true, safety: false });
  assert.equal(f.route, 'everest');
  assert.equal(f.redirects, 1);
});

test('refresh errors and malformed results preserve last-known flags but block actions', () => {
  const f = fixture();
  f.features.connect();
  f.reply();
  f.setRoute('network');
  f.features.connect();
  f.reply({}, 'error');
  assert.equal(f.route, 'network');
  assert.equal(f.features.isReady(), false);
  f.retry();
  f.reply({ network: 'true', safety: true });
  assert.deepEqual(f.visible, { network: true, safety: true });
  assert.equal(f.features.isReady(), false);
  f.retry();
  f.reply();
  assert.equal(f.features.isReady(), true);
  assert.equal(f.redirects, 0);
});

test('send failure and timeout retry, and disconnect or destruction cancels retries', () => {
  const f = fixture();
  f.setSendFailure(true);
  f.features.connect();
  assert.equal(f.timers.size, 1);
  f.setSendFailure(false);
  f.retry();
  f.features.onRequestTimeout({ requestId: f.requests, moduleAction: 'system:read_features' });
  assert.equal(f.timers.size, 1);
  f.features.disconnect();
  assert.equal(f.timers.size, 0);
  f.features.connect();
  const stale = f.requests;
  f.features.connect();
  f.reply({ network: false, safety: false }, 'result', stale);
  assert.equal(f.features.isReady(), false);
  f.reply({}, 'error');
  f.features.destroy();
  assert.equal(f.timers.size, 0);
});
