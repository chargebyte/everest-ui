// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { validateCatalog } from './pageConfigValidation.js';

const kUiBlocks = {
  settings_table: normalizeSettingsTable,
  config_loader: normalizeConfigLoader,
  settings_matrix: normalizeSettingsMatrix
};

export function loadPageConfig(moduleId, parameterCatalog) {
  const moduleConfig = parameterCatalog?.modules?.[moduleId];

  validateCatalog(moduleConfig);

  return {
    moduleId: extractModuleId(moduleConfig),
    title: extractTitle(moduleConfig),
    actions: extractActions(moduleConfig),
    blocks: createBlocks(moduleConfig)
  };
}

function extractModuleId(moduleConfig) {
  const actionConfigs = Object.values(moduleConfig.actions);
  return actionConfigs[0].group;
}

function extractTitle(moduleConfig) {
  return moduleConfig.title;
}

function extractActions(moduleConfig) {
  return Object.fromEntries(
    Object.entries(moduleConfig.actions).map(([actionKey, actionConfig]) => {
      return [actionKey, { ...actionConfig }];
    })
  );
}

function createBlocks(moduleConfig) {
  const catalogUiElements = Object.keys(moduleConfig).filter((key) => {
    return key !== 'title' && key !== 'actions';
  });

  return catalogUiElements.map((uiElement) => {
    const normalizeUiBlock = kUiBlocks[uiElement];
    if (!normalizeUiBlock) {
      throw new Error(`Unsupported UI block: ${uiElement}`);
    }
    return normalizeUiBlock(moduleConfig[uiElement], uiElement);
  });
}

function normalizeSettingsTable(block) {
  return {
    kind: 'settings_table',
    sections: normalizeSettingsTableSections(block)
  };
}

function normalizeSettingsTableSections(block) {
  return Object.entries(block).map(([sectionId, sectionRows]) => {
    return normalizeSettingsTableSection(sectionId, sectionRows);
  });
}

function normalizeSettingsTableSection(sectionId, sectionRows) {
  return {
    id: sectionId,
    title: toTitleCase(sectionId),
    parameters: normalizeSettingsTableParameters(sectionRows)
  };
}

function normalizeSettingsTableParameters(sectionRows) {
  return sectionRows.map((row) => {
    return normalizeSettingsTableParameter(row);
  });
}

function normalizeSettingsTableParameter(row) {
  const parameter = {
    id: row.id,
    display_name: row.display_name,
    description: row.description,
    value_type: row.value_type,
    backend_path: row.backend_path
  };

  if (Object.hasOwn(row, 'unit')) {
    parameter.unit = row.unit;
  }

  if (Object.hasOwn(row, 'radio_group')) {
    parameter.radio_group = row.radio_group;
  }

  return parameter;
}

function normalizeSettingsMatrix(block) {
  return {
    kind: 'settings_matrix',
    sections: normalizeSettingsMatrixSections(block)
  };
}

function normalizeSettingsMatrixSections(block) {
  return block.map((section) => {
      return normalizeSettingsMatrixSection(section);
    });
}

function normalizeSettingsMatrixSection(section) {
  return {
    id: section.id,
    title: section.display_name,
    instances: normalizeSettingsMatrixInstances(section.instances),
    fields: normalizeSettingsMatrixFields(section.fields)
  };
}

function normalizeSettingsMatrixInstances(instances) {
  return instances.map((instance) => ({
    id: instance.id,
    display_name: instance.display_name
  }));
}

function normalizeSettingsMatrixFields(fields) {
  return fields.map((field) => {
    return normalizeSettingsMatrixField(field);
  });
}

function normalizeSettingsMatrixField(field) {
  const normalizedField = {
    id: field.id,
    display_name: field.display_name,
    value_type: field.value_type,
    backend_path: field.backend_path
  };

  if (Object.hasOwn(field, 'description')) {
    normalizedField.description = field.description;
  }

  if (Object.hasOwn(field, 'unit')) {
    normalizedField.unit = field.unit;
  }

  if (Object.hasOwn(field, 'enum_values')) {
    normalizedField.enum_values = [...field.enum_values];
  }

  return normalizedField;
}

function normalizeConfigLoader(block) {
  const loaderConfig = block.loader[0];

  return {
    kind: 'config_loader',
    id: loaderConfig.id,
    title: toTitleCase(loaderConfig.display_name)
  };
}

function toTitleCase(value) {
  return String(value)
    .split(' ')
    .filter(Boolean)
    .map((part) => {
      return part.charAt(0).toUpperCase() + part.slice(1);
    })
    .join(' ');
}
