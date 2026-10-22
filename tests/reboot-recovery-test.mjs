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
const { createRebootRecovery, isLoginStatus, probeAuthStatus } =
  await import(pathToFileURL(path.join(root, 'js/ui/rebootRecovery.js')).href);
const { createTransport } = await import(pathToFileURL(path.join(root, 'js/transport.js')).href);
const oldBoot = '11111111-1111-1111-1111-111111111111';
const newBoot = '22222222-2222-2222-2222-222222222222';
const rebootAction = { group: 'firmware', action: 'reboot' };
const openStatus = { loginAllowed: true, loginDisabledReason: '', rebootOperation: '',
  authenticated: false, setupRequired: false, bootId: oldBoot };
const blockedStatus = { ...openStatus, loginAllowed: false, loginDisabledReason: 'waiting_for_reboot',
  rebootOperation: 'factory_reset' };
const flush = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };

function fixture(t, hooks = {}) {
  let clock = 1000;
  let timerId = 0;
  const timers = new Map();
  const memory = new Map();
  const sent = [];
  const views = [];
  const available = [];
  const notices = [];
  let closes = 0;
  let probes = 0;
  let currentStatus = { ...openStatus, authenticated: true };
  let read = async () => currentStatus;
  const schedule = (callback, delay) => { const id = ++timerId; timers.set(id, { callback, at: clock + delay }); return id; };
  const controllers = [];
  function make() {
    const controller = createRebootRecovery({
      sendPayload(payload, options) { sent.push({ payload, options }); return { ok: hooks.sendOk !== false }; },
      closeTransport() { closes++; hooks.onClose?.(controller); },
      render(view) { views.push(view); },
      onChange() { hooks.onChange?.(controller); },
      onAvailable(status, { notice } = {}) { available.push(status); notices.push(notice); },
      readStatus(signal) { probes++; return read(signal); },
      storage: { getItem: key => memory.get(key), setItem: (key, value) => memory.set(key, value),
        removeItem: key => memory.delete(key) },
      now: () => clock, schedule, cancelTimer: id => timers.delete(id)
    });
    controllers.push(controller);
    return controller;
  }
  const controller = make();
  t.after(() => controllers.forEach(item => item.destroy()));
  function settings(seconds = 30, bootId = oldBoot) {
    controller.onConnected();
    controller.onMessage({ requestId: sent.at(-1).payload.requestId, type: 'system.read_reboot_settings.result',
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
  const reset = () => {
    settings();
    controller.requestFactoryReset({ requestId: 500, group: 'factory_reset', action: 'reset', parameters: { confirm: true } });
  };
  const success = () => controller.onMessage({ requestId: 500, type: 'factory_reset.reset.result', ok: true,
    final: true, parameters: { reboot_expected: true } });
  return { controller, make, settings, reset, success, sent, views, memory, advance, available, notices,
    setStatus(value) { currentStatus = value; }, setRead(value) { read = value; },
    closes: () => closes, probes: () => probes };
}

test('reboot sends once without acknowledgement tracking and waits for backend-authorized login', async t => {
  const f = fixture(t);
  f.settings();
  assert.equal(f.controller.requestReboot(rebootAction), true);
  assert.deepEqual(f.sent.at(-1).options, { expectResponse: false });
  assert.equal(f.closes(), 1);
  f.setStatus({ ...blockedStatus, bootId: newBoot });
  await f.advance(5000);
  assert.equal(f.available.length, 0); // Even a new boot identity cannot override a backend restriction.
  f.setStatus({ ...openStatus, bootId: newBoot });
  await f.advance(2000);
  assert.equal(f.available.length, 1);
  assert.equal(f.controller.isActive(), false);
  assert.equal(f.sent.filter(item => item.payload.action === 'reboot').length, 1);
});

test('successful reset retains local information and only transitions to blocked login at timeout', async t => {
  const f = fixture(t);
  f.reset();
  assert.equal(f.controller.manualReload(), false);
  await f.advance(25000);
  assert.equal(f.probes(), 0);
  assert.equal(f.views.at(-1).phase, 'resetting');
  f.success();
  f.setStatus(blockedStatus);
  assert.equal(f.closes(), 1);
  await f.advance(29000);
  assert.equal(f.views.at(-1).phase, 'waiting');
  assert.equal(f.views.at(-1).completed, true);
  assert.equal(f.available.length, 0);
  await f.advance(1000);
  assert.equal(f.views.at(-1).phase, 'blocked');
  assert.equal(f.views.at(-1).kind, 'factory_reset');
  const count = f.probes();
  await f.advance(10000);
  assert.ok(f.probes() > count);
  assert.equal(f.available.length, 0);
});

test('platform wait controls local transition even while requests fail or remain pending', async t => {
  const f = fixture(t);
  f.settings(120);
  f.controller.requestReboot(rebootAction);
  f.setRead(async () => { throw new Error('service unavailable'); });
  await f.advance(30000);
  assert.equal(f.views.at(-1).phase, 'waiting');
  await f.advance(90000);
  assert.equal(f.views.at(-1).phase, 'unconfirmed');
  assert.equal(f.available.length, 0);
});

test('manual Reload switches locally and neither dismisses restrictions nor retries commands', async t => {
  const hooks = {};
  const f = fixture(t, hooks);
  f.reset(); f.success(); f.setStatus(blockedStatus);
  hooks.onChange = controller => controller.expectFactoryReboot();
  assert.equal(f.controller.manualReload(), true);
  assert.equal(f.views.at(-1).phase, 'blocked');
  assert.equal(f.controller.isActive(), true);
  assert.equal(f.memory.has('everest_ui_reboot_recovery_v1'), true);
  assert.equal(f.controller.canStart(), false);
  const count = f.probes();
  await f.advance(6000);
  assert.ok(f.probes() > count);
  assert.equal(f.sent.filter(item => item.payload.action === 'reset').length, 1);
  const resumed = f.make();
  f.controller.destroy();
  assert.equal(resumed.resume(), 'waiting');
  assert.equal(f.views.at(-1).phase, 'blocked');
  assert.equal(f.available.length, 0);
});

test('fresh browser displays backend lockout, including when credentials were erased', async t => {
  const f = fixture(t);
  f.controller.observeStatus({ ...blockedStatus, setupRequired: true });
  assert.equal(f.views.at(-1).phase, 'blocked');
  assert.equal(f.views.at(-1).completed, true);
  assert.equal(f.controller.canStart(), false);
  f.setStatus({ ...blockedStatus, setupRequired: true });
  await f.advance(10000);
  assert.equal(f.available.length, 0);
});

test('service restart restores login on the same boot, but never revives an old session', async t => {
  const f = fixture(t);
  f.controller.observeStatus(blockedStatus);
  f.setStatus({ ...openStatus, authenticated: true });
  await f.advance(4000);
  assert.equal(f.available.length, 0);
  f.setStatus(openStatus);
  await f.advance(2000);
  assert.equal(f.available.length, 1);
  assert.equal(f.available[0].authenticated, false);
  assert.equal(f.memory.has('everest_ui_reboot_recovery_v1'), false);
});

test('missing identity or old dismissal cannot bypass authoritative backend state', async t => {
  const f = fixture(t);
  f.memory.set('everest_ui_reboot_dismissal_v1', JSON.stringify({ version: 1, bootId: oldBoot }));
  const controller = f.make();
  assert.equal(f.memory.has('everest_ui_reboot_dismissal_v1'), false);
  controller.observeStatus({ ...blockedStatus, bootId: '' });
  f.setStatus({ ...blockedStatus, bootId: '' });
  await f.advance(2000);
  assert.equal(f.available.length, 0);
  f.setStatus({ ...openStatus, bootId: '' });
  await f.advance(2000);
  assert.equal(f.available.length, 1);
});

test('an initial offline screen does not invent a restriction once the backend permits access', async t => {
  const f = fixture(t);
  f.controller.onStatusUnavailable();
  assert.equal(f.views.at(-1).phase, 'unconfirmed');
  f.setStatus({ ...openStatus, authenticated: true });
  await f.advance(0);
  assert.equal(f.available.length, 1);
  assert.equal(f.controller.isActive(), false);
});

test('an actual unavailable-state denial cannot be confused with a local connection error', async t => {
  const f = fixture(t);
  f.controller.onStatusUnavailable();
  f.setStatus({ ...blockedStatus, loginDisabledReason: 'login_state_unavailable', rebootOperation: '' });
  await f.advance(0);
  assert.equal(f.views.at(-1).restrictionObserved, true);
  f.setStatus({ ...openStatus, authenticated: true });
  await f.advance(40000);
  assert.equal(f.available.length, 0);
  f.setStatus(openStatus);
  await f.advance(2000);
  assert.equal(f.available.length, 1);
});

test('a lost reboot request releases a still-authenticated session only after a fresh post-deadline probe', async t => {
  const f = fixture(t);
  f.settings(1);
  let firstResponse;
  let reads = 0;
  f.setRead(() => ++reads === 1 ? new Promise(resolve => { firstResponse = resolve; })
    : Promise.resolve({ ...openStatus, authenticated: true }));
  f.controller.requestReboot(rebootAction);
  await f.advance(0);
  await f.advance(1000);
  assert.equal(f.views.at(-1).phase, 'unconfirmed');
  firstResponse({ ...openStatus, authenticated: true });
  await flush();
  assert.equal(f.available.length, 0); // The response belongs to a probe started before the deadline.
  await f.advance(2000);
  assert.equal(reads, 2);
  assert.equal(f.available.length, 1);
  assert.equal(f.available[0].authenticated, true);
  assert.equal(f.notices[0], 'reboot_not_confirmed');
  assert.equal(f.controller.isActive(), false);
  assert.equal(f.memory.has('everest_ui_reboot_recovery_v1'), false);
  assert.equal(f.sent.filter(item => item.payload.action === 'reboot').length, 1);
});

test('a rejected reboot checks backend status immediately even if the socket already disconnected', async t => {
  const f = fixture(t, { onClose: controller => controller.onDisconnected() });
  f.settings();
  f.controller.requestReboot(rebootAction);
  const request = f.sent.at(-1).payload;
  f.controller.onMessage({ requestId: request.requestId, type: 'firmware.reboot.error', ok: false,
    final: true, parameters: { error: 'not_allowed' } });
  await f.advance(0);
  assert.equal(f.available.length, 1);
  assert.equal(f.available[0].authenticated, true);
  assert.equal(f.notices[0], 'reboot_not_confirmed');
  assert.equal(f.sent.filter(item => item.payload.action === 'reboot').length, 1);
});

test('a rejection during a running probe requests a fresh check without overlapping requests', async t => {
  const f = fixture(t);
  f.settings();
  let firstResponse;
  let reads = 0;
  f.setRead(() => ++reads === 1 ? new Promise(resolve => { firstResponse = resolve; })
    : Promise.resolve(blockedStatus));
  f.controller.requestReboot(rebootAction);
  await f.advance(0);
  await f.advance(10);
  f.controller.onMessage({ requestId: f.sent.at(-1).payload.requestId, type: 'firmware.reboot.error', ok: false,
    final: true, parameters: { error: 'not_allowed' } });
  assert.equal(reads, 1);
  firstResponse({ ...openStatus, authenticated: true });
  await flush();
  assert.equal(f.available.length, 0);
  await f.advance(0);
  assert.equal(reads, 2);
  assert.equal(f.views.at(-1).restrictionObserved, true); // The backend restriction wins over the rejection.
  f.setRead(async () => ({ ...openStatus, authenticated: true }));
  await f.advance(40000);
  assert.equal(f.available.length, 0);
});

test('delayed acceptance and a successful reset remain strict past the unconfirmed deadline', async t => {
  const f = fixture(t);
  f.settings();
  f.controller.requestReboot(rebootAction);
  await f.advance(20000);
  assert.equal(f.available.length, 0);
  f.setStatus(blockedStatus);
  await f.advance(2000);
  assert.equal(f.views.at(-1).operationAccepted, true);
  f.setStatus({ ...openStatus, authenticated: true });
  await f.advance(30000);
  assert.equal(f.available.length, 0);
  assert.equal(f.views.at(-1).phase, 'blocked');

  const successful = fixture(t);
  successful.reset(); successful.success();
  await successful.advance(60000); // Acceptance alone is sufficient, even without observing a status restriction.
  assert.equal(successful.available.length, 0);
  assert.equal(successful.views.at(-1).operationAccepted, true);
});

test('failed reset plus unrelated disconnect recovers on the same authenticated service', async t => {
  const f = fixture(t);
  f.reset();
  f.controller.onMessage({ requestId: 500, type: 'factory_reset.reset.error', ok: false, final: true,
    parameters: { error: 'factory_reset_failed' } });
  f.controller.onDisconnected();
  await f.advance(29000);
  assert.equal(f.available.length, 0);
  assert.equal(f.views.at(-1).knownFailure, true);
  assert.notEqual(f.views.at(-1).operationAccepted, true);
  assert.notEqual(f.views.at(-1).completed, true);
  await f.advance(2000);
  assert.equal(f.available.length, 1);
  assert.equal(f.available[0].authenticated, true);
  assert.equal(f.notices[0], 'factory_reset_failed');
  assert.equal(f.sent.filter(item => item.payload.action === 'reset').length, 1);
});

test('refresh preserves both an unconfirmed deadline and confirmed restriction evidence', async t => {
  for (const accepted of [false, true]) {
    const f = fixture(t);
    f.settings(); f.controller.requestReboot(rebootAction);
    if (accepted) f.controller.observeStatus(blockedStatus);
    await f.advance(10000);
    f.controller.destroy();
    const resumed = f.make();
    assert.equal(resumed.resume(), 'waiting');
    await f.advance(19000);
    assert.equal(f.available.length, 0);
    await f.advance(2000);
    assert.equal(f.available.length, accepted ? 0 : 1);
    assert.equal(resumed.isActive(), accepted);
  }
});

test('deadline does not reuse cached permissive status when the service becomes unavailable', async t => {
  const f = fixture(t);
  f.settings(); f.controller.requestReboot(rebootAction);
  await f.advance(20000);
  f.setRead(async () => { throw new Error('offline'); });
  await f.advance(20000);
  assert.equal(f.views.at(-1).phase, 'unconfirmed');
  assert.equal(f.available.length, 0);
  f.setRead(async () => ({ ...openStatus, authenticated: true }));
  await f.advance(2000);
  assert.equal(f.available.length, 1);
});

test('manual Reload changes unconfirmed presentation without shortening the verification wait', async t => {
  const f = fixture(t);
  f.settings(); f.controller.requestReboot(rebootAction);
  assert.equal(f.controller.manualReload(), true);
  assert.equal(f.views.at(-1).phase, 'unconfirmed');
  await f.advance(29000);
  assert.equal(f.available.length, 0);
  await f.advance(2000);
  assert.equal(f.available.length, 1);
});

test('legacy saved unconfirmed recovery can exit, while legacy restriction evidence stays latched', async t => {
  for (const loginBlocked of [undefined, true]) {
    const f = fixture(t);
    f.memory.set('everest_ui_reboot_recovery_v1', JSON.stringify({ version: 1, kind: 'reboot', requestId: 42,
      phase: 'blocked', bootId: oldBoot, waitSeconds: 1, waitStarted: 0, loginBlocked }));
    const resumed = f.make();
    resumed.resume();
    await f.advance(0);
    assert.equal(f.available.length, loginBlocked ? 0 : 1);
    assert.equal(resumed.isActive(), loginBlocked === true);
  }
});

test('lost final result still obtains completion context from backend status', async t => {
  const f = fixture(t, { onClose: controller => controller.onDisconnected() });
  f.reset();
  f.controller.onDisconnected();
  f.controller.onDisconnected();
  f.setStatus(blockedStatus);
  await f.advance(10000);
  assert.equal(f.closes(), 1);
  assert.equal(f.views.at(-1).completed, true);
  assert.equal(f.views.at(-1).phase, 'waiting');
  await f.advance(20000);
  assert.equal(f.views.at(-1).phase, 'blocked');
});

test('failed reset restores pane; a subsequent shutdown can still enter recovery', async t => {
  const f = fixture(t);
  f.reset();
  f.controller.onMessage({ requestId: 500, type: 'factory_reset.reset.error', ok: false, final: true,
    parameters: { error: 'factory_reset_failed' } });
  assert.equal(f.controller.isActive(), false);
  assert.equal(f.controller.canStart(), true);
  assert.equal(f.probes(), 0);
  f.controller.onDisconnected();
  f.setStatus({ ...openStatus, bootId: newBoot, setupRequired: true });
  await f.advance(0);
  assert.equal(f.available.length, 1);
});

test('failed send does not close transport or leave progress active', t => {
  const hooks = {};
  const f = fixture(t, hooks);
  f.settings(); hooks.sendOk = false;
  assert.equal(f.controller.requestReboot(rebootAction), false);
  assert.equal(f.controller.isActive(), false);
  assert.equal(f.closes(), 0);
});

test('status is validated and read-only probes are uncached, bounded, and never overlap', async t => {
  assert.equal(isLoginStatus({ authenticated: false, setupRequired: false }), false);
  assert.equal(isLoginStatus({ ...openStatus, loginAllowed: 'true' }), false);
  const oldFetch = globalThis.fetch;
  t.after(() => { globalThis.fetch = oldFetch; });
  globalThis.fetch = async (url, options) => {
    assert.equal(url, '/auth/status');
    assert.equal(options.method, undefined);
    assert.equal(options.cache, 'no-store');
    return { ok: true, json: async () => blockedStatus };
  };
  assert.deepEqual(await probeAuthStatus(new AbortController().signal), blockedStatus);
  const f = fixture(t);
  f.settings();
  let active = 0;
  f.setRead(signal => new Promise((resolve, reject) => {
    assert.equal(++active, 1);
    signal.addEventListener('abort', () => { active--; reject(new Error('aborted')); });
  }));
  f.controller.requestReboot(rebootAction);
  await f.advance(3000);
  assert.equal(active, 0);
  await f.advance(2000);
  assert.equal(active, 1);
  f.controller.manualReload(); // A local screen transition must not start a concurrent request.
  assert.equal(active, 1);
  f.controller.destroy();
  assert.equal(active, 0);
});

test('transport creates no pending timeout for fire-and-forget but still forwards a rejection', t => {
  const previous = { WebSocket: globalThis.WebSocket, window: globalThis.window,
    setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout };
  const timers = new Map();
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
  globalThis.setTimeout = callback => { timers.set(1, callback); return 1; };
  globalThis.clearTimeout = id => timers.delete(id);
  const received = [];
  const transport = createTransport({ wsPath: '/ws', onOpen() {}, onClose() {},
    onMessage: message => received.push(message), onRequestTimeout() { assert.fail('unexpected timeout'); } });
  t.after(() => { transport.close(); Object.assign(globalThis, previous); });
  transport.connect(); socket.onopen();
  transport.sendPayload({ requestId: 1, group: 'firmware', action: 'reboot', parameters: {} }, { expectResponse: false });
  assert.equal(timers.size, 0);
  socket.onmessage({ data: JSON.stringify({ requestId: 1, type: 'firmware.reboot.error', ok: false, final: true }) });
  assert.equal(received.length, 1);
});
