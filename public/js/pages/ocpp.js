// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { bindSwitchRows, renderSettingsPage } from '../ui/settingsTable.js';
import { kNoopPageLifecycle } from '../ui/pageLifecycle.js';

function sectionFromBackendPath(path) {
  return String(path || '').split('.')[0] || 'general';
}

function toRow(param) {
  if (param.value_type === 'boolean') {
    return {
      type: 'checkbox',
      id: param.id,
      label: param.display_name,
      checked: false
    };
  }
  if (param.value_type === 'enum') {
    const options = Array.isArray(param.enum_values) ? param.enum_values : [];
    return {
      type: 'select',
      id: param.id,
      label: `${param.display_name}:`,
      options,
      value: options[0] || ''
    };
  }
  if (param.value_type === 'secret_string') {
    return {
      type: 'password',
      id: param.id,
      label: `${param.display_name}:`,
      value: ''
    };
  }
  return {
    type: 'text',
    id: param.id,
    label: `${param.display_name}:`,
    value: ''
  };
}

export function renderOcppPage(container, { addLog, parameterCatalog }) {
  const moduleConfig = parameterCatalog?.modules?.ocpp;
  const parameters = moduleConfig?.parameters || [];
  const generalRows = parameters
    .filter((param) => sectionFromBackendPath(param.backend?.path) === 'general')
    .map(toRow);
  const securityRows = parameters
    .filter((param) => sectionFromBackendPath(param.backend?.path) === 'security')
    .map(toRow);

  const refs = renderSettingsPage(container, {
    title: 'OCPP Configuration',
    sections: [
      {
        title: 'General',
        rows: generalRows
      },
      {
        title: 'Security',
        rows: securityRows
      }
    ],
    controlsClass: 'settings-action-controls',
    buttonId: 'save-ocpp',
    buttonLabel: 'Save Configuration'
  });

  const enabledCheckbox = container.querySelector('#enabled');

  function applyOcppEnabledState() {
    if (!enabledCheckbox) {
      return;
    }
    const disabled = !enabledCheckbox.checked;
    const controls = container.querySelectorAll('input, select, button, textarea');
    controls.forEach((control) => {
      if (control === enabledCheckbox) {
        return;
      }
      control.disabled = disabled;
    });
  }

  bindSwitchRows(container);
  applyOcppEnabledState();
  enabledCheckbox?.addEventListener('change', applyOcppEnabledState);

  refs.actionButton.addEventListener('click', () => {
    if (enabledCheckbox && !enabledCheckbox.checked) {
      addLog('OCPP save ignored: OCPP is disabled');
      return;
    }
    addLog('OCPP configuration saved (placeholder)');
  });

  return kNoopPageLifecycle;
}
