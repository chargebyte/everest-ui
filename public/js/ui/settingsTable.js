// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderPasswordInput } from './passwordInput.js';

const kSettingsTableFields = {
  string: createSettingsTableTextInput,
  integer: createSettingsTableTextInput,
  boolean: createSettingsTableBooleanField,
  secret_string: createSettingsTablePasswordInput
};

export function renderSettingsTableBlock(blockConfig, options) {
  const fieldMap = new Map();
  const unavailableParameterIds = new Set();

  const element = document.createElement('section');
  element.className = 'section';
  const sectionElements = renderSettingsTableSections(blockConfig.sections, fieldMap);

  sectionElements.forEach((sectionElement) => {
    element.appendChild(sectionElement.element);
  });

  const applyButtonElement = createSettingsTableApplyButton(options);
  element.appendChild(applyButtonElement);

  const requestResponseObject = createRequestResponseObject(blockConfig.sections);

  return {
    element,
    requestResponseObject,
    bindSubmit(handler) {
      applyButtonElement.addEventListener('click', () => {
        if (!validateSettingsTableFields(fieldMap)) {
          return;
        }

        handler();
      });
    },
    applyAvailableModules(availableModules) {
      applySettingsTableAvailableModules(sectionElements, unavailableParameterIds, availableModules);
    },
    getValues(requestResponseObject) {
      return getSettingsTableValues(requestResponseObject, fieldMap, unavailableParameterIds);
    },
    setValues(requestResponseObject) {
      setSettingsTableValues(requestResponseObject, fieldMap);
    }
  };
}

function createRequestResponseObject(sections) {
  const requestResponseObject = {};

  (sections || []).forEach((section) => {
    (section.parameters || []).forEach((parameter) => {
      requestResponseObject[parameter.id] = {
        backend_path: parameter.backend_path,
        value_type: parameter.value_type,
        value: null
      };

      if (Object.hasOwn(parameter, 'overwrite_value')) {
        requestResponseObject[parameter.id].overwrite_value = parameter.overwrite_value;
      }
    });
  });

  return requestResponseObject;
}

function renderSettingsTableSections(sections, fieldMap) {
  return (sections || []).map((section) => {
    const sectionElement = document.createElement('section');
    sectionElement.className = 'section';

    renderSettingsTableSectionTitle(sectionElement, section.title);
    const noteElement = createSettingsTableMissingNote();
    sectionElement.appendChild(noteElement);
    const { gridElement, rows } = renderSettingsTableSectionRows(sectionElement, section, fieldMap);
    return {
      section,
      element: sectionElement,
      gridElement,
      noteElement,
      rows
    };
  });
}

function renderSettingsTableSectionTitle(sectionElement, sectionTitle) {
  const titleElement = document.createElement('h2');
  titleElement.textContent = sectionTitle;
  sectionElement.appendChild(titleElement);
}

function renderSettingsTableSectionRows(sectionElement, section, fieldMap) {
  const gridElement = document.createElement('div');
  gridElement.className = 'form-grid';
  const sectionRadioGroups = new Map();
  const rows = [];

  (section.parameters || []).forEach((parameter) => {
    const labelElement = createSettingsTableRowLabel(parameter);

    const createField = kSettingsTableFields[parameter.value_type];
    if (!createField) {
      throw new Error(`Unsupported settings_table value_type: ${parameter.value_type}`);
    }
    const inputElement = createField(parameter, fieldMap, {
      sectionId: section.id,
      radioGroups: sectionRadioGroups
    });

    gridElement.appendChild(labelElement);
    gridElement.appendChild(inputElement);
    rows.push({
      parameter,
      labelElement,
      inputElement
    });
  });

  sectionElement.appendChild(gridElement);
  return {
    gridElement,
    rows
  };
}

function createSettingsTableMissingNote() {
  const noteElement = document.createElement('p');
  noteElement.className = 'settings-missing-note';
  noteElement.hidden = true;
  return noteElement;
}

