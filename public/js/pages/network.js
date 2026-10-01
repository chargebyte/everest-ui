// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { state } from '../state.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';

const kGroup = MODULE_IDS.NETWORK;

export function isCurrentSettingsResponse(message, requestId, interfaceName) {
  return message?.requestId === requestId && message.parameters?.interface === interfaceName;
}

export function isSuccessfulApplyResponse(message) {
  return message?.type === 'network.apply.ack' || message?.type === 'network.apply.result';
}

export function formatInterfaceWarnings(info) {
  const warnings = Array.isArray(info?.warning) ? info.warning : [];
  return {
    text: warnings.join(' '),
    visible: warnings.length > 0
  };
}

export function normalizeNetworkSettings(settings) {
  const dhcpIpv4 = settings?.dhcp_ipv4 === true;
  return {
    dhcp_ipv4: dhcpIpv4,
    dhcp_ipv6: settings?.dhcp_ipv6 === true,
    ipv4_address: dhcpIpv4 ? '' : String(settings?.ipv4_address || '').trim(),
    ipv4_prefix_length: Number.isInteger(settings?.ipv4_prefix_length)
      ? settings.ipv4_prefix_length
      : 24,
    gateway: dhcpIpv4 ? '' : String(settings?.gateway || '').trim(),
    dns: dhcpIpv4 ? [] : Array.isArray(settings?.dns)
      ? settings.dns.map((value) => String(value).trim()).filter(Boolean)
      : []
  };
}

export function isValidIpv4Address(value) {
  const octets = String(value).trim().split('.');
  return octets.length === 4 && octets.every((octet) =>
    /^\d{1,3}$/.test(octet) && Number(octet) >= 0 && Number(octet) <= 255);
}

export function isValidIpv4PrefixLength(value) {
  return /^\d{1,2}$/.test(String(value)) && Number(value) >= 0 && Number(value) <= 32;
}

export function networkSettingsEqual(left, right) {
  return JSON.stringify(normalizeNetworkSettings(left)) === JSON.stringify(normalizeNetworkSettings(right));
}

export function networkActionState({ loaded, editable, dirty, userOverride, resetStaged = false }) {
  return {
    saveDisabled: !loaded || !editable || resetStaged,
    resetDisabled: !loaded || !editable || userOverride !== true,
    applyDisabled: !loaded || !editable || (dirty && !resetStaged)
  };
}

export function networkFieldDisabledState({ loaded, editable, resetStaged, dhcpIpv4 }) {
  const globallyDisabled = !loaded || !editable || resetStaged;
  const staticFieldsDisabled = globallyDisabled || dhcpIpv4;
  return {
    mode: globallyDisabled,
    ipv4Address: staticFieldsDisabled,
    ipv4PrefixLength: staticFieldsDisabled,
    gateway: staticFieldsDisabled,
    dns: staticFieldsDisabled
  };
}

