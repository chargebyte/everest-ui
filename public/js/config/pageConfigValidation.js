// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kUiElementValidators = {
  settings_table: validateSettingsTableParameters,
  config_loader: validateConfigLoaderParameters,
  settings_matrix: validateSettingsMatrixParameters
};

export function validateCatalog(moduleConfig) {
  if (!moduleConfig || typeof moduleConfig !== 'object') {
    throw new Error('Module configuration is missing or invalid');
  }

  validateTitle(moduleConfig);

  validateActionParameters(moduleConfig.actions);

  const catalogUiElements = Object.keys(moduleConfig).filter((key) => {
    return key !== 'title' && key !== 'actions';
  });
  if (catalogUiElements.length === 0) {
    throw new Error('Module configuration is missing UI element definitions');
  }

  catalogUiElements.forEach((uiElement) => {
    const validateUiElement = kUiElementValidators[uiElement];
    if (!validateUiElement) {
      throw new Error(`Unsupported block kind: ${uiElement}`);
    }
    validateUiElement(moduleConfig[uiElement]);
  });
}

function validateTitle(moduleConfig) {
  if (!hasNonEmptyStringField(moduleConfig, 'title')) {
    throw new Error('Module configuration is missing title');
  }
}

function validateActionParameters(actions) {
  validateActionsObject(actions);
  validateActionEntries(actions);
  validateActionHasNonEmptyGroup(actions);
  validateActionHasNonEmptyAction(actions);
  validateActionKeyMatchesActionValue(actions);
  validateActionParameterModes(actions);
  validateActionPayloadFields(actions);
}

function validateActionsObject(actions) {
  if (!isPlainObject(actions)) {
    throw new Error('Actions configuration is missing or invalid');
  }
}

function validateActionEntries(actions) {
  Object.entries(actions).forEach(([actionKey, actionConfig]) => {
    if (!isPlainObject(actionConfig)) {
      throw new Error(`Action '${actionKey}' is invalid`);
    }
  });
}

function validateActionHasNonEmptyGroup(actions) {
  Object.entries(actions).forEach(([actionKey, actionConfig]) => {
    if (!hasNonEmptyStringField(actionConfig, 'group')) {
      throw new Error(`Action '${actionKey}' is missing group`);
    }
  });
}

function validateActionHasNonEmptyAction(actions) {
  Object.entries(actions).forEach(([actionKey, actionConfig]) => {
    if (!hasNonEmptyStringField(actionConfig, 'action')) {
      throw new Error(`Action '${actionKey}' is missing action`);
    }
  });
}

function validateActionKeyMatchesActionValue(actions) {
  Object.entries(actions).forEach(([actionKey, actionConfig]) => {
    if (actionConfig.action !== actionKey) {
      throw new Error(`Action '${actionKey}' does not match action value '${actionConfig.action}'`);
    }
  });
}

function validateActionParameterModes(actions) {
  const allowedModes = new Set(['empty', 'from_catalog', 'raw_field', 'raw_object']);

  Object.entries(actions).forEach(([actionKey, actionConfig]) => {
    if (!hasNonEmptyStringField(actionConfig, 'parameters_mode')) {
      throw new Error(`Action '${actionKey}' is missing parameters_mode`);
    }

    if (!allowedModes.has(actionConfig.parameters_mode)) {
      throw new Error(`Action '${actionKey}' has invalid parameters_mode`);
    }
  });
}

function validateActionPayloadFields(actions) {
  Object.entries(actions).forEach(([actionKey, actionConfig]) => {
    const hasPayloadField = Object.hasOwn(actionConfig, 'payload_field');
    const payloadField = actionConfig.payload_field;

    if (actionConfig.parameters_mode === 'raw_field') {
      if (!hasNonEmptyStringField(actionConfig, 'payload_field')) {
        throw new Error(`Action '${actionKey}' requires payload_field`);
      }
      return;
    }

    if (hasPayloadField) {
      throw new Error(`Action '${actionKey}' must not define payload_field`);
    }
  });
}

function validateSettingsTableParameters(block) {
  validateUiElementObject(block, 'settings_table');
  validateUiElementSections(block, 'settings_table');
  validateUiElementSectionEntries(block, 'settings_table');
  validateSettingsTableRows(block);
}

function validateConfigLoaderParameters(block) {
  validateUiElementObject(block, 'config_loader');
  validateUiElementSections(block, 'config_loader');
  validateUiElementSectionEntries(block, 'config_loader');
  validateConfigLoaderRow(block);
}

function validateSettingsMatrixParameters(block) {
  validateUiElementArray(block, 'settings_matrix');
  validateSettingsMatrixSectionEntries(block);
  validateSettingsMatrixSections(block);
}

function validateUiElementObject(block, uiElementName) {
  if (!isPlainObject(block)) {
    throw new Error(`${uiElementName} configuration is missing or invalid`);
  }
}

