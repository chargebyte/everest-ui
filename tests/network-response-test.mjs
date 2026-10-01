import assert from 'node:assert/strict';
import test from 'node:test';
import {
  formatInterfaceWarnings,
  isValidIpv4Address,
  isValidIpv4PrefixLength,
  isCurrentSettingsResponse,
  isSuccessfulApplyResponse,
  networkActionState,
  networkFieldDisabledState,
  networkSettingsEqual,
  normalizeNetworkSettings
} from '../public/js/pages/network.js';
import { hasUnsavedSettings } from '../public/js/pages/everest.js';
import {
  kSettingsTableApplyingLabel,
  kSettingsTableReloadLabel,
  kSettingsTableReloadingLabel,
  kUnassignedValueHint
} from '../public/js/ui/settingsTable.js';

test('ignores settings responses for an older request or interface', () => {
  const current = { requestId: 2, parameters: { interface: 'eth1' } };
  const oldRequest = { requestId: 1, parameters: { interface: 'eth0' } };
  const wrongInterface = { requestId: 2, parameters: { interface: 'eth0' } };

  assert.equal(isCurrentSettingsResponse(current, 2, 'eth1'), true);
  assert.equal(isCurrentSettingsResponse(oldRequest, 2, 'eth1'), false);
  assert.equal(isCurrentSettingsResponse(wrongInterface, 2, 'eth1'), false);
});

test('keeps interface warnings visible after settings data is loaded', () => {
  assert.deepEqual(
    formatInterfaceWarnings({ warning: ['This interface is likely used for ISO high level communications (PLC/HomePlug).'] }),
    {
      text: 'This interface is likely used for ISO high level communications (PLC/HomePlug).',
      visible: true
    }
  );
  assert.deepEqual(formatInterfaceWarnings({ warning: [] }), { text: '', visible: false });
});

test('normalizes network settings for dirty-state comparison', () => {
  const baseline = {
    dhcp_ipv4: false,
    dhcp_ipv6: true,
    ipv4_address: ' 192.168.1.20 ',
    ipv4_prefix_length: 24,
    gateway: ' 192.168.1.1 ',
    dns: ['192.168.1.1']
  };
  assert.equal(networkSettingsEqual(baseline, normalizeNetworkSettings(baseline)), true);
  assert.equal(networkSettingsEqual(baseline, { ...baseline, gateway: '192.168.1.2' }), false);
  assert.deepEqual(normalizeNetworkSettings({ dhcp_ipv4: true, gateway: '192.168.1.1' }), {
    dhcp_ipv4: true,
    dhcp_ipv6: false,
    ipv4_address: '',
    ipv4_prefix_length: 24,
    gateway: '',
    dns: []
  });
});

test('validates bare IPv4 addresses and prefix lengths', () => {
  assert.equal(isValidIpv4Address('192.168.1.20'), true);
  assert.equal(isValidIpv4Address('256.168.1.20'), false);
  assert.equal(isValidIpv4Address('192.168.1'), false);
  assert.equal(isValidIpv4PrefixLength('24'), true);
  assert.equal(isValidIpv4PrefixLength('0'), true);
  assert.equal(isValidIpv4PrefixLength('32'), true);
  assert.equal(isValidIpv4PrefixLength('33'), false);
  assert.equal(isValidIpv4PrefixLength('24.5'), false);
});

test('disables Apply for unsaved edits but keeps Save and Reset available', () => {
  assert.deepEqual(networkActionState({ loaded: true, editable: true, dirty: true, userOverride: true }), {
    saveDisabled: false,
    resetDisabled: false,
    applyDisabled: true
  });
  assert.equal(networkActionState({ loaded: true, editable: true, dirty: true, userOverride: false, resetStaged: true }).applyDisabled, false);
  assert.equal(networkActionState({ loaded: true, editable: true, dirty: false, userOverride: true, resetStaged: true }).saveDisabled, true);
  assert.equal(networkActionState({ loaded: true, editable: true, dirty: false, userOverride: true }).applyDisabled, false);
});

test('disables static fields in DHCP mode', () => {
  assert.deepEqual(networkFieldDisabledState({
    loaded: true,
    editable: true,
    resetStaged: false,
    dhcpIpv4: true,
  }), {
    mode: false,
    ipv4Address: true,
    ipv4PrefixLength: true,
    gateway: true,
    dns: true
  });
});

test('enables static fields in static mode', () => {
  const state = networkFieldDisabledState({
    loaded: true,
    editable: true,
    resetStaged: false,
    dhcpIpv4: false
  });
  assert.equal(state.mode, false);
  assert.equal(state.ipv4Address, false);
  assert.equal(state.ipv4PrefixLength, false);
  assert.equal(state.dns, false);
});

test('keeps all network fields disabled until editable settings are loaded or while reset is staged', () => {
  for (const gating of [
    { loaded: false, editable: true, resetStaged: false },
    { loaded: true, editable: false, resetStaged: false },
    { loaded: true, editable: true, resetStaged: true }
  ]) {
    const state = networkFieldDisabledState({
      ...gating,
      dhcpIpv4: false
    });
    assert.ok(Object.values(state).every(Boolean));
  }
});

test('accepts both successful Apply acknowledgements and results', () => {
  assert.equal(isSuccessfulApplyResponse({ type: 'network.apply.ack' }), true);
  assert.equal(isSuccessfulApplyResponse({ type: 'network.apply.result' }), true);
  assert.equal(isSuccessfulApplyResponse({ type: 'network.apply.error' }), false);
});

test('describes empty EVerest values as default-preserving', () => {
  assert.match(kUnassignedValueHint, /non-boolean/);
  assert.match(kUnassignedValueHint, /default/);
  assert.match(kUnassignedValueHint, /Checkboxes always apply/);
});

test('detects EVerest settings that would be discarded by reload', () => {
  const baseline = { setting: { value: 'old' } };
  assert.equal(hasUnsavedSettings({ setting: { value: 'old' } }, baseline), false);
  assert.equal(hasUnsavedSettings({ setting: { value: 'new' } }, baseline), true);
  assert.equal(hasUnsavedSettings({ setting: { value: 'new' } }, null), false);
});

test('exposes distinct Save and Reload progress labels', () => {
  assert.equal(kSettingsTableApplyingLabel, 'Please wait while applying...');
  assert.equal(kSettingsTableReloadLabel, 'Reload Configuration');
  assert.equal(kSettingsTableReloadingLabel, 'Reloading configuration...');
});