export function renderNetworkPage(container, { sendPayload, addLog }) {
  container.innerHTML = '';

  const page = document.createElement('div');
  page.className = 'page';
  page.innerHTML = `
    <h1>Network Configuration</h1>
    <section class="section network-warning-section">
      <p class="network-warning"><span class="network-warning-icon" aria-hidden="true">⚠</span>
        Changing network configuration can disconnect this Web UI and lock you out of the target.</p>
    </section>
    <section class="section">
      <h2>Interface</h2>
      <div class="form-grid network-interface-grid">
        <label class="label" for="network-interface">Network interface</label>
        <select class="input" id="network-interface"></select>
        <span class="label">Current status</span>
        <span id="network-interface-status" class="network-status">Loading interfaces...</span>
        <span class="label">Network file</span>
        <span id="network-file" class="network-status">-</span>
        <span class="label">Warnings</span>
        <span id="network-interface-warnings" class="network-status network-interface-warnings"></span>
      </div>
    </section>
    <section class="section" id="network-settings-section" hidden>
      <h2>IPv4 Configuration</h2>
      <div class="form-grid network-settings-grid">
        <span class="label">IPv4 method</span>
        <div class="network-ipv4-mode">
          <label><input id="network-dhcp" name="network-ipv4-mode" type="radio" value="dhcp" /> Use DHCP for IPv4</label>
          <label><input id="network-static-ip" name="network-ipv4-mode" type="radio" value="static" /> Use static IPv4 configuration</label>
        </div>
        <label class="label" id="network-address-label" for="network-address">IPv4 address / prefix</label>
        <div class="network-cidr-inputs">
          <input class="input" id="network-address" type="text" inputmode="decimal" autocomplete="off" placeholder="192.168.0.10" />
          <span aria-hidden="true">/</span>
          <input class="input" id="network-prefix" type="number" min="0" max="32" step="1" value="24" aria-label="IPv4 network prefix length" />
        </div>
        <label class="label" id="network-gateway-label" for="network-gateway">IPv4 gateway</label>
        <input class="input" id="network-gateway" type="text" placeholder="Optional" />
        <label class="label" id="network-dns-label" for="network-dns">DNS servers</label>
        <input class="input" id="network-dns" type="text" placeholder="Optional, comma separated" />
      </div>
      <p id="network-settings-warning" class="network-settings-warning" hidden></p>
      <div class="network-actions">
        <button class="btn" id="network-save" type="button">Save</button>
        <button class="btn btn-secondary network-reset" id="network-reset" type="button">Reset to factory defaults</button>
        <button class="btn" id="network-apply" type="button">Apply</button>
      </div>
    </section>
  `;
  container.appendChild(page);

  const interfaceSelect = page.querySelector('#network-interface');
  const statusElement = page.querySelector('#network-interface-status');
  const fileElement = page.querySelector('#network-file');
  const warningsElement = page.querySelector('#network-interface-warnings');
  const settingsSection = page.querySelector('#network-settings-section');
  const warningElement = page.querySelector('#network-settings-warning');
  const dhcpElement = page.querySelector('#network-dhcp');
  const staticIpv4Element = page.querySelector('#network-static-ip');
  const addressElement = page.querySelector('#network-address');
  const prefixElement = page.querySelector('#network-prefix');
  const gatewayElement = page.querySelector('#network-gateway');
  const dnsElement = page.querySelector('#network-dns');
  const staticLabels = [
    page.querySelector('#network-address-label'),
    page.querySelector('#network-gateway-label'),
    page.querySelector('#network-dns-label')
  ];
  const saveButton = page.querySelector('#network-save');
  const resetButton = page.querySelector('#network-reset');
  const applyButton = page.querySelector('#network-apply');
  let selectedInfo = null;
  let editable = false;
  let settingsLoaded = false;
  let pendingSettingsInterface = '';
  let pendingSettingsRequestId = null;
  let pendingResetRequestId = null;
  let pendingWriteRequestId = null;
  let pendingWriteSettings = null;
  let pendingWriteInterface = '';
  let baselineSettings = null;
  let userOverride = false;
  let dirty = false;
  let dhcpIpv6 = false;
  let resetStaged = false;

  function formatSendStatus(result) {
    if (result.ok) {
      return 'sent';
    }

    const details = [result.error, result.websocketState].filter(Boolean).join(', ');
    return `rejected${details ? `: ${details}` : ''}`;
  }

  function setWarning(message) {
    warningElement.textContent = message || '';
    warningElement.hidden = !message;
  }

  function updateActionButtons() {
    const buttons = networkActionState({
      loaded: settingsLoaded,
      editable,
      dirty,
      userOverride,
      resetStaged
    });
    saveButton.disabled = buttons.saveDisabled;
    resetButton.disabled = (!resetStaged && buttons.resetDisabled) || pendingResetRequestId !== null;
    resetButton.textContent = resetStaged ? 'Cancel reset' : 'Reset to factory defaults';
    applyButton.disabled = buttons.applyDisabled;
  }

  function updateDirtyState(showHint = true) {
    dirty = baselineSettings !== null && !networkSettingsEqual(collectSettings(), baselineSettings);
    updateActionButtons();
    if (dirty && showHint) {
      setWarning('Unsaved changes. Save the changes first, or Reset to factory defaults.');
    }
  }

  function updateFieldStates() {
    const disabled = networkFieldDisabledState({
      loaded: settingsLoaded,
      editable,
      resetStaged,
      dhcpIpv4: dhcpElement.checked
    });
    dhcpElement.disabled = disabled.mode;
    staticIpv4Element.disabled = disabled.mode;
    addressElement.disabled = disabled.ipv4Address;
    prefixElement.disabled = disabled.ipv4PrefixLength;
    gatewayElement.disabled = disabled.gateway;
    dnsElement.disabled = disabled.dns;
    staticLabels.forEach((label) => { label.hidden = dhcpElement.checked; });
    page.querySelector('.network-cidr-inputs').hidden = dhcpElement.checked;
    gatewayElement.hidden = dhcpElement.checked;
    dnsElement.hidden = dhcpElement.checked;
    updateActionButtons();
  }

  function renderInterfaceInfo(info) {
    selectedInfo = info;
    editable = info?.editable === true;
    statusElement.textContent = info
      ? `${info.kind || 'unknown'}; ${info.operational_state || 'unknown'}; setup ${info.setup_state || 'unknown'}`
      : 'Select an interface';
    fileElement.textContent = info?.network_file || 'No effective Network File reported';
    const interfaceWarnings = formatInterfaceWarnings(info);
    warningsElement.textContent = interfaceWarnings.text;
    warningsElement.hidden = !interfaceWarnings.visible;
    settingsLoaded = false;
    baselineSettings = null;
    userOverride = false;
    dirty = false;
    pendingSettingsInterface = '';
    pendingSettingsRequestId = null;
    pendingResetRequestId = null;
    pendingWriteRequestId = null;
    pendingWriteSettings = null;
    pendingWriteInterface = '';
    resetStaged = false;
    settingsSection.hidden = true;
    updateFieldStates();
    setWarning(info && !editable ? 'This interface cannot be edited safely by the Web UI.' : '');
  }

  function populateInterfaces(interfaces) {
    interfaceSelect.replaceChildren();
    interfaces.forEach((info) => {
      const option = document.createElement('option');
      option.value = info.name;
      option.textContent = `${info.name} (${info.kind || 'unknown'})`;
      interfaceSelect.appendChild(option);
    });
    if (interfaces.length === 0) {
      renderInterfaceInfo(null);
      statusElement.textContent = 'No networkd interfaces found';
      return;
    }
    const selected = state.network.selectedInterface || interfaces[0].name;
    interfaceSelect.value = interfaces.some((info) => info.name === selected)
      ? selected
      : interfaces[0].name;
    state.network.selectedInterface = interfaceSelect.value;
    renderInterfaceInfo(interfaces.find((info) => info.name === interfaceSelect.value));
    requestSettings(interfaceSelect.value);
  }

  function requestSettings(name) {
    pendingSettingsInterface = name;
    const request = buildRequest(kGroup, 'read_settings', {
      interface: { backend_path: 'interface', value_type: 'string', value: name }
    });
    pendingSettingsRequestId = request.requestId;
    const result = sendPayload(request);
    if (!result.ok) {
      pendingSettingsInterface = '';
      pendingSettingsRequestId = null;
    }
    addLog(`${kGroup}.read_settings ${formatSendStatus(result)}`);
  }

  function setSettings(parameters) {
    pendingSettingsInterface = '';
    pendingSettingsRequestId = null;
    pendingWriteRequestId = null;
    pendingWriteSettings = null;
    pendingWriteInterface = '';
    pendingResetRequestId = null;
    selectedInfo = state.network.interfaces.find((info) => info.name === interfaceSelect.value) || selectedInfo;
    if (parameters.editable === false) {
      editable = false;
    }
    fileElement.textContent = parameters.network_file || 'No effective Network File reported';
    dhcpElement.checked = parameters.dhcp_ipv4 === true;
    staticIpv4Element.checked = !dhcpElement.checked;
    dhcpIpv6 = parameters.dhcp_ipv6 === true;
    addressElement.value = parameters.ipv4_address || '';
    prefixElement.value = Number.isInteger(parameters.ipv4_prefix_length)
      ? String(parameters.ipv4_prefix_length)
      : '24';
    gatewayElement.value = parameters.gateway || '';
    dnsElement.value = Array.isArray(parameters.dns) ? parameters.dns.join(', ') : '';
    settingsLoaded = true;
    settingsSection.hidden = false;
    baselineSettings = normalizeNetworkSettings(collectSettings());
    userOverride = parameters.user_override === true;
    resetStaged = parameters.reset_staged === true;
    dirty = false;
    updateFieldStates();
  }

  function collectSettings() {
    const dns = dnsElement.value.split(',').map((value) => value.trim()).filter(Boolean);
    return {
      interface: interfaceSelect.value,
      dhcp_ipv4: dhcpElement.checked,
      dhcp_ipv6: dhcpIpv6,
      ipv4_address: dhcpElement.checked ? '' : addressElement.value.trim(),
      ipv4_prefix_length: Number(prefixElement.value),
      gateway: dhcpElement.checked ? '' : gatewayElement.value.trim(),
      dns: dhcpElement.checked ? [] : dns
    };
  }

  function handleFormChange() {
    updateFieldStates();
    updateDirtyState();
  }

  function sendAction(action, parameters) {
    const requestResponseObject = {};
    Object.entries(parameters).forEach(([key, value]) => {
      requestResponseObject[key] = {
        backend_path: key,
        value_type: typeof value === 'boolean' ? 'boolean' : 'string',
        value
      };
    });

    const request = buildRequest(kGroup, action, requestResponseObject);
    const result = sendPayload(request);
    addLog(`${kGroup}.${action} ${formatSendStatus(result)}`);
    if (!result.ok) {
      setWarning(`Unable to send network ${action}: ${formatSendStatus(result)}`);
    }
    return result;
  }

  interfaceSelect.addEventListener('change', () => {
    state.network.selectedInterface = interfaceSelect.value;
    renderInterfaceInfo(state.network.interfaces.find((info) => info.name === interfaceSelect.value));
    requestSettings(interfaceSelect.value);
  });
  saveButton.addEventListener('click', () => {
    if (!settingsLoaded || !editable) return;
    if (!dhcpElement.checked) {
      const dns = dnsElement.value.split(',').map((value) => value.trim()).filter(Boolean);
      const gateway = gatewayElement.value.trim();
      if (!isValidIpv4Address(addressElement.value) || !isValidIpv4PrefixLength(prefixElement.value)) {
        setWarning('Enter a valid IPv4 address and prefix length (0-32).');
        return;
      }
      if ((gateway && !isValidIpv4Address(gateway)) || dns.some((server) => !isValidIpv4Address(server))) {
        setWarning('Enter valid IPv4 gateway and DNS server addresses.');
        return;
      }
    }
    setWarning('Saving changes to the persistent network configuration. Apply them separately when ready.');
    const settings = collectSettings();
    const result = sendAction('write_settings', settings);
    if (result.ok) {
      pendingWriteRequestId = result.payload.requestId;
      pendingWriteSettings = normalizeNetworkSettings(settings);
      pendingWriteInterface = interfaceSelect.value;
    }
  });
  applyButton.addEventListener('click', () => {
    if (!settingsLoaded || !editable) return;
    if (!window.confirm('Applying network configuration may disconnect this Web UI. Continue?')) return;
    sendAction('apply', { interface: interfaceSelect.value });
  });
  resetButton.addEventListener('click', () => {
    if (!settingsLoaded || !editable || resetButton.disabled) return;
    if (resetStaged) {
      const result = sendAction('cancel_reset_settings', { interface: interfaceSelect.value });
      if (result.ok) {
        pendingResetRequestId = result.payload.requestId;
        setWarning('Cancelling factory reset.');
      }
      return;
    }
    if (!window.confirm('Reset this interface to factory defaults? Any unsaved edits will be discarded. The override file will be removed when you press Apply.')) return;
    const result = sendAction('reset_settings', { interface: interfaceSelect.value });
    if (result.ok) {
      pendingResetRequestId = result.payload.requestId;
      setWarning('Factory reset staged. Press Apply to activate the factory configuration.');
    }
  });

  [dhcpElement, staticIpv4Element, addressElement, prefixElement, gatewayElement, dnsElement]
    .forEach((element) => element.addEventListener('input', handleFormChange));
  [dhcpElement, staticIpv4Element].forEach((element) => element.addEventListener('change', handleFormChange));

  return {
    onMessage(message) {
      if (message.type === 'network.read_interfaces.result') {
        addLog(`${kGroup}.read_interfaces.result received`);
        state.network.interfacesRequestPending = false;
        state.network.available = message.parameters?.available !== false;
        state.network.interfaces = Array.isArray(message.parameters?.interfaces)
          ? message.parameters.interfaces
          : [];
        populateInterfaces(state.network.interfaces);
      } else if (message.type === 'network.read_settings.result') {
        addLog(`${kGroup}.read_settings.result received`);
        if (!isCurrentSettingsResponse(message, pendingSettingsRequestId, interfaceSelect.value)) {
          addLog(`${kGroup}.read_settings.result ignored as stale`);
          return;
        }
        setSettings(message.parameters || {});
      } else if (message.type === 'network.write_settings.result') {
        addLog(`${kGroup}.write_settings.result received`);
        if (message.requestId !== pendingWriteRequestId ||
            pendingWriteInterface !== interfaceSelect.value) {
          addLog(`${kGroup}.write_settings.result ignored as stale`);
          return;
        }
        baselineSettings = pendingWriteSettings;
        if (pendingWriteSettings?.dhcp_ipv4) {
          addressElement.value = '';
          gatewayElement.value = '';
          dnsElement.value = '';
        }
        pendingWriteRequestId = null;
        pendingWriteSettings = null;
        pendingWriteInterface = '';
        userOverride = message.parameters?.user_override === true;
        resetStaged = false;
        updateFieldStates();
        updateDirtyState(false);
        setWarning('Network configuration saved. Apply it separately when ready.');
        fileElement.textContent = message.parameters?.network_file || fileElement.textContent;
      } else if (isSuccessfulApplyResponse(message)) {
        addLog(`${kGroup}.${message.type.split('.')[1]} received`);
        setWarning('Network configuration applied. The Web UI may disconnect if this interface carries its connection.');
        const resetWasStaged = resetStaged;
        resetStaged = false;
        if (resetWasStaged) {
          userOverride = false;
        }
        updateFieldStates();
        requestSettings(interfaceSelect.value);
      } else if (message.type === 'network.reset_settings.result' ||
                 message.type === 'network.cancel_reset_settings.result') {
        addLog(`${kGroup}.${message.type.split('.')[1]}.result received`);
        if (message.requestId !== pendingResetRequestId ||
            message.parameters?.interface !== interfaceSelect.value) {
          if (message.requestId === pendingResetRequestId) {
            pendingResetRequestId = null;
            updateActionButtons();
          }
          addLog(`${kGroup}.reset_settings.result ignored as stale`);
          return;
        }
        pendingResetRequestId = null;
        resetStaged = message.parameters?.reset_staged === true;
        userOverride = message.parameters?.user_override === true;
        updateFieldStates();
        requestSettings(interfaceSelect.value);
        setWarning(resetStaged
          ? 'Factory reset staged. Press Apply to activate the factory configuration.'
          : 'Factory reset cancelled.');
      } else if (message.type.endsWith('.error') && message.type.startsWith(`${kGroup}.`)) {
        const error = message.parameters?.error || 'network operation failed';
        if (message.type === 'network.read_interfaces.error') {
          state.network.interfacesRequestPending = false;
          statusElement.textContent = `Unable to load interfaces: ${error}`;
        }
        if (message.type === 'network.read_settings.error') {
          if (message.requestId !== pendingSettingsRequestId) {
            addLog(`${kGroup}.read_settings.error ignored as stale`);
            return;
          }
          pendingSettingsInterface = '';
          pendingSettingsRequestId = null;
          settingsLoaded = false;
          editable = false;
          settingsSection.hidden = true;
          updateFieldStates();
        }
        if (message.type === 'network.reset_settings.error' ||
            message.type === 'network.cancel_reset_settings.error') {
          if (message.requestId === pendingResetRequestId) {
            pendingResetRequestId = null;
            updateActionButtons();
          }
        }
        if (message.type === 'network.write_settings.error' &&
            message.requestId === pendingWriteRequestId &&
            pendingWriteInterface === interfaceSelect.value) {
          pendingWriteRequestId = null;
          pendingWriteSettings = null;
          pendingWriteInterface = '';
          updateActionButtons();
        }
        if (message.type === 'network.apply.error') {
          const resetWasStaged = resetStaged;
          if (typeof message.parameters?.reset_staged === 'boolean') {
            resetStaged = message.parameters.reset_staged;
          }
          if (resetWasStaged && !resetStaged) {
            userOverride = false;
          }
          updateFieldStates();
          requestSettings(interfaceSelect.value);
        }
        addLog(`${message.type}: ${error}`);
        setWarning(error);
      }
    },
    onConnectionChange(connected) {
      if (connected) {
        if (state.network.interfaces.length > 0) {
          populateInterfaces(state.network.interfaces);
        } else if (!state.network.interfacesRequestPending) {
          const request = buildRequest(kGroup, 'read_interfaces', {});
          state.network.interfacesRequestPending = true;
          const result = sendPayload(request);
          if (!result.ok) {
            state.network.interfacesRequestPending = false;
          }
          addLog(`${kGroup}.read_interfaces ${formatSendStatus(result)}`);
        }
      }
    },
    destroy() {}
  };
}