function validateUiElementArray(block, uiElementName) {
  if (!Array.isArray(block)) {
    throw new Error(`${uiElementName} configuration is missing or invalid`);
  }
}

function validateUiElementSections(block, uiElementName) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    if (!Array.isArray(sectionEntries)) {
      throw new Error(`${uiElementName} section '${sectionKey}' must be an array`);
    }
  });
}

function validateUiElementSectionEntries(block, uiElementName) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    sectionEntries.forEach((entry, index) => {
      if (!isPlainObject(entry)) {
        throw new Error(
          `${uiElementName} section '${sectionKey}' has invalid entry at index ${index}`
        );
      }
    });
  });
}

function validateSettingsMatrixSectionEntries(block) {
  block.forEach((section, index) => {
    if (!isPlainObject(section)) {
      throw new Error(`settings_matrix has invalid section at index ${index}`);
    }
  });
}

function validateSettingsTableRows(block) {
  const requiredKeys = ['id', 'display_name', 'description', 'value_type', 'backend_path'];
  const optionalKeys = ['unit', 'radio_group', 'default_value'];
  const allowedKeys = new Set([...requiredKeys, ...optionalKeys]);
  const optionalKeyValidators = {
    unit(row) {
      return hasNonEmptyStringField(row, 'unit');
    },
    radio_group(row) {
      return Number.isInteger(row.radio_group);
    },
    default_value(row) {
      if (!Object.hasOwn(row, 'default_value')) {
        return true;
      }

      if (row.value_type === 'boolean') {
        return typeof row.default_value === 'boolean';
      }

      if (row.value_type === 'integer') {
        return Number.isInteger(row.default_value);
      }

      if (row.value_type === 'float') {
        return typeof row.default_value === 'number';
      }

      if (row.value_type === 'string' ||
          row.value_type === 'secret_string' ||
          row.value_type === 'enum') {
        return typeof row.default_value === 'string';
      }

      return false;
    }
  };

  validateRequiredKeys(block, requiredKeys, 'settings_table');
  validateAllowedKeys(block, allowedKeys, 'settings_table');
  validateSettingsTableRowValues(block);
  validateOptionalKeys(block, optionalKeys, optionalKeyValidators, 'settings_table');
}

function validateConfigLoaderRow(block) {
  const requiredKeys = ['id', 'display_name'];

  validateRequiredKeys(block, requiredKeys, 'config_loader');
  validateKeyStringValues(block, 'config_loader');
}

function validateSettingsMatrixSections(block) {
  const requiredSectionKeys = ['id', 'display_name', 'instances', 'fields'];
  const allowedFieldTypes = new Set(['string', 'integer', 'float', 'boolean', 'enum']);

  block.forEach((section, sectionIndex) => {
    requiredSectionKeys.forEach((requiredKey) => {
      if (!Object.hasOwn(section, requiredKey)) {
        throw new Error(
          `settings_matrix section at index ${sectionIndex} is missing '${requiredKey}'`
        );
      }
    });

    if (!hasNonEmptyStringField(section, 'id')) {
      throw new Error(`settings_matrix section at index ${sectionIndex} has invalid 'id'`);
    }

    if (!hasNonEmptyStringField(section, 'display_name')) {
      throw new Error(
        `settings_matrix section '${section.id}' has invalid 'display_name'`
      );
    }

    if (!Array.isArray(section.instances)) {
      throw new Error(`settings_matrix section '${section.id}' has invalid 'instances'`);
    }

    if (!Array.isArray(section.fields)) {
      throw new Error(`settings_matrix section '${section.id}' has invalid 'fields'`);
    }

    section.instances.forEach((instance, instanceIndex) => {
      if (!isPlainObject(instance)) {
        throw new Error(
          `settings_matrix section '${section.id}' has invalid instance at index ${instanceIndex}`
        );
      }

      ['id', 'display_name'].forEach((requiredKey) => {
        if (!Object.hasOwn(instance, requiredKey)) {
          throw new Error(
            `settings_matrix section '${section.id}' instance at index ${instanceIndex} ` +
            `is missing '${requiredKey}'`
          );
        }
      });

      if (!hasNonEmptyStringField(instance, 'id')) {
        throw new Error(
          `settings_matrix section '${section.id}' instance at index ${instanceIndex} has invalid 'id'`
        );
      }

      if (!hasNonEmptyStringField(instance, 'display_name')) {
        throw new Error(
          `settings_matrix section '${section.id}' instance '${instance.id}' has invalid 'display_name'`
        );
      }
    });

    section.fields.forEach((field, fieldIndex) => {
      if (!isPlainObject(field)) {
        throw new Error(
          `settings_matrix section '${section.id}' has invalid field at index ${fieldIndex}`
        );
      }

      const requiredFieldKeys = ['id', 'display_name', 'description', 'value_type', 'backend_path'];
      const optionalFieldKeys = ['unit', 'enum_values'];
      const allowedFieldKeys = new Set([...requiredFieldKeys, ...optionalFieldKeys]);

      requiredFieldKeys.forEach((requiredKey) => {
        if (!Object.hasOwn(field, requiredKey)) {
          throw new Error(
            `settings_matrix section '${section.id}' field at index ${fieldIndex} ` +
            `is missing '${requiredKey}'`
          );
        }
      });

      Object.keys(field).forEach((fieldKey) => {
        if (!allowedFieldKeys.has(fieldKey)) {
          throw new Error(
            `settings_matrix section '${section.id}' field at index ${fieldIndex} ` +
            `has unsupported '${fieldKey}'`
          );
        }
      });

      ['id', 'display_name', 'description', 'value_type', 'backend_path'].forEach((fieldKey) => {
        if (!hasNonEmptyStringField(field, fieldKey)) {
          throw new Error(
            `settings_matrix section '${section.id}' field at index ${fieldIndex} ` +
            `has invalid '${fieldKey}'`
          );
        }
      });

      if (!allowedFieldTypes.has(field.value_type)) {
        throw new Error(
          `settings_matrix section '${section.id}' field '${field.id}' has invalid 'value_type'`
        );
      }

      if (Object.hasOwn(field, 'unit') && !hasNonEmptyStringField(field, 'unit')) {
        throw new Error(
          `settings_matrix section '${section.id}' field '${field.id}' has invalid 'unit'`
        );
      }

      if (Object.hasOwn(field, 'enum_values')) {
        if (field.value_type !== 'enum') {
          throw new Error(
            `settings_matrix section '${section.id}' field '${field.id}' must not define 'enum_values'`
          );
        }

        if (!Array.isArray(field.enum_values) || field.enum_values.length === 0) {
          throw new Error(
            `settings_matrix section '${section.id}' field '${field.id}' has invalid 'enum_values'`
          );
        }

        field.enum_values.forEach((enumValue, enumIndex) => {
          if (typeof enumValue !== 'string' || enumValue.trim() === '') {
            throw new Error(
              `settings_matrix section '${section.id}' field '${field.id}' ` +
              `has invalid enum_values entry at index ${enumIndex}`
            );
          }
        });
      } else if (field.value_type === 'enum') {
        throw new Error(
          `settings_matrix section '${section.id}' field '${field.id}' requires 'enum_values'`
        );
      }
    });
  });
}

