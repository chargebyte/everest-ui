import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const source = readFileSync('public/js/ui/featureAvailability.js', 'utf8');
const { createFeatureAvailability } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

test('optional pages remain unavailable until successful discovery', () => {
  const visible = {};
  const features = createFeatureAvailability({
    setPageAvailable: (route, value) => { visible[route] = value; },
    getActiveRoute: () => 'everest',
    returnToDefault: () => assert.fail('unrelated route must not change')
  });
  assert.equal(features.allows('safety'), false);
  assert.equal(features.allows('network'), false);
  features.update();
  assert.deepEqual(visible, { network: false, safety: false });
  assert.equal(features.allows('ssh'), true);
  features.update({ network: true, safety: false });
  assert.equal(features.allows('network'), true);
  assert.equal(features.allows('safety'), false);
  features.update({ network: 'true', safety: true });
  assert.deepEqual(visible, { network: false, safety: true });
});

test('reconnect and failed discovery revoke visibility and leave disabled active pages', () => {
  let activeRoute = 'everest';
  let redirects = 0;
  const features = createFeatureAvailability({
    setPageAvailable() {},
    getActiveRoute: () => activeRoute,
    returnToDefault() { activeRoute = 'everest'; redirects++; }
  });
  features.update({ network: true, safety: true });
  activeRoute = 'safety';
  features.update({ network: true, safety: false });
  assert.equal(activeRoute, 'everest');
  features.update({ network: true, safety: true });
  activeRoute = 'network';
  features.update();
  assert.equal(activeRoute, 'everest');
  assert.equal(features.allows('network'), false);
  assert.equal(features.allows('safety'), false);
  features.update({ safety: true });
  assert.equal(features.allows('safety'), true);
  assert.equal(redirects, 2);
});
