import assert from 'node:assert/strict';
import { cpSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import test, { after } from 'node:test';
import { pathToFileURL } from 'node:url';

const root = mkdtempSync(path.join(tmpdir(), 'everest-ui-reboot-test-'));
cpSync(path.resolve('public/js'), path.join(root, 'js'), { recursive: true });
writeFileSync(path.join(root, 'package.json'), '{"type":"module"}\n');
after(() => rmSync(root, { recursive: true, force: true }));
const { createRebootRecovery } = await import(pathToFileURL(path.join(root, 'js/ui/rebootRecovery.js')).href);
const { createTransport } = await import(pathToFileURL(path.join(root, 'js/transport.js')).href);
const oldBoot = '11111111-1111-1111-1111-111111111111';
const newBoot = '22222222-2222-2222-2222-222222222222';
const rebootAction = { group: 'firmware', action: 'reboot' };
const flush = async () => { for (let i = 0; i < 5; i++) await Promise.resolve(); };

function fixture(t) {
  let clock = 1000;
  let timerId = 0;
  const timers = new Map();
  const memory = new Map();
  const sent = [];
  const views = [];
  let reloads = 0;
  let closes = 0;
  let probes = 0;
  let currentStatus = { bootId: oldBoot, authenticated: false, setupRequired: false };
  let read = async () => currentStatus;
  const schedule = (callback, delay) => { const id = ++timerId; timers.set(id, { callback, at: clock + delay }); return id; };
  const controllers = [];
  function make() {
    const controller = createRebootRecovery({
      sendPayload(payload, options) { sent.push({ payload, options }); return { ok: true }; },
      closeTransport() { closes++; },
      render(view) { views.push(view); },
      readStatus(signal) { probes++; return read(signal); },
      storage: { getItem: key => memory.get(key), setItem: (key, value) => memory.set(key, value),
        removeItem: key => memory.delete(key) },
      now: () => clock, schedule, cancelTimer: id => timers.delete(id), reload() { reloads++; }
    });
    controllers.push(controller);
    return controller;
  }
  const controller = make();
  t.after(() => controllers.forEach(item => item.destroy()));
  function settings(seconds = 30, bootId = oldBoot) {
    controller.onConnected();
    const request = sent.at(-1).payload;
    controller.onMessage({ requestId: request.requestId, type: 'system.read_reboot_settings.result',
      ok: true, final: true, parameters: { wait_timeout_seconds: seconds, boot_id: bootId } });
  }
  async function advance(milliseconds) {
    const target = clock + milliseconds;
    for (let i = 0; i < 1000; i++) {
      const ready = [...timers].filter(([, entry]) => entry.at <= target).sort((a, b) => a[1].at - b[1].at)[0];
      if (!ready) break;
      clock = ready[1].at;
      timers.delete(ready[0]);
      ready[1].callback();
      await flush();
    }
    clock = target;
    await flush();
  }
  return { controller, make, settings, sent, views, memory, advance,
    setStatus(value) { currentStatus = value; }, setRead(value) { read = value; },
    reloads: () => reloads, closes: () => closes, probes: () => probes };
}

test('reboot is sent once without response tracking; only a new boot permits automatic reload', async (t) => {
  const f = fixture(t);
  assert.equal(f.controller.canStart(), false);
  f.settings();
  assert.equal(f.controller.requestReboot(rebootAction), true);
  assert.deepEqual(f.sent.at(-1).options, { expectResponse: false });
  assert.equal(f.controller.canStart(), false);
  assert.equal(f.controller.onDisconnected(), true);
  assert.equal(f.closes(), 1);
  await f.advance(10000);
  assert.equal(f.reloads(), 0); // Same boot and a live proxy are not sufficient.
  f.setStatus({ bootId: newBoot, authenticated: false, setupRequired: false });
  await f.advance(2000);
  assert.equal(f.reloads(), 1);
  assert.equal(f.sent.filter(item => item.payload.action === 'reboot').length, 1);
  assert.equal(f.views.at(-1).phase, 'returning');
});

test('configured wait changes the fallback threshold and automatic probes continue afterward', async (t) => {
  const f = fixture(t);
  f.settings(120);
  f.controller.requestReboot(rebootAction);
  await f.advance(31000);
  assert.equal(f.views.at(-1).timedOut, false);
  await f.advance(90000);
  assert.equal(f.views.at(-1).timedOut, true);
  const count = f.probes();
  await f.advance(10000);
  assert.ok(f.probes() > count);
  f.controller.manualReload();
  assert.equal(f.memory.size, 0);
  assert.equal(f.reloads(), 1);
});

test('a received rejection restores controls and never retries a reboot', async (t) => {
  const f = fixture(t);
  f.settings();
  f.controller.requestReboot(rebootAction);
  const request = f.sent.at(-1).payload;
  f.controller.onMessage({ requestId: request.requestId, type: 'firmware.reboot.error', ok: false,
    final: true, parameters: { error: 'not_allowed' } });
  assert.equal(f.controller.isActive(), false);
  assert.equal(f.controller.canStart(), true);
  assert.equal(f.views.at(-1), null);
  await f.advance(60000);
  assert.equal(f.reloads(), 0);
  assert.equal(f.sent.filter(item => item.payload.action === 'reboot').length, 1);
});

test('reset has its own execution phase; the recovery clock starts at success or disconnection', async (t) => {
  const f = fixture(t);
  f.settings();
  const request = { requestId: 500, group: 'factory_reset', action: 'reset', parameters: { confirm: true } };
  assert.equal(f.controller.requestFactoryReset(request).ok, true);
  await f.advance(25000);
  assert.equal(f.probes(), 0);
  assert.equal(f.views.at(-1).phase, 'resetting');
  f.controller.onMessage({ requestId: 500, type: 'factory_reset.reset.ack', final: false, ok: true, parameters: {} });
  assert.equal(f.views.at(-1).phase, 'resetting');
  f.controller.onMessage({ requestId: 500, type: 'factory_reset.reset.result', final: true, ok: true,
    parameters: { reboot_expected: true } });
  await f.advance(29000);
  assert.equal(f.views.at(-1).timedOut, false);
  await f.advance(2000);
  assert.equal(f.views.at(-1).timedOut, true);
});

test('reset shutdown without a final response starts recovery; an explicit failure clears it', async (t) => {
  const f = fixture(t);
  f.settings();
  f.controller.requestFactoryReset({ requestId: 501, group: 'factory_reset', action: 'reset', parameters: { confirm: true } });
  f.controller.onDisconnected();
  assert.equal(f.views.at(-1).phase, 'waiting');
  f.setStatus({ bootId: newBoot, authenticated: false, setupRequired: true });
  await f.advance(0);
  assert.equal(f.reloads(), 1);

  const failure = fixture(t);
  failure.settings();
  failure.controller.requestFactoryReset({ requestId: 502, group: 'factory_reset', action: 'reset', parameters: { confirm: true } });
  failure.controller.onMessage({ requestId: 502, type: 'factory_reset.reset.error', final: true, ok: false,
    parameters: { error: 'factory_reset_failed' } });
  assert.equal(failure.controller.isActive(), false);
  assert.equal(failure.controller.canStart(), true);
  await failure.advance(60000);
  assert.equal(failure.probes(), 0);
});

test('refresh resumes persisted recovery without another command; returning goes through auth', async (t) => {
  const f = fixture(t);
  f.settings();
  f.controller.requestReboot(rebootAction);
  await f.advance(10000);
  f.controller.destroy();
  const resumed = f.make();
  assert.equal(resumed.resume(), 'waiting');
  await f.advance(21000);
  assert.equal(f.views.at(-1).timedOut, true);
  assert.equal(f.sent.filter(item => item.payload.action === 'reboot').length, 1);
  f.setStatus({ bootId: newBoot, authenticated: false, setupRequired: false });
  await f.advance(2000);
  resumed.onDisconnected(); // A late socket close must not turn the saved returning phase back into waiting.
  const returning = f.make();
  assert.equal(returning.resume(), 'authenticate');
  assert.equal(returning.acceptReturnedStatus({ bootId: oldBoot, authenticated: false, setupRequired: false }), false);
  assert.equal(returning.acceptReturnedStatus({ bootId: newBoot, authenticated: false, setupRequired: false }), true);
  assert.equal(f.memory.size, 0);
});

test('a failed reset can still reboot during shutdown; recovery resumes on the subsequent disconnect', async (t) => {
  const f = fixture(t);
  f.settings();
  f.controller.requestFactoryReset({ requestId: 503, group: 'factory_reset', action: 'reset', parameters: { confirm: true } });
  f.controller.onMessage({ requestId: 503, type: 'factory_reset.reset.error', ok: false, final: true,
    parameters: { error: 'factory_reset_failed' } });
  assert.equal(f.controller.isActive(), false);
  assert.equal(f.controller.onDisconnected(), true);
  f.setStatus({ bootId: newBoot, authenticated: false, setupRequired: false });
  await f.advance(0);
  assert.equal(f.reloads(), 1);
  assert.equal(f.sent.filter(item => item.payload.action === 'reset').length, 1);
});

test('empty boot identity never falsely confirms reboot; malformed API settings keep actions disabled', async (t) => {
  const f = fixture(t);
  f.settings(30, '');
  f.controller.requestReboot(rebootAction);
  f.setStatus({ bootId: newBoot, authenticated: false, setupRequired: false });
  await f.advance(31000);
  assert.equal(f.reloads(), 0);
  assert.equal(f.views.at(-1).timedOut, true);
  const invalid = fixture(t);
  invalid.settings(0);
  assert.equal(invalid.controller.canStart(), false);
  await invalid.advance(2000);
  assert.equal(invalid.sent.at(-1).payload.action, 'read_reboot_settings');
});

test('recovery probes are aborted after three seconds and never overlap', async (t) => {
  const f = fixture(t);
  f.settings();
  let active = 0;
  f.setRead(signal => new Promise((resolve, reject) => {
    active++;
    assert.equal(active, 1);
    signal.addEventListener('abort', () => { active--; reject(new Error('aborted')); });
  }));
  f.controller.requestReboot(rebootAction);
  await f.advance(3000);
  assert.equal(active, 0);
  await f.advance(2000);
  assert.equal(active, 1);
  f.controller.destroy();
  assert.equal(active, 0);
});

test('transport creates no pending timeout for fire-and-forget but still forwards a rejection', (t) => {
  const previous = { WebSocket: globalThis.WebSocket, window: globalThis.window,
    setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout };
  const timers = new Map();
  let next = 0;
  let socket;
  class Socket {
    static OPEN = 1;
    readyState = 1;
    constructor() { socket = this; }
    send() {}
    close() {}
  }
  globalThis.WebSocket = Socket;
  globalThis.window = { location: { protocol: 'http:', host: 'device' } };
  globalThis.setTimeout = callback => { const id = ++next; timers.set(id, callback); return id; };
  globalThis.clearTimeout = id => timers.delete(id);
  const received = [];
  const transport = createTransport({ wsPath: '/ws', onOpen() {}, onClose() {},
    onMessage: message => received.push(message), onRequestTimeout() { assert.fail('unexpected timeout'); } });
  t.after(() => { transport.close(); Object.assign(globalThis, previous); });
  transport.connect();
  socket.onopen();
  transport.sendPayload({ requestId: 1, group: 'firmware', action: 'reboot', parameters: {} }, { expectResponse: false });
  assert.equal(timers.size, 0);
  socket.onmessage({ data: JSON.stringify({ requestId: 1, type: 'firmware.reboot.error', ok: false, final: true }) });
  assert.equal(received.length, 1);
  transport.sendPayload({ requestId: 2, group: 'factory_reset', action: 'reset', parameters: { confirm: true } });
  assert.equal(timers.size, 1);
});
