import assert from 'node:assert/strict';
import test from 'node:test';
import { isCurrentSettingsResponse } from '../public/js/pages/network.js';

test('ignores settings responses for an older request or interface', () => {
  const current = { requestId: 2, parameters: { interface: 'eth1' } };
  const oldRequest = { requestId: 1, parameters: { interface: 'eth0' } };
  const wrongInterface = { requestId: 2, parameters: { interface: 'eth0' } };

  assert.equal(isCurrentSettingsResponse(current, 2, 'eth1'), true);
  assert.equal(isCurrentSettingsResponse(oldRequest, 2, 'eth1'), false);
  assert.equal(isCurrentSettingsResponse(wrongInterface, 2, 'eth1'), false);
});
