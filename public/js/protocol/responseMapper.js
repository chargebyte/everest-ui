// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kUiElementMappers = {
  settings_table: mapSettingsTableResponse,
  settings_matrix: mapSettingsMatrixResponse
};

export function mapResponse(uiElementType, requestResponseObject, message) {
  const mapUiElementResponse = kUiElementMappers[uiElementType];
  if (!mapUiElementResponse) {
    throw new Error(`Unsupported UI element type: ${uiElementType}`);
  }

  return mapUiElementResponse(requestResponseObject, message);
}

function mapSettingsTableResponse(requestResponseObject, message) {
  const mappedObject = structuredClone(requestResponseObject);
  const backendParameters = message.parameters;

  Object.values(mappedObject).forEach((entry) => {
    const backendValue = readBackendValue(backendParameters, entry.backend_path);
    entry.value = Object.hasOwn(entry, 'default_value')
      ? entry.default_value
      : backendValue;
  });

  return mappedObject;
}

function mapSettingsMatrixResponse(requestResponseObject, message) {
  const mappedObject = structuredClone(requestResponseObject);
  const backendParameters = message.parameters;

  Object.values(mappedObject).forEach((entry) => {
    entry.value = readBackendValue(backendParameters, entry.backend_path);
  });

  return mappedObject;
}

function readBackendValue(backendParameters, backendPath) {
  const pathSegments = splitBackendPath(backendPath);
  let currentNode = backendParameters;

  pathSegments.forEach((segment) => {
    if (currentNode && typeof currentNode === 'object') {
      currentNode = currentNode[segment];
      return;
    }
    currentNode = undefined;
  });

  return currentNode;
}

function splitBackendPath(backendPath) {
  return String(backendPath)
      .split('.')
      .filter((segment) => segment !== '');
}
