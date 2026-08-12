import assert from 'node:assert/strict';
import test from 'node:test';
import {
  formatInterfaceWarnings,
  isCurrentSettingsResponse,
  networkActionState,
  networkSettingsEqual,
  normalizeNetworkSettings
} from '../public/js/pages/network.js';

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
    formatInterfaceWarnings({ warning: ['This interface probably belongs to a PLC/HomePlug adapter.'] }),
    {
      text: 'This interface probably belongs to a PLC/HomePlug adapter.',
      visible: true
    }
  );
  assert.deepEqual(formatInterfaceWarnings({ warning: [] }), { text: '', visible: false });
});

test('normalizes network settings for dirty-state comparison', () => {
  const baseline = {
    dhcp_ipv4: false,
    dhcp_ipv6: true,
    ipv4_addresses: [' 192.168.1.20/24 '],
    gateway: ' 192.168.1.1 ',
    dns: ['192.168.1.1']
  };
  assert.equal(networkSettingsEqual(baseline, normalizeNetworkSettings(baseline)), true);
  assert.equal(networkSettingsEqual(baseline, { ...baseline, gateway: '192.168.1.2' }), false);
  assert.deepEqual(normalizeNetworkSettings({ dhcp_ipv4: true, gateway: '192.168.1.1' }), {
    dhcp_ipv4: true,
    dhcp_ipv6: false,
    ipv4_addresses: [],
    gateway: '',
    dns: []
  });
});

test('disables Apply for unsaved edits but keeps Save and Reset available', () => {
  assert.deepEqual(networkActionState({ loaded: true, editable: true, dirty: true, userOverride: true }), {
    saveDisabled: false,
    resetDisabled: false,
    applyDisabled: true
  });
  assert.equal(networkActionState({ loaded: true, editable: true, dirty: false, userOverride: true }).applyDisabled, false);
});