function validateRequiredKeys(block, requiredKeysList, uiElementName) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    sectionEntries.forEach((row, index) => {
      requiredKeysList.forEach((requiredKey) => {
        if (!Object.hasOwn(row, requiredKey)) {
          throw new Error(
            `${uiElementName} section '${sectionKey}' row at index ${index} ` +
            `is missing '${requiredKey}'`
          );
        }
      });
    });
  });
}

function validateAllowedKeys(block, allowedKeys, uiElementName) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    sectionEntries.forEach((row, index) => {
      Object.keys(row).forEach((rowKey) => {
        if (!allowedKeys.has(rowKey)) {
          throw new Error(
            `${uiElementName} section '${sectionKey}' row at index ${index} ` +
            `has unsupported '${rowKey}'`
          );
        }
      });
    });
  });
}

function validateOptionalKeys(block, optionalKeys, optionalKeyValidators, uiElementName) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    sectionEntries.forEach((row, index) => {
      optionalKeys.forEach((optionalKey) => {
        if (!Object.hasOwn(row, optionalKey)) {
          return;
        }

        const validateOptionalKey = optionalKeyValidators[optionalKey];
        if (!validateOptionalKey(row)) {
          throw new Error(
            `${uiElementName} section '${sectionKey}' row at index ${index} ` +
            `has invalid '${optionalKey}'`
          );
        }
      });
    });
  });
}

function validateSettingsTableRowValues(block) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    sectionEntries.forEach((row, index) => {
      const requiredStringKeys = ['id', 'display_name', 'description', 'value_type', 'backend_path'];
      requiredStringKeys.forEach((rowKey) => {
        if (!hasNonEmptyStringField(row, rowKey)) {
          throw new Error(
            `settings_table section '${sectionKey}' row at index ${index} ` +
            `has invalid '${rowKey}'`
          );
        }
      });
    });
  });
}

function validateKeyStringValues(block, uiElementName) {
  Object.entries(block).forEach(([sectionKey, sectionEntries]) => {
    sectionEntries.forEach((row, index) => {
      Object.keys(row).forEach((rowKey) => {
        if (!hasNonEmptyStringField(row, rowKey)) {
          throw new Error(
            `${uiElementName} section '${sectionKey}' row at index ${index} ` +
            `has invalid '${rowKey}'`
          );
        }
      });
    });
  });
}

function isPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function hasNonEmptyStringField(target, fieldName) {
  return typeof target?.[fieldName] === 'string' && target[fieldName].trim() !== '';
}
