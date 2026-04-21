// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kSettingsTableFields = {
  string: createSettingsTableTextInput,
  integer: createSettingsTableTextInput,
  boolean: createSettingsTableBooleanField,
  secret_string: createSettingsTablePasswordInput
};

export function renderSettingsTableBlock(blockConfig, options) {
  const fieldMap = new Map();

  const element = document.createElement('section');
  element.className = 'section';
  const sectionElements = renderSettingsTableSections(blockConfig.sections, fieldMap);

  sectionElements.forEach((sectionElement) => {
    element.appendChild(sectionElement);
  });

  const applyButtonElement = createSettingsTableApplyButton(options);
  element.appendChild(applyButtonElement);

  const requestResponseObject = createRequestResponseObject(blockConfig.sections);

  return {
    element,
    requestResponseObject,
    bindSubmit(handler) {
      applyButtonElement.addEventListener('click', handler);
    },
    getValues(requestResponseObject) {
      return getSettingsTableValues(requestResponseObject, fieldMap);
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

      if (Object.hasOwn(parameter, 'default_value')) {
        requestResponseObject[parameter.id].default_value = parameter.default_value;
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
    renderSettingsTableSectionRows(sectionElement, section, fieldMap);
    return sectionElement;
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
  });

  sectionElement.appendChild(gridElement);
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

function createSettingsTablePasswordInput(parameter) {
  void parameter;
  throw new Error('createSettingsTablePasswordInput is not implemented yet');
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

function getSettingsTableValues(requestResponseObject, fieldMap) {
  const updatedRequestResponseObject = structuredClone(requestResponseObject);

  Object.entries(updatedRequestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    if (fieldElement.type === 'checkbox' || fieldElement.type === 'radio') {
      parameterEntry.value = fieldElement.checked;
      return;
    }

    parameterEntry.value = coerceSettingsTableValue(fieldElement.value, parameterEntry.value_type);
  });

  return updatedRequestResponseObject;
}

function coerceSettingsTableValue(value, valueType) {
  if (valueType === 'integer') {
    const trimmedValue = String(value).trim();
    if (trimmedValue === '') {
      return '';
    }

    const integerValue = Number.parseInt(trimmedValue, 10);
    return Number.isNaN(integerValue) ? value : integerValue;
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
