import assert from 'node:assert/strict';
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import test, { after } from 'node:test';
import { pathToFileURL } from 'node:url';

// Keep module loading compatible with Node versions without --experimental-default-type.
const root = mkdtempSync(path.join(tmpdir(), 'everest-ui-safety-test-'));
cpSync(path.resolve('public/js'), path.join(root, 'js'), { recursive: true });
writeFileSync(path.join(root, 'package.json'), '{"type":"module"}\n');
after(() => rmSync(root, { recursive: true, force: true }));
const { safetyWriteRequiresReload, getSafetyWriteErrorMessage, filterSafetyEstopInstances } =
  await import(pathToFileURL(path.join(root, 'js/pages/safety.js')).href);

test('unavailable and refreshed caches lock the selected editor until reload', () => {
  assert.equal(safetyWriteRequiresReload({ cache_state: 'unavailable' }), true);
  assert.equal(safetyWriteRequiresReload({ cache_state: 'refreshed' }), true);
  assert.equal(safetyWriteRequiresReload({ cache_state: 'unchanged' }), false);
  assert.equal(safetyWriteRequiresReload({}), false);
  assert.equal(safetyWriteRequiresReload(), false);
});

test('failed flash with recovered settings remains a failure and requests reload', () => {
  const message = getSafetyWriteErrorMessage({
    error: 'safety_controller_flash_failed', flash_outcome: 'nonzero_exit', exit_code: 7,
    cache_state: 'refreshed', recovery_error: 'internal detail', restart_error: 'rpc_api_not_connected'
  });
  assert.match(message, /Unable to apply/);
  assert.match(message, /Current settings were read back/);
  assert.match(message, /Reload/);
  assert.doesNotMatch(message, /internal detail|rpc_api_not_connected/);
});

test('unrecoverable cache gives a concise readback requirement', () => {
  const message = getSafetyWriteErrorMessage({
    error: 'safety_controller_flash_failed', flash_outcome: 'timeout', cache_state: 'unavailable'
  });
  assert.match(message, /timed out/);
  assert.match(message, /readback is required/);
  assert.match(message, /backend logs/);
});

test('definite launch failure does not suggest cache invalidity', () => {
  const message = getSafetyWriteErrorMessage({
    error: 'safety_controller_flash_failed', flash_outcome: 'start_failed', cache_state: 'unchanged'
  });
  assert.match(message, /could not start/);
  assert.doesNotMatch(message, /unavailable|readback|Reload/);
});

const { renderSettingsMatrixBlock } = await import(pathToFileURL(path.join(root, 'js/ui/settingsMatrix.js')).href);
const { loadPageConfig } = await import(pathToFileURL(path.join(root, 'js/config/pageConfigAdapter.js')).href);
const { buildRequest } = await import(pathToFileURL(path.join(root, 'js/protocol/requestBuilder.js')).href);
const catalog = JSON.parse(readFileSync('public/config/parameter_catalog.json', 'utf8'));

test('E-stop selection retains original instance numbers and leaves other sections unchanged', () => {
  const block = loadPageConfig('safety', catalog).blocks.find(block => block.kind === 'settings_matrix');
  const original = structuredClone(block);
  const filtered = filterSafetyEstopInstances(block, { estops_0: { enabled: 'active-low' },
    estops_2: { enabled: 'disabled' } });
  assert.deepEqual(filtered.sections.find(section => section.id === 'estops').instances,
    [{ id: '0', display_name: 'E-Stop 1' }, { id: '2', display_name: 'E-Stop 3' }]);
  assert.deepEqual(filtered.sections.filter(section => section.id !== 'estops'),
    block.sections.filter(section => section.id !== 'estops'));
  assert.deepEqual(block, original);
  assert.equal(filterSafetyEstopInstances(block, {}).sections.some(section => section.id === 'estops'), false);
  const all = Object.fromEntries([0, 1, 2].map(index => [`estops_${index}`, {}]));
  assert.deepEqual(filterSafetyEstopInstances(block, all), block);
});

