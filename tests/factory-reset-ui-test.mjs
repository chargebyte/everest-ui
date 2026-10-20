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
  const pane = renderFactoryResetPane({
    sendPayload(request) { sent.push(request); return { ok: true }; },
    addLog() {},
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
  function statusReply(state = 'idle', canReset = true, rebootAvailable = false) {
    const request = sent.findLast(item => item.action === 'read_status');
    reply(request, { state, can_reset: canReset, reboot_available: rebootAvailable });
  }
  pane.onConnectionChange(true);
  statusReply();
  return { pane, sent, busy, reset, reboot, status, reply, statusReply, confirmations: () => confirmations };
}

test('confirmation cancellation sends no destructive request', (t) => {
  const f = fixture(t, false);
  f.reset.click();
  assert.equal(f.confirmations(), 1);
  assert.deepEqual(f.sent.map(item => item.action), ['read_status']);
  assert.equal(f.reset.disabled, false);
  assert.equal(f.reboot.disabled, true);
});

test('confirmed reset locks controls until final outcome and offers reboot on both outcomes', (t) => {
  for (const success of [true, false]) {
    const f = fixture(t);
    f.reset.click();
    const request = f.sent.at(-1);
    assert.equal(request.group, 'factory_reset');
    assert.deepEqual(request.parameters, { confirm: true });
    assert.equal(f.reset.disabled, true);
    assert.equal(f.reboot.disabled, true);
    assert.equal(f.pane.isBusy(), true);
    f.reply(request, {}, true, false);
    assert.equal(f.pane.isBusy(), true);
    assert.equal(f.reset.disabled, true);
    f.reply(request, success ? { restart_required: true } :
      { error: 'factory_reset_failed', restart_required: true }, success);
    assert.equal(f.reboot.disabled, false);
    assert.match(f.status.textContent, success ? /completed/ : /failed/);
    f.statusReply(success ? 'succeeded' : 'failed', true, true);
    f.reboot.click();
    assert.equal(f.sent.at(-1).action, 'reboot');
    f.reply(f.sent.at(-1), {});
    assert.equal(f.reset.disabled, true);
    assert.equal(f.reboot.disabled, true);
    assert.equal(f.status.textContent, 'Reboot requested.');
  }
});

test('backend eligibility and other maintenance prevent action', (t) => {
  const f = fixture(t);
  f.pane.setMaintenanceBusy(true);
  f.reset.click();
  assert.equal(f.confirmations(), 0);
  f.pane.setMaintenanceBusy(false);
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
  f.statusReply('failed', false, true);
  assert.equal(f.pane.isBusy(), false);
  assert.equal(f.reboot.disabled, false);
  assert.equal(f.sent.filter(item => item.action === 'reset').length, 1);
});

test('transport timeout with string request ID locks until an authoritative status reply', (t) => {
  const f = fixture(t);
  f.reset.click();
  const request = f.sent.at(-1);
  f.pane.onRequestTimeout({ requestId: String(request.requestId), moduleAction: 'factory_reset:reset' });
  assert.equal(f.reset.disabled, true);
  assert.equal(f.sent.at(-1).action, 'read_status');
  f.reply(request, { restart_required: true }); // Late transport replies cannot unlock this operation.
  assert.equal(f.reset.disabled, true);
  f.statusReply('succeeded', false, true);
  assert.equal(f.reboot.disabled, false);
});

test('rejection before command execution does not offer reboot', (t) => {
  const f = fixture(t);
  f.reset.click();
  f.reply(f.sent.at(-1), { error: 'factory_reset_not_allowed' }, false);
  assert.equal(f.reboot.disabled, true);
  f.statusReply('idle', false, false);
  assert.equal(f.reboot.disabled, true);
});
