import assert from 'node:assert/strict';
import { cpSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import test, { after } from 'node:test';
import { pathToFileURL } from 'node:url';

const root = mkdtempSync(path.join(tmpdir(), 'everest-ui-factory-test-'));
cpSync(path.resolve('public/js'), path.join(root, 'js'), { recursive: true });
writeFileSync(path.join(root, 'package.json'), '{"type":"module"}\n');
after(() => rmSync(root, { recursive: true, force: true }));
const { renderFactoryResetPane } = await import(pathToFileURL(path.join(root, 'js/ui/factoryReset.js')).href);
const { createRebootRecovery, createRebootOverlay, showRecoveryNotice } =
  await import(pathToFileURL(path.join(root, 'js/ui/rebootRecovery.js')).href);

class Element {
  constructor(tag = 'div') { this.tag = tag; }
  children = new Map();
  listeners = new Map();
  disabled = false;
  hidden = false;
  textContent = '';
  attributes = new Map();
  set innerHTML(markup) {
    this.children.clear();
    for (const match of markup.matchAll(/<([a-z][a-z0-9]*)\b([^>]*)>/g)) {
      const child = new Element(match[1]);
      child.hidden = /\bhidden\b/.test(match[2]);
      const id = match[2].match(/id="([^"]+)"/)?.[1];
      this.children.set(id || `${child.tag}-${this.children.size}`, child);
    }
  }
  querySelector(selector) {
    const direct = selector.startsWith('#') ? this.children.get(selector.slice(1))
      : [...this.children.values()].find(child => selector.startsWith('.')
        ? child.className?.split(' ').includes(selector.slice(1)) : child.tag === selector);
    return direct || [...this.children.values()].map(child => child.querySelector(selector)).find(Boolean);
  }
  appendChild(child) { this.children.set(`child-${this.children.size}`, child); child.parent = this; }
  prepend(child) { this.children = new Map([[Symbol(), child], ...this.children]); child.parent = this; }
  setAttribute(name, value) { this.attributes.set(name, value); }
  remove() {
    for (const [key, child] of this.parent?.children || []) if (child === this) this.parent.children.delete(key);
  }
  focus() { document.activeElement = this; }
  addEventListener(name, callback) { this.listeners.set(name, callback); }
  click() { if (!this.disabled && !this.hidden) this.listeners.get('click')?.(); }
}
globalThis.document = { createElement: tag => new Element(tag), body: new Element('body'), activeElement: null };

function fixture(t, confirmation = true) {
  const sent = [];
  const busy = [];
  let confirmations = 0;
  let expectedReboots = 0;
  const recovery = {
    ready: true,
    canStart() { return this.ready; },
    requestFactoryReset(request) { sent.push(request); return { ok: true }; },
    expectFactoryReboot() { expectedReboots++; }
  };
  const pane = renderFactoryResetPane({
    sendPayload(request) { sent.push(request); return { ok: true }; },
    addLog() {},
    rebootRecovery: recovery,
    confirm() { confirmations++; return confirmation; },
    onBusyChange(value) { busy.push(value); }
  });
  t.after(() => pane.destroy());
  const reset = pane.element.querySelector('#factory-reset-start');
  const reboot = pane.element.querySelector('#factory-reset-reboot');
  const status = pane.element.querySelector('#factory-reset-status');
  function reply(request, parameters, ok = true, final = true) {
    pane.onMessage({ requestId: request.requestId, type: `factory_reset.${request.action}.${ok ?
      Object.keys(parameters).length ? 'result' : 'ack' : 'error'}`, parameters, ok, final });
  }
  function statusReply(state = 'idle', canReset = true, rebootExpected = false) {
    const request = sent.findLast(item => item.action === 'read_status');
    reply(request, { state, can_reset: canReset, reboot_expected: rebootExpected });
  }
  pane.onConnectionChange(true);
  statusReply();
  return { pane, sent, busy, reset, reboot, status, reply, statusReply, recovery,
    expectedReboots: () => expectedReboots, confirmations: () => confirmations };
}

test('confirmation cancellation sends no destructive request', (t) => {
  const f = fixture(t, false);
  f.reset.click();
  assert.equal(f.confirmations(), 1);
  assert.deepEqual(f.sent.map(item => item.action), ['read_status']);
  assert.equal(f.reset.disabled, false);
  assert.equal(f.reboot, undefined);
});

