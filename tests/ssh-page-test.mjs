import assert from 'assert';
import fs from 'fs';
import os from 'os';
import path from 'path';
import { pathToFileURL } from 'url';

class FakeClassList {
  constructor() {
    this.classes = new Set();
  }

  add(...classes) {
    classes.forEach((className) => this.classes.add(className));
  }

  remove(...classes) {
    classes.forEach((className) => this.classes.delete(className));
  }
}

class FakeElement {
  constructor(tagName = 'div') {
    this.tagName = tagName.toUpperCase();
    this.children = [];
    this.eventListeners = {};
    this.classList = new FakeClassList();
    this.id = '';
    this.className = '';
    this.textContent = '';
    this.hidden = false;
    this.disabled = false;
    this.value = '';
    this.type = '';
    this.ownerDocument = null;
  }

  set innerHTML(markup) {
    this.children = [];
    const idPattern = /<([a-z0-9-]+)\b[^>]*\bid="([^"]+)"[^>]*>/gi;
    let match = idPattern.exec(markup);
    while (match) {
      const element = new FakeElement(match[1]);
      element.id = match[2];
      element.ownerDocument = this.ownerDocument;
      this.appendChild(element);
      match = idPattern.exec(markup);
    }
  }

  appendChild(child) {
    child.ownerDocument = this.ownerDocument;
    this.children.push(child);
    return child;
  }

  querySelector(selector) {
    if (!selector.startsWith('#')) {
      return null;
    }
    return this.findById(selector.slice(1));
  }

  findById(id) {
    if (this.id === id) {
      return this;
    }
    for (const child of this.children) {
      const found = child.findById(id);
      if (found) {
        return found;
      }
    }
    return null;
  }

  addEventListener(type, listener) {
    this.eventListeners[type] = this.eventListeners[type] || [];
    this.eventListeners[type].push(listener);
  }

  dispatchEvent(event) {
    (this.eventListeners[event.type] || []).forEach((listener) => listener(event));
  }

  click() {
    this.dispatchEvent({ type: 'click' });
  }
}

function createContainer() {
  const document = {
    createElement(tagName) {
      const element = new FakeElement(tagName);
      element.ownerDocument = document;
      return element;
    }
  };

  const container = new FakeElement('div');
  container.ownerDocument = document;
  globalThis.document = document;
  return container;
}

function copyDirectory(source, target) {
  fs.mkdirSync(target, { recursive: true });
  for (const entry of fs.readdirSync(source, { withFileTypes: true })) {
    const sourcePath = path.join(source, entry.name);
    const targetPath = path.join(target, entry.name);
    if (entry.isDirectory()) {
      copyDirectory(sourcePath, targetPath);
    } else {
      fs.copyFileSync(sourcePath, targetPath);
    }
  }
}

function createTestModuleTree() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'everest-ui-ssh-page-test-'));
  copyDirectory(path.resolve('public', 'js'), path.join(root, 'public', 'js'));
  fs.writeFileSync(path.join(root, 'package.json'), '{"type":"module"}\n');
  return root;
}

function renderPage({ renderSshPage, state, sendPayload }) {
  state.connection.requestId = 0;
  const container = createContainer();
  const logs = [];
  const page = renderSshPage(container, {
    sendPayload,
    addLog(message) {
      logs.push(message);
    }
  });
  return { container, page, logs };
}

function readStatusLoaded(page) {
  page.onConnectionChange(true);
  page.onMessage({
    type: 'ssh.read.result',
    parameters: {
      socket_active: false,
      socket_enabled: false
    }
  });
}

function fillPassword(container, password) {
  const passwordInput = container.querySelector('#ssh-password');
  const passwordConfirmInput = container.querySelector('#ssh-password-confirm');
  passwordInput.value = password;
  passwordConfirmInput.value = password;
  passwordInput.dispatchEvent({ type: 'input' });
  passwordConfirmInput.dispatchEvent({ type: 'input' });
}

function actionNames(requests) {
  return requests.map((request) => request.action);
}

async function main() {
  const testModuleRoot = createTestModuleTree();
  const [{ renderSshPage }, { state }] = await Promise.all([
    import(pathToFileURL(path.join(testModuleRoot, 'public', 'js', 'pages', 'ssh.js')).href),
    import(pathToFileURL(path.join(testModuleRoot, 'public', 'js', 'state.js')).href)
  ]);

  {
    const requests = [];
    const { container, page } = renderPage({
      renderSshPage,
      state,
      sendPayload(request) {
        requests.push(request);
        return { ok: true };
      }
    });

    readStatusLoaded(page);
    fillPassword(container, 'secret123');

    container.querySelector('#ssh-enable').click();
    assert.deepEqual(actionNames(requests), ['read', 'set_password']);

    page.onMessage({ type: 'ssh.set_password.ack' });
    assert.deepEqual(actionNames(requests), ['read', 'set_password', 'enable']);

    page.onMessage({ type: 'ssh.enable.ack' });
    assert.deepEqual(actionNames(requests), ['read', 'set_password', 'enable', 'read']);
  }

  {
    const requests = [];
    const { container, page } = renderPage({
      renderSshPage,
      state,
      sendPayload(request) {
        requests.push(request);
        return { ok: request.action !== 'set_password', error: 'offline' };
      }
    });

    readStatusLoaded(page);
    fillPassword(container, 'secret123');

    container.querySelector('#ssh-enable').click();

    assert.deepEqual(actionNames(requests), ['read', 'set_password']);
    assert.equal(container.querySelector('#ssh-enable').disabled, false);
    assert.equal(container.querySelector('#ssh-password').disabled, false);
  }

  {
    const requests = [];
    const { container, page } = renderPage({
      renderSshPage,
      state,
      sendPayload(request) {
        requests.push(request);
        return { ok: request.action !== 'enable', error: 'offline' };
      }
    });

    readStatusLoaded(page);
    fillPassword(container, 'secret123');

    container.querySelector('#ssh-enable').click();
    page.onMessage({ type: 'ssh.set_password.ack' });

    assert.deepEqual(actionNames(requests), ['read', 'set_password', 'enable']);
    assert.equal(container.querySelector('#ssh-enable').disabled, false);
    assert.equal(container.querySelector('#ssh-password').disabled, false);
  }

  console.log('ssh-page-test: ok');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