function applySettingsTableAvailableModules(sectionElements, unavailableParameterIds, availableModules) {
  unavailableParameterIds.clear();

  if (!Array.isArray(availableModules)) {
    sectionElements.forEach((sectionElement) => {
      sectionElement.gridElement.hidden = false;
      sectionElement.noteElement.hidden = true;
      sectionElement.element.classList.remove(
        'settings-section-unavailable',
        'settings-section-partial-unavailable'
      );
      sectionElement.rows.forEach((row) => {
        setSettingsTableRowAvailable(row, true);
      });
    });
    return;
  }

  const availableModuleNames = new Set(
    availableModules.filter((moduleName) => typeof moduleName === 'string')
  );

  sectionElements.forEach((sectionElement) => {
    const missingModuleNames = new Set();
    let visibleRowCount = 0;
    let unavailableRowCount = 0;

    sectionElement.rows.forEach((row) => {
      const moduleName = getSettingsTableBackendModule(row.parameter.backend_path);
      const available = moduleName === '' || availableModuleNames.has(moduleName);

      setSettingsTableRowAvailable(row, available);

      if (available) {
        visibleRowCount += 1;
        return;
      }

      unavailableRowCount += 1;
      unavailableParameterIds.add(row.parameter.id);
      missingModuleNames.add(moduleName);
    });

    const sectionUnavailable = unavailableRowCount > 0 && visibleRowCount === 0;
    sectionElement.gridElement.hidden = false;
    sectionElement.element.classList.toggle('settings-section-unavailable', sectionUnavailable);
    sectionElement.element.classList.toggle(
      'settings-section-partial-unavailable',
      unavailableRowCount > 0 && !sectionUnavailable
    );
    renderSettingsTableMissingNote(
      sectionElement.noteElement,
      sectionElement.section.title,
      Array.from(missingModuleNames),
      sectionUnavailable
    );
  });
}

function setSettingsTableRowAvailable(row, available) {
  row.labelElement.hidden = false;
  row.inputElement.hidden = false;
  row.labelElement.classList.toggle('settings-unavailable-row', !available);
  row.inputElement.classList.toggle('settings-unavailable-control', !available);
  setSettingsTableInteractiveElementsDisabled(row.labelElement, !available);
  setSettingsTableInteractiveElementsDisabled(row.inputElement, !available);
}

function setSettingsTableInteractiveElementsDisabled(element, disabled) {
  if ('disabled' in element) {
    element.disabled = disabled;
  }

  element.querySelectorAll('input, button, select, textarea').forEach((controlElement) => {
    controlElement.disabled = disabled;
  });
}

function getSettingsTableBackendModule(backendPath) {
  const pathParts = String(backendPath || '').split('.');
  return pathParts[0] || '';
}

function renderSettingsTableMissingNote(noteElement, sectionTitle, missingModuleNames, sectionHidden) {
  if (missingModuleNames.length === 0) {
    noteElement.hidden = true;
    noteElement.textContent = '';
    return;
  }

  const moduleList = missingModuleNames.join(', ');
  if (sectionHidden) {
    noteElement.textContent =
      `${sectionTitle} is not available in the current EVerest base configuration. ` +
      `Use Direct config to upload or create a base config that includes ${moduleList}.`;
  } else {
    noteElement.textContent =
      `Some options are not available because ${moduleList} is missing from ` +
      'the current EVerest base configuration. Use Direct config to update the base config.';
  }
  noteElement.hidden = false;
}

function createSettingsTableRowLabel(parameter) {
  const labelWrapperElement = document.createElement('div');
  labelWrapperElement.className = 'settings-label';

  const labelHeaderElement = document.createElement('div');
  labelHeaderElement.className = 'settings-label-header';

  const labelTextElement = document.createElement('span');
  labelTextElement.className = 'label';
  labelTextElement.textContent = parameter.display_name;

  if (parameter.description) {
    const infoButtonElement = document.createElement('button');
    infoButtonElement.type = 'button';
    infoButtonElement.className = 'settings-info-button';
    infoButtonElement.textContent = 'i';

    const descriptionElement = document.createElement('div');
    descriptionElement.className = 'settings-description';
    descriptionElement.hidden = true;
    descriptionElement.textContent = parameter.description;

    infoButtonElement.addEventListener('click', () => {
      descriptionElement.hidden = !descriptionElement.hidden;
    });

    labelHeaderElement.appendChild(infoButtonElement);
    labelHeaderElement.appendChild(labelTextElement);
    labelWrapperElement.appendChild(labelHeaderElement);
    labelWrapperElement.appendChild(descriptionElement);
    return labelWrapperElement;
  }

  labelWrapperElement.appendChild(labelHeaderElement);
  return labelWrapperElement;
}

function createSettingsTableTextInput(parameter, fieldMap) {
  void fieldMap;
  const inputElement = document.createElement('input');
  inputElement.className = 'input';
  inputElement.id = parameter.id;

  if (parameter.value_type === 'integer' && parameter.id === 'security_profile') {
    inputElement.type = 'number';
    inputElement.min = '1';
    inputElement.max = '3';
    inputElement.step = '1';
    inputElement.inputMode = 'numeric';
    inputElement.addEventListener('input', () => {
      updateSecurityProfileValidity(inputElement);
    });
  }

  fieldMap.set(parameter.id, inputElement);

  if (!parameter.unit) {
    return inputElement;
  }

  const inputWithUnitElement = document.createElement('div');
  inputWithUnitElement.className = 'input-with-unit';

  const unitElement = document.createElement('span');
  unitElement.className = 'unit';
  unitElement.textContent = parameter.unit;

  inputWithUnitElement.appendChild(inputElement);
  inputWithUnitElement.appendChild(unitElement);

  return inputWithUnitElement;
}