test('confirmed reset expects automatic reboot only on success and never renders a reboot button', (t) => {
  for (const success of [true, false]) {
    const f = fixture(t);
    f.reset.click();
    const request = f.sent.at(-1);
    assert.equal(request.group, 'factory_reset');
    assert.deepEqual(request.parameters, { confirm: true });
    assert.equal(f.reset.disabled, true);
    assert.equal(f.reboot, undefined);
    assert.equal(f.pane.isBusy(), true);
    f.reply(request, {}, true, false);
    assert.equal(f.pane.isBusy(), true);
    assert.equal(f.reset.disabled, true);
    f.reply(request, success ? { reboot_expected: true } : { error: 'factory_reset_failed' }, success);
    assert.match(f.status.textContent, success ? /completed/ : /failed/);
    if (success) {
      assert.equal(f.reset.disabled, true);
      assert.ok(f.expectedReboots() > 0);
      assert.match(f.status.textContent, /automatic reboot is expected/);
    } else {
      f.statusReply('failed', true, false);
      assert.equal(f.reset.disabled, false);
      assert.match(f.status.textContent, /may not reboot automatically/);
      assert.equal(f.expectedReboots(), 0);
    }
    assert.equal(f.sent.filter(item => item.action === 'reboot').length, 0);
  }
});

test('backend eligibility and other maintenance prevent action', (t) => {
  const f = fixture(t);
  f.pane.setMaintenanceBusy(true);
  f.reset.click();
  assert.equal(f.confirmations(), 0);
  f.pane.setMaintenanceBusy(false);
  f.recovery.ready = false;
  f.pane.onRebootAvailabilityChange();
  assert.equal(f.reset.disabled, true);
  f.recovery.ready = true;
  f.pane.onConnectionChange(false);
  assert.equal(f.reset.disabled, true);
  f.pane.onConnectionChange(true);
  f.statusReply('idle', false);
  f.reset.click();
  assert.equal(f.confirmations(), 0);
});

test('reconnection recovers a running operation and its completed outcome without resending reset', (t) => {
  const f = fixture(t);
  f.reset.click();
  f.pane.onConnectionChange(false);
  f.pane.onConnectionChange(true);
  f.statusReply('running', false, false);
  assert.equal(f.pane.isBusy(), true);
  f.pane.onConnectionChange(false);
  f.pane.onConnectionChange(true);
  f.statusReply('failed', true, false);
  assert.equal(f.pane.isBusy(), false);
  assert.equal(f.reboot, undefined);
  assert.equal(f.reset.disabled, false);
  assert.equal(f.sent.filter(item => item.action === 'reset').length, 1);
});

test('transport timeout with string request ID locks until an authoritative status reply', (t) => {
  const f = fixture(t);
  f.reset.click();
  const request = f.sent.at(-1);
  f.pane.onRequestTimeout({ requestId: String(request.requestId), moduleAction: 'factory_reset:reset' });
  assert.equal(f.reset.disabled, true);
  assert.equal(f.sent.at(-1).action, 'read_status');
  f.reply(request, { reboot_expected: true }); // Late transport replies cannot unlock this operation.
  assert.equal(f.reset.disabled, true);
  f.statusReply('rebooting', false, true);
  assert.equal(f.reset.disabled, true);
  assert.ok(f.expectedReboots() > 0);
});

test('rejection before command execution does not offer reboot', (t) => {
  const f = fixture(t);
  f.reset.click();
  f.reply(f.sent.at(-1), { error: 'factory_reset_not_allowed' }, false);
  assert.equal(f.reboot, undefined);
  f.statusReply('idle', false, false);
  assert.equal(f.reboot, undefined);
});

