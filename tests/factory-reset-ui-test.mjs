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

class Element {
  children = new Map();
  listeners = new Map();
  disabled = false;
  textContent = '';
  set innerHTML(markup) {
    for (const match of markup.matchAll(/id="([^"]+)"/g)) this.children.set(match[1], new Element());
  }
  querySelector(selector) { return this.children.get(selector.slice(1)); }
  addEventListener(name, callback) { this.listeners.set(name, callback); }
  click() { if (!this.disabled) this.listeners.get('click')?.(); }
}
globalThis.document = { createElement: () => new Element() };

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
      assert.match(f.status.textContent, /rebooting automatically/);
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