function createSettingsTableBooleanField(parameter, fieldMap, context = {}) {
  const inputElement = document.createElement('input');
  inputElement.id = parameter.id;

  if (Object.hasOwn(parameter, 'radio_group')) {
    inputElement.type = 'radio';
    inputElement.name = getRadioGroupName(
      parameter.radio_group,
      context.sectionId,
      context.radioGroups
    );
  } else {
    inputElement.type = 'checkbox';
  }

  fieldMap.set(parameter.id, inputElement);
  return inputElement;
}

function getRadioGroupName(radioGroupId, sectionId, radioGroups) {
  if (!radioGroups.has(radioGroupId)) {
    radioGroups.set(radioGroupId, `settings-table-${sectionId}-radio-group-${radioGroupId}`);
  }

  return radioGroups.get(radioGroupId);
}

function createSettingsTablePasswordInput(parameter, fieldMap) {
  const inputElement = renderPasswordInput({
    id: parameter.id
  });
  fieldMap.set(parameter.id, inputElement);
  return inputElement;
}

function createSettingsTableApplyButton(options) {
  const buttonElement = document.createElement('button');
  buttonElement.type = 'button';
  buttonElement.className = 'btn btn-start';
  buttonElement.textContent = options?.buttonLabel || 'Apply';

  return buttonElement;
}

function setSettingsTableValues(requestResponseObject, fieldMap) {
  Object.entries(requestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    const fieldValue = parameterEntry?.value;

    if (fieldElement.type === 'checkbox' || fieldElement.type === 'radio') {
      fieldElement.checked = Boolean(fieldValue);
      return;
    }

    fieldElement.value = fieldValue ?? '';
  });
}

function getSettingsTableValues(requestResponseObject, fieldMap, unavailableParameterIds = new Set()) {
  const updatedRequestResponseObject = structuredClone(requestResponseObject);

  Object.entries(updatedRequestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    if (unavailableParameterIds.has(parameterId)) {
      delete updatedRequestResponseObject[parameterId];
      return;
    }

    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    if (fieldElement.type === 'checkbox' || fieldElement.type === 'radio') {
      parameterEntry.value = fieldElement.checked;
      return;
    }

    parameterEntry.value = coerceSettingsTableValue(
      fieldElement.value,
      parameterEntry.value_type,
      parameterId
    );
  });

  return updatedRequestResponseObject;
}

function coerceSettingsTableValue(value, valueType, parameterId = '') {
  if (valueType === 'integer') {
    const trimmedValue = String(value).trim();
    if (trimmedValue === '') {
      return '';
    }

    const integerValue = Number.parseInt(trimmedValue, 10);
    if (Number.isNaN(integerValue)) {
      return value;
    }

    if (parameterId === 'security_profile') {
      if (!/^[+-]?\d+$/.test(trimmedValue)) {
        return value;
      }

      if (integerValue < 1 || integerValue > 3) {
        return value;
      }
    }

    return integerValue;
  }

  if (valueType === 'float') {
    const trimmedValue = String(value).trim();
    if (trimmedValue === '') {
      return '';
    }

    const floatValue = Number.parseFloat(trimmedValue);
    return Number.isNaN(floatValue) ? value : floatValue;
  }

  return value;
}

function validateSettingsTableFields(fieldMap) {
  for (const fieldElement of fieldMap.values()) {
    if (!(fieldElement instanceof HTMLInputElement)) {
      continue;
    }

    if (fieldElement.id === 'security_profile') {
      updateSecurityProfileValidity(fieldElement);
    }

    if (typeof fieldElement.checkValidity === 'function' && !fieldElement.checkValidity()) {
      if (typeof fieldElement.reportValidity === 'function') {
        fieldElement.reportValidity();
      }
      return false;
    }
  }

  return true;
}

function updateSecurityProfileValidity(fieldElement) {
  const trimmedValue = String(fieldElement.value ?? '').trim();

  if (trimmedValue === '') {
    fieldElement.setCustomValidity('');
    return;
  }

  if (!/^[+-]?\d+$/.test(trimmedValue)) {
    fieldElement.setCustomValidity('Enter an integer from 1 to 3.');
    return;
  }

  const integerValue = Number.parseInt(trimmedValue, 10);
  if (Number.isNaN(integerValue) || integerValue < 1 || integerValue > 3) {
    fieldElement.setCustomValidity('Enter an integer from 1 to 3.');
    return;
  }

  fieldElement.setCustomValidity('');
}
