// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kSettingsMatrixFields = {
  string: createSettingsMatrixTextInput,
  integer: createSettingsMatrixTextInput,
  float: createSettingsMatrixTextInput,
  boolean: createSettingsMatrixBooleanField,
  enum: createSettingsMatrixEnumField
};

export function renderSettingsMatrixBlock(blockConfig, options) {
  const fieldMap = new Map();

  const element = document.createElement('section');
  element.className = 'section';
  const sectionElements = renderSettingsMatrixSections(blockConfig.sections, fieldMap);

  sectionElements.forEach((sectionElement) => {
    element.appendChild(sectionElement);
  });

  const applyButtonElement = createSettingsMatrixApplyButton(options);
  element.appendChild(applyButtonElement);

  const requestResponseObject = createRequestResponseObject(blockConfig.sections);

  return {
    element,
    requestResponseObject,
    bindSubmit(handler) {
      applyButtonElement.addEventListener('click', handler);
    },
    getValues(requestResponseObject) {
      return getSettingsMatrixValues(requestResponseObject, fieldMap);
    },
    setValues(requestResponseObject) {
      setSettingsMatrixValues(requestResponseObject, fieldMap);
    }
  };
}

function createRequestResponseObject(sections) {
  const requestResponseObject = {};

  (sections || []).forEach((section) => {
    (section.instances || []).forEach((instance) => {
      (section.fields || []).forEach((field) => {
        const parameterId = buildSettingsMatrixParameterId(section.id, instance.id, field.id);
        requestResponseObject[parameterId] = {
          backend_path: resolveSettingsMatrixBackendPath(field, instance.id),
          value_type: field.value_type,
          value: null
        };

        if (Object.hasOwn(field, 'default_value')) {
          requestResponseObject[parameterId].default_value = field.default_value;
        }
      });
    });
  });

  return requestResponseObject;
}

function renderSettingsMatrixSections(sections, fieldMap) {
  return (sections || []).map((section) => {
    const sectionElement = document.createElement('section');
    sectionElement.className = 'section';

    renderSettingsMatrixSectionTitle(sectionElement, section.title);
    renderSettingsMatrixSection(sectionElement, section, fieldMap);
    return sectionElement;
  });
}

function renderSettingsMatrixSectionTitle(sectionElement, sectionTitle) {
  const titleElement = document.createElement('h2');
  titleElement.textContent = sectionTitle;
  sectionElement.appendChild(titleElement);
}

function renderSettingsMatrixSection(sectionElement, section, fieldMap) {
  const matrixElement = document.createElement('div');
  matrixElement.className = 'matrix-grid';
  matrixElement.style.setProperty('--matrix-columns', String(section.instances?.length || 0));

  renderSettingsMatrixSectionColumnHeaders(matrixElement, section);
  renderSettingsMatrixFieldRows(matrixElement, section, fieldMap);

  sectionElement.appendChild(matrixElement);
}

function renderSettingsMatrixSectionColumnHeaders(matrixElement, section) {
  const cornerHeaderElement = document.createElement('div');
  cornerHeaderElement.className = 'matrix-column-header';
  matrixElement.appendChild(cornerHeaderElement);

  (section.instances || []).forEach((instance) => {
    const columnHeaderElement = document.createElement('div');
    columnHeaderElement.className = 'matrix-column-header';
    columnHeaderElement.textContent = instance.display_name;
    matrixElement.appendChild(columnHeaderElement);
  });
}

function renderSettingsMatrixFieldRows(matrixElement, section, fieldMap) {
  (section.fields || []).forEach((field) => {
    renderSettingsMatrixFieldRow(matrixElement, section, field, fieldMap);
  });
}

function renderSettingsMatrixFieldRow(matrixElement, section, field, fieldMap) {
  const labelElement = createSettingsMatrixRowLabel(field);
  matrixElement.appendChild(labelElement);

  (section.instances || []).forEach((instance) => {
    const inputElement = createSettingsMatrixInstanceField(section, instance, field, fieldMap);
    matrixElement.appendChild(inputElement);
  });
}

function createSettingsMatrixRowLabel(field) {
  const labelWrapperElement = document.createElement('div');
  labelWrapperElement.className = 'settings-label';

  const labelHeaderElement = document.createElement('div');
  labelHeaderElement.className = 'settings-label-header';

  const labelTextElement = document.createElement('span');
  labelTextElement.className = 'label';
  labelTextElement.textContent = field.display_name;

  if (field.description) {
    const infoButtonElement = document.createElement('button');
    infoButtonElement.type = 'button';
    infoButtonElement.className = 'settings-info-button';
    infoButtonElement.textContent = 'i';

    const descriptionElement = document.createElement('div');
    descriptionElement.className = 'settings-description';
    descriptionElement.hidden = true;
    descriptionElement.textContent = field.description;

    infoButtonElement.addEventListener('click', () => {
      descriptionElement.hidden = !descriptionElement.hidden;
    });

    labelHeaderElement.appendChild(infoButtonElement);
    labelHeaderElement.appendChild(labelTextElement);
    labelWrapperElement.appendChild(labelHeaderElement);
    labelWrapperElement.appendChild(descriptionElement);
    return labelWrapperElement;
  }

  labelHeaderElement.appendChild(labelTextElement);
  labelWrapperElement.appendChild(labelHeaderElement);
  return labelWrapperElement;
}