class MatrixElement {
  constructor(tag) {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.dataset = {};
    this.attributes = {};
    this.events = {};
    this.style = { setProperty() {} };
    this.className = '';
    this.value = '';
    this.checked = false;
    this.disabled = false;
    this.type = tag === 'input' ? 'text' : '';
    this.classList = {
      contains: (name) => this.className.split(' ').includes(name),
      toggle: (name, enabled) => {
        const classes = new Set(this.className.split(' ').filter(Boolean));
        if (enabled) classes.add(name); else classes.delete(name);
        this.className = [...classes].join(' ');
      },
      add: (name) => this.classList.toggle(name, true)
    };
  }
  appendChild(child) {
    child.parentElement = this;
    this.children.push(child);
    return child;
  }
  setAttribute(name, value) { this.attributes[name] = value; }
  addEventListener(type, listener) { (this.events[type] ||= []).push(listener); }
  dispatchEvent(event) { (this.events[event.type] || []).forEach((listener) => listener(event)); }
  reportValidity() {
    if (this.disabled) return true;
    const value = String(this.value).trim();
    if (value === '') return !this.required;
    if (this.type === 'number') {
      return /^[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$/.test(value) && Number.isFinite(Number(value));
    }
    return true;
  }
  focus() { this.focused = true; }
  querySelectorAll(selector) {
    const matches = (element) => selector.split(',').some((part) => {
      part = part.trim();
      return part.startsWith('.') ? element.classList.contains(part.slice(1)) : element.tagName === part.toUpperCase();
    });
    return this.children.flatMap((child) => [...(matches(child) ? [child] : []), ...child.querySelectorAll(selector)]);
  }
  closest(selector) {
    if (selector.startsWith('.') && this.classList.contains(selector.slice(1))) return this;
    return this.parentElement?.closest(selector);
  }
}

function createMatrix(suffix = '') {
  globalThis.document = { createElement: (tag) => new MatrixElement(tag) };
  const block = loadPageConfig('safety', catalog).blocks[0];
  block.sections.forEach((section) => { section.id += suffix; });
  const matrix = renderSettingsMatrixBlock(block, { buttonLabel: 'Save Configuration' });
  const control = (backendPath) => matrix.element.querySelectorAll('input, select')
    .find((element) => element.dataset.backendPath === backendPath);
  const setValues = (values) => {
    const parameters = structuredClone(matrix.requestResponseObject);
    Object.values(parameters).forEach((entry) => {
      entry.value = values[entry.backend_path] ?? (entry.value_type === 'boolean' ? false : entry.default_value ?? '');
    });
    matrix.setValues(parameters);
  };
  return { matrix, control, setValues };
}

test('catalog supplies E-stop polarities, contactor types, and validated layout dependencies', () => {
  const config = loadPageConfig('safety', catalog);
  const sections = config.blocks[0].sections;
  assert.equal(sections[0].title, 'Overtemperature Protection');
  assert.equal(sections[0].layout, 'instances_as_rows');
  assert.equal(sections[0].fields[1].enabled_by, 'overtemperature-protection');
  assert.equal(sections[0].fields[1].required, true);
  assert.equal(Object.hasOwn(sections[0].fields[1], 'default_value'), false);
  assert.deepEqual(sections[2].fields[0].enum_values, ['active-low', 'active-high', 'disabled']);
  assert.deepEqual(sections[1].fields[0].enum_values,
    ['disabled', 'without-feedback', 'with-feedback-normally-open', 'with-feedback-normally-closed']);
  const invalid = structuredClone(catalog);
  invalid.modules.safety.settings_matrix[0].fields[1].enabled_by = 'abort-temperature';
  assert.throws(() => loadPageConfig('safety', invalid), /invalid 'enabled_by'/);
  invalid.modules.safety.settings_matrix[0].layout = 'unknown';
  assert.throws(() => loadPageConfig('safety', invalid), /invalid 'layout'/);
  const invalidRequired = structuredClone(catalog);
  invalidRequired.modules.safety.settings_matrix[0].fields[1].required = 'yes';
  assert.throws(() => loadPageConfig('safety', invalidRequired), /invalid 'required'/);
});

test('PT1000 uses channel rows and E-stops have exactly one dropdown per labeled row', () => {
  const { matrix } = createMatrix();
  const tables = matrix.element.querySelectorAll('table');
  assert.equal(tables.length, 2);
  assert.equal(tables[0].attributes['aria-label'], 'Overtemperature Protection');
  const ptRows = tables[0].querySelectorAll('tbody')[0].children;
  assert.equal(ptRows.length, 4);
  ptRows.forEach((row, index) => {
    assert.equal(row.children.length, 3);
    assert.equal(row.querySelectorAll('label')[0].textContent, `PT1000 ${index + 1}`);
    assert.equal(row.querySelectorAll('input')[0].type, 'checkbox');
  });
  const estopRows = tables[1].querySelectorAll('tbody')[0].children;
  assert.equal(tables[1].querySelectorAll('thead').length, 0);
  assert.equal(estopRows.length, 3);
  estopRows.forEach((row, index) => {
    assert.equal(row.children[0].textContent, `E-Stop ${index + 1}`);
    assert.equal(row.children[0].scope, 'row');
    assert.equal(row.querySelectorAll('td').length, 1);
    assert.equal(row.querySelectorAll('select').length, 1);
  });
  assert.equal(matrix.element.querySelectorAll('.matrix-grid').length, 1, 'contactor layout stays unchanged');
});

test('filtered E-stop rows render and serialize only the selected original channels', () => {
  globalThis.document = { createElement: tag => new MatrixElement(tag) };
  const block = loadPageConfig('safety', catalog).blocks[0];
  const filtered = filterSafetyEstopInstances(block, { estops_0: {}, estops_2: {} });
  const matrix = renderSettingsMatrixBlock(filtered, { buttonLabel: 'Save Configuration' });
  const table = matrix.element.querySelectorAll('table').find(element => element.attributes['aria-label'] === 'Emergency Stops');
  const rows = table.querySelectorAll('tbody')[0].children;
  assert.deepEqual(rows.map(row => row.children[0].textContent), ['E-Stop 1', 'E-Stop 3']);
  assert.deepEqual(rows.map(row => row.querySelectorAll('select')[0].dataset.backendPath),
    ['estops_0.enabled', 'estops_2.enabled']);
  const paths = Object.values(matrix.requestResponseObject).map(entry => entry.backend_path);
  assert.equal(paths.includes('estops_1.enabled'), false);
  assert.equal(paths.includes('estops_2.enabled'), true);
  assert.equal(paths.includes('pt1000_0.abort-temperature'), true);
  const empty = renderSettingsMatrixBlock(filterSafetyEstopInstances(block, {}), { buttonLabel: 'Save Configuration' });
  assert.equal(empty.element.querySelectorAll('table').some(element => element.attributes['aria-label'] === 'Emergency Stops'), false);
});

test('disabled PT1000 inputs retain values and are included in Save payloads', () => {
  const { matrix, control, setValues } = createMatrix();
  setValues({
    'pt1000_0.overtemperature-protection': false,
    'pt1000_0.abort-temperature': 86.5,
    'pt1000_0.resistance-offset': -1.25
  });
  const enabled = control('pt1000_0.overtemperature-protection');
  const temperature = control('pt1000_0.abort-temperature');
  const offset = control('pt1000_0.resistance-offset');
  assert.equal(temperature.disabled, true);
  assert.equal(offset.disabled, true);
  assert.equal(temperature.closest('.matrix-cell').classList.contains('is-disabled'), true);
  for (const checked of [true, false, true, false]) {
    enabled.checked = checked;
    enabled.dispatchEvent({ type: 'change' });
    assert.equal(temperature.disabled, !checked);
    assert.equal(offset.disabled, !checked);
    assert.equal(temperature.value, 86.5);
    assert.equal(offset.value, -1.25);
  }
  const request = buildRequest('safety', 'write_settings', matrix.getValues(matrix.requestResponseObject));
  assert.deepEqual(request.parameters.pt1000_0, {
    'overtemperature-protection': false, 'abort-temperature': 86.5, 'resistance-offset': -1.25
  });
});

test('busy/read-only state composes with per-channel state and stays independent per controller', () => {
  const first = createMatrix('-controller-a');
  const second = createMatrix('-controller-b');
  for (const instance of [first, second]) {
    instance.setValues({ 'pt1000_1.overtemperature-protection': true });
  }
  first.matrix.setDisabled(true);
  assert.equal(first.control('pt1000_1.abort-temperature').disabled, true);
  assert.equal(first.control('pt1000_0.overtemperature-protection').disabled, true);
  assert.equal(second.control('pt1000_1.abort-temperature').disabled, false);
  first.matrix.setDisabled(false);
  assert.equal(first.control('pt1000_0.abort-temperature').disabled, true);
  assert.equal(first.control('pt1000_1.abort-temperature').disabled, false);
  assert.notEqual(first.control('pt1000_0.abort-temperature').id, second.control('pt1000_0.abort-temperature').id);
});

test('dropdown choices retain their individual E-stop and contactor backend mappings', () => {
  const { matrix, control, setValues } = createMatrix();
  const values = ['active-low', 'active-high', 'disabled'];
  setValues(Object.fromEntries(values.map((value, index) => [`estops_${index}.enabled`, value])));
  const request = buildRequest('safety', 'write_settings', matrix.getValues(matrix.requestResponseObject));
  values.forEach((value, index) => {
    assert.equal(request.parameters[`estops_${index}`].enabled, value);
    assert.deepEqual(control(`estops_${index}.enabled`).children.map((option) => option.value), values);
  });
});

test('unknown PT1000 values stay empty; only enabled channels require both values before Save', () => {
  const { matrix, control, setValues } = createMatrix();
  setValues({ 'estops_0.enabled': 'active-high' });
  const temperature = control('pt1000_0.abort-temperature');
  const offset = control('pt1000_0.resistance-offset');
  const enabled = control('pt1000_0.overtemperature-protection');
  const save = matrix.element.querySelectorAll('button').at(-1);
  const requests = [];
  matrix.bindSubmit(() => requests.push(buildRequest('safety', 'write_settings', matrix.getValues(matrix.requestResponseObject))));
  assert.equal(temperature.value, '');
  assert.equal(offset.value, '');
  assert.equal(temperature.required, true);
  assert.equal(temperature.type, 'number');
  save.dispatchEvent({ type: 'click' });
  assert.equal(requests.length, 1, 'disabled empty channels must not block an E-stop change');
  assert.equal(requests[0].parameters.estops_0.enabled, 'active-high');
  assert.deepEqual(requests[0].parameters.pt1000_0, {
    'overtemperature-protection': false, 'abort-temperature': '', 'resistance-offset': ''
  });

  enabled.checked = true;
  enabled.dispatchEvent({ type: 'change' });
  save.dispatchEvent({ type: 'click' });
  assert.equal(requests.length, 1);
  assert.equal(temperature.focused, true);
  assert.equal(enabled.disabled, false, 'validation failure must not leave the editor busy');
  temperature.value = '0';
  save.dispatchEvent({ type: 'click' });
  assert.equal(requests.length, 1, 'both values are required');
  assert.equal(offset.focused, true);
  offset.value = '0';
  save.dispatchEvent({ type: 'click' });
  assert.equal(requests.length, 2);
  assert.equal(requests[1].parameters.pt1000_0['abort-temperature'], 0);
  assert.equal(requests[1].parameters.pt1000_0['resistance-offset'], 0);
  for (const value of ['bad', '86.5abc', 'NaN', 'Infinity', '1e309', ' ']) {
    temperature.value = value;
    save.dispatchEvent({ type: 'click' });
    assert.equal(requests.length, 2, `invalid temperature ${value} must not be sent`);
  }
});

test('required values survive busy/read-only transitions without losing zero or allowing invalid Save', () => {
  const { matrix, control, setValues } = createMatrix();
  setValues({
    'pt1000_0.overtemperature-protection': true,
    'pt1000_0.abort-temperature': 0,
    'pt1000_0.resistance-offset': 0
  });
  const save = matrix.element.querySelectorAll('button').at(-1);
  let submissions = 0;
  matrix.bindSubmit(() => { submissions += 1; });
  matrix.setDisabled(true);
  save.dispatchEvent({ type: 'click' });
  assert.equal(submissions, 0);
  matrix.setDisabled(false);
  assert.equal(control('pt1000_0.abort-temperature').value, 0);
  save.dispatchEvent({ type: 'click' });
  assert.equal(submissions, 1);
  const enabled = control('pt1000_0.overtemperature-protection');
  enabled.checked = false;
  enabled.dispatchEvent({ type: 'change' });
  matrix.setDisabled(true);
  matrix.setDisabled(false);
  assert.equal(control('pt1000_0.abort-temperature').disabled, true);
  assert.equal(control('pt1000_0.resistance-offset').value, 0);
});
