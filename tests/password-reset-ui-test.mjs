import assert from 'node:assert/strict';
import test from 'node:test';
import { mountPasswordReset, resetHint } from '../public/js/ui/passwordReset.js';
import { formatAuthError } from '../public/js/ui/authErrors.js';

function fixture(overrides = {}, initialStatus = { available: true, remainingSeconds: 60, windowSeconds: 60 }) {
  const nodes = {};
  for (const name of ['button', 'label', 'info', 'description', 'error']) nodes[name] = {
    hidden: false, disabled: false, textContent: '',
    attributes: {},
    setAttribute(name, value) { this.attributes[name] = value; },
    addEventListener(_, fn) { this.click = fn; },
    removeEventListener() { this.click = null; }
  };
  const container = { innerHTML: '', querySelector(selector) { return nodes[selector.match(/data-reset-(\w+)/)[1]]; } };
  let time = 0, timer = null, reads = 0, resets = 0, setups = 0;
  const dependencies = {
    now: () => time,
    schedule: fn => { timer = fn; return 1; },
    cancel: () => { timer = null; },
    confirm: () => true,
    readStatus: async () => { reads++; return { passwordReset: { available: true, remainingSeconds: 20, windowSeconds: 60 } }; },
    reset: async () => { resets++; },
    onSetup: () => { setups++; },
    ...overrides
  };
  const dispose = mountPasswordReset(container, initialStatus, dependencies);
  return { nodes, container, dispose, advance(ms) { time += ms; timer?.(); },
    get reads() { return reads; }, get resets() { return resets; }, get setups() { return setups; },
    get timer() { return timer; } };
}

test('countdown expires without network polling', () => {
  const f = fixture();
  assert.match(f.container.innerHTML, /Reset password/);
  assert.match(f.container.innerHTML, /Forgotten password\?/);
  assert.equal(f.nodes.label.hidden, false);
  assert.equal(f.nodes.label.textContent, 'Forgotten password');
  assert.equal(f.nodes.info.hidden, false);
  assert.equal(f.nodes.button.hidden, false);
  assert.match(f.nodes.info.attributes['data-tooltip'], /60-second window/);
  assert.match(f.nodes.info.attributes['data-tooltip'], /60 seconds remain/);
  f.advance(59000); assert.match(f.nodes.info.attributes['data-tooltip'], /1 seconds remain/);
  f.advance(1000); assert.equal(f.nodes.button.hidden, true);
  assert.equal(f.nodes.label.hidden, false);
  assert.match(f.nodes.info.attributes['data-tooltip'], /Power-cycle and reopen this page/);
  assert.equal(f.reads, 0); assert.equal(f.timer, null); f.dispose();
});

test('configured reset window is reflected in the hint and countdown', () => {
  const f = fixture({}, { available: true, remainingSeconds: 150, windowSeconds: 180 });
  assert.match(f.nodes.info.attributes['data-tooltip'], /180-second window/);
  assert.match(f.nodes.info.attributes['data-tooltip'], /150 seconds remain/);
  f.dispose();
});

test('warm, consumed and expired share the power-cycle hint', () => {
  assert.equal(resetHint('warm_boot'), resetHint('expired'));
  assert.equal(resetHint('unavailable_this_boot'), resetHint('expired'));
  assert.notEqual(resetHint('boot_info_invalid'), resetHint('expired'));
  assert.notEqual(resetHint('disabled'), resetHint('expired'));
  assert.equal(resetHint('setup_required'), '');
});

test('disallowed host error explains how to reach the device', () => {
  assert.equal(
    formatAuthError('host_not_allowed'),
    'Open this page via the device IP address or add the hostname to allowed_hosts.'
  );
});

test('unavailable reset keeps the prompt visible and puts the power-cycle hint on the info icon', () => {
  const f = fixture();
  f.dispose();
  const nodes = f.nodes;
  // A fresh component with a warm-boot status renders the prompt as inactive text.
  const container = { innerHTML: '', querySelector(selector) { return nodes[selector.match(/data-reset-(\w+)/)[1]]; } };
  mountPasswordReset(container, { available: false, reason: 'warm_boot', windowSeconds: 180 }, {
    readStatus: async () => ({}), reset: async () => {}, onSetup: () => {},
    now: () => 0, schedule: () => 1, cancel: () => {}
  });
  assert.equal(nodes.button.hidden, true);
  assert.equal(nodes.label.hidden, false);
  assert.equal(nodes.label.textContent, 'Forgotten password?');
  assert.equal(nodes.info.hidden, false);
  assert.match(container.innerHTML, /Forgotten password\?/);
  assert.match(container.innerHTML, /Reset password/);
  assert.match(nodes.info.attributes['data-tooltip'], /Reset window: 180 seconds/);
  assert.match(nodes.info.attributes['data-tooltip'], /Power-cycle and reopen this page/);
  assert.equal(nodes.description.textContent, nodes.info.attributes['data-tooltip']);
});

test('confirmation cancellation leaves the timer running and does not reset', async () => {
  const f = fixture({ confirm: () => false });
  await f.nodes.button.click(); assert.equal(f.resets, 0); assert.ok(f.timer); f.dispose();
});

test('reset stops countdown immediately and enters unlimited first setup', async () => {
  let resolve;
  const f = fixture({ reset: () => new Promise(r => { resolve = r; }) });
  const operation = f.nodes.button.click();
  assert.equal(f.timer, null); assert.equal(f.nodes.button.disabled, true);
  await f.nodes.button.click();
  resolve(); await operation;
  assert.equal(f.setups, 1); assert.equal(f.reads, 0);
  f.advance(900000); assert.equal(f.timer, null); f.dispose();
});

test('transient lock error refreshes once and permits manual retry', async () => {
  let attempts = 0;
  const f = fixture({ reset: async () => { if (++attempts === 1) throw new Error('reset_temporarily_unavailable'); } });
  await f.nodes.button.click();
  assert.equal(f.reads, 1); assert.equal(f.nodes.button.disabled, false);
  assert.match(f.nodes.error.textContent, /temporarily busy/);
  assert.match(f.nodes.info.attributes['data-tooltip'], /20 seconds/);
  f.advance(1000); assert.equal(f.reads, 1);
  await f.nodes.button.click(); assert.equal(f.setups, 1); f.dispose();
});

test('disposed component does not navigate after an in-flight reset', async () => {
  let resolve;
  const f = fixture({ reset: () => new Promise(r => { resolve = r; }) });
  const operation = f.nodes.button.click(); f.dispose(); resolve(); await operation;
  assert.equal(f.setups, 0); assert.equal(f.timer, null);
});