function createSettingsMatrixInstanceField(section, instance, field, fieldMap) {
  const createField = kSettingsMatrixFields[field.value_type];
  if (!createField) {
    throw new Error(`Unsupported settings_matrix value_type: ${field.value_type}`);
  }

  const fieldElement = createField(field, fieldMap, {
    sectionId: section.id,
    instanceId: instance.id
  });

  const cellElement = document.createElement('div');
  cellElement.className = 'matrix-cell';
  if (field.value_type === 'boolean') {
    cellElement.classList.add('matrix-cell-boolean');
  }
  cellElement.appendChild(fieldElement);

  return cellElement;
}

function createSettingsMatrixTextInput(field, fieldMap, context = {}) {
  const inputElement = document.createElement('input');
  inputElement.className = 'input';
  inputElement.id = buildSettingsMatrixParameterId(context.sectionId, context.instanceId, field.id);
  inputElement.dataset.backendPath = resolveSettingsMatrixBackendPath(field, context.instanceId);
  fieldMap.set(inputElement.id, inputElement);

  if (!field.unit) {
    return inputElement;
  }

  const inputWithUnitElement = document.createElement('div');
  inputWithUnitElement.className = 'input-with-unit';

  const unitElement = document.createElement('span');
  unitElement.className = 'unit';
  unitElement.textContent = field.unit;

  inputWithUnitElement.appendChild(inputElement);
  inputWithUnitElement.appendChild(unitElement);

  return inputWithUnitElement;
}

function createSettingsMatrixBooleanField(field, fieldMap, context = {}) {
  const inputElement = document.createElement('input');
  inputElement.type = 'checkbox';
  inputElement.id = buildSettingsMatrixParameterId(context.sectionId, context.instanceId, field.id);
  inputElement.dataset.backendPath = resolveSettingsMatrixBackendPath(field, context.instanceId);
  fieldMap.set(inputElement.id, inputElement);

  return inputElement;
}

function createSettingsMatrixEnumField(field, fieldMap, context = {}) {
  const selectElement = document.createElement('select');
  selectElement.className = 'input';
  selectElement.id = buildSettingsMatrixParameterId(context.sectionId, context.instanceId, field.id);
  selectElement.dataset.backendPath = resolveSettingsMatrixBackendPath(field, context.instanceId);

  (field.enum_values || []).forEach((enumValue) => {
    const optionElement = document.createElement('option');
    optionElement.value = enumValue;
    optionElement.textContent = enumValue;
    selectElement.appendChild(optionElement);
  });

  fieldMap.set(selectElement.id, selectElement);
  return selectElement;
}

function buildSettingsMatrixParameterId(sectionId, instanceId, fieldId) {
  return `${sectionId}-${instanceId}-${fieldId}`;
}

function resolveSettingsMatrixBackendPath(field, instanceId) {
  return field.backend_path.replace('{instance}', instanceId);
}

function createSettingsMatrixApplyButton(options) {
  const buttonElement = document.createElement('button');
  buttonElement.type = 'button';
  buttonElement.className = 'btn btn-start';
  buttonElement.textContent = options?.buttonLabel || 'Apply';

  return buttonElement;
}

function setSettingsMatrixValues(requestResponseObject, fieldMap) {
  Object.entries(requestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    const fieldValue = parameterEntry?.value;

    if (fieldElement.type === 'checkbox') {
      fieldElement.checked = Boolean(fieldValue);
      return;
    }

    fieldElement.value = fieldValue ?? '';
  });
}

function getSettingsMatrixValues(requestResponseObject, fieldMap) {
  const updatedRequestResponseObject = structuredClone(requestResponseObject);

  Object.entries(updatedRequestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    if (fieldElement.type === 'checkbox') {
      parameterEntry.value = fieldElement.checked;
      return;
    }

    parameterEntry.value = coerceSettingsMatrixValue(
      fieldElement.value,
      parameterEntry.value_type,
      parameterEntry.default_value
    );
  });

  return updatedRequestResponseObject;
}

function coerceSettingsMatrixValue(value, valueType, defaultValue) {
  const trimmedValue = String(value).trim();
  const effectiveValue =
    trimmedValue === '' && defaultValue !== undefined ? defaultValue : value;

  if (valueType === 'integer') {
    const normalizedValue = String(effectiveValue).trim();
    if (normalizedValue === '') {
      return '';
    }

    const integerValue = Number.parseInt(normalizedValue, 10);
    return Number.isNaN(integerValue) ? effectiveValue : integerValue;
  }

  if (valueType === 'float') {
    const normalizedValue = String(effectiveValue).trim();
    if (normalizedValue === '') {
      return '';
    }

    const floatValue = Number.parseFloat(normalizedValue);
    return Number.isNaN(floatValue) ? effectiveValue : floatValue;
  }

  return effectiveValue;
}