test('real overlay retains reset completion and Reload switches locally to blocked access', (t) => {
  const sent = [];
  const memory = new Map();
  const app = new Element('main');
  let pane;
  let recovery;
  let available = 0;
  let closes = 0;
  let now = 1000;
  const render = createRebootOverlay(app, () => recovery.manualReload());
  const sendPayload = request => { sent.push(request); return { ok: true }; };
  recovery = createRebootRecovery({
    sendPayload,
    closeTransport() { closes++; recovery.onDisconnected(); pane.onConnectionChange(false); },
    render,
    onChange() { pane?.onRebootAvailabilityChange(); },
    storage: { getItem: key => memory.get(key), setItem: (key, value) => memory.set(key, value),
      removeItem: key => memory.delete(key) },
    now: () => now,
    schedule: () => 1, cancelTimer() {}, onAvailable() { available++; }
  });
  pane = renderFactoryResetPane({ sendPayload, addLog() {}, rebootRecovery: recovery, confirm: () => true });
  t.after(() => { pane.destroy(); recovery.destroy(); });
  recovery.onConnected();
  recovery.onMessage({ requestId: sent.at(-1).requestId, type: 'system.read_reboot_settings.result',
    ok: true, final: true, parameters: { wait_timeout_seconds: 30,
      boot_id: '11111111-1111-1111-1111-111111111111' } });
  pane.onConnectionChange(true);
  pane.onMessage({ requestId: sent.at(-1).requestId, type: 'factory_reset.read_status.result', ok: true,
    parameters: { state: 'idle', can_reset: true, reboot_expected: false } });
  pane.element.querySelector('#factory-reset-start').click();
  const request = sent.at(-1);
  const dialog = [...document.body.children.values()].at(-1);
  const reloadButton = dialog.querySelector('button');
  assert.equal(app.inert, true);
  assert.equal(reloadButton.hidden, true);
  reloadButton.click();
  assert.equal(available, 0);
  assert.equal(closes, 0);

  const success = { requestId: request.requestId, type: 'factory_reset.reset.result', ok: true,
    final: true, parameters: { reboot_expected: true } };
  recovery.onMessage(success);
  pane.onMessage(success);
  assert.equal(closes, 1);
  assert.equal(reloadButton.hidden, false);
  assert.match(dialog.querySelector('h2').textContent, /Waiting/);
  assert.equal(pane.element.querySelector('#factory-reset-start').disabled, true);
  assert.match(dialog.querySelector('p').textContent, /Factory reset completed/);
  reloadButton.click();
  assert.equal(available, 0);
  assert.equal(app.inert, true);
  assert.equal(recovery.isActive(), true);
  assert.equal(memory.has('everest_ui_reboot_recovery_v1'), true);
  assert.equal(reloadButton.hidden, true);
  assert.equal(dialog.querySelector('h2').textContent, 'Login not allowed');
  assert.match(dialog.querySelector('p').textContent, /Factory reset completed/);
  assert.match(dialog.querySelector('p').textContent, /waiting for device reboot/);
  pane.onRebootAvailabilityChange();
  assert.equal(recovery.isActive(), true);
  assert.equal(sent.filter(item => item.action === 'reset').length, 1);
  recovery.observeStatus({ loginAllowed: true, loginDisabledReason: '', authenticated: false, setupRequired: false });
  assert.equal(available, 1);
  assert.equal(app.inert, false);
  assert.equal(recovery.isActive(), false);
});

test('unconfirmed availability screens do not assert backend denial or successful reset', () => {
  const app = new Element('main');
  const render = createRebootOverlay(app, () => {});
  render({ kind: 'reboot', phase: 'unconfirmed' });
  const dialog = [...document.body.children.values()].at(-1);
  assert.equal(dialog.querySelector('h2').textContent, 'Reboot not confirmed');
  assert.doesNotMatch(dialog.querySelector('p').textContent, /Login not allowed/);
  for (const phase of ['waiting', 'unconfirmed']) {
    render({ kind: 'factory_reset', phase, knownFailure: true });
    assert.equal(dialog.querySelector('h2').textContent, 'Checking device availability');
    assert.match(dialog.querySelector('p').textContent, /Factory reset failed/);
    assert.doesNotMatch(dialog.querySelector('p').textContent, /completed|Login not allowed/);
  }
  render({ kind: 'reboot', phase: 'blocked', restrictionObserved: true, reason: 'waiting_for_reboot' });
  assert.equal(dialog.querySelector('h2').textContent, 'Login not allowed');
  render(null);
});

test('recovery notices are non-blocking and replace an earlier notice', () => {
  const app = new Element('main');
  app.inert = false;
  const content = new Element('section');
  content.className = 'content';
  app.appendChild(content);
  showRecoveryNotice(app, 'reboot_not_confirmed');
  assert.equal(content.querySelector('p').textContent, 'Reboot was not confirmed. The backend still permits access.');
  assert.equal(content.querySelector('p').attributes.get('role'), 'status');
  assert.equal(app.inert, false);
  showRecoveryNotice(app, 'factory_reset_failed');
  assert.equal(content.children.size, 1);
  assert.equal(content.querySelector('p').textContent, 'Factory reset failed. The backend still permits access.');
  showRecoveryNotice(app, undefined);
  assert.equal(content.children.size, 1);
});
