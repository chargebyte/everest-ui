// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { nextRequestId } from '../state.js';

export function buildRequest(group, action, requestResponseObject) {
  const hasParameters = Object.keys(requestResponseObject || {}).length > 0;

  return {
    requestId: nextRequestId(),
    group,
    action,
    parameters: hasParameters ? buildParameters(requestResponseObject) : {}
  };
}

function buildParameters(requestResponseObject) {
  const parameters = {};

  Object.values(requestResponseObject || {}).forEach((entry) => {
    insertParameterValue(parameters, entry.backend_path, entry.value);
  });

  return parameters;
}

function insertParameterValue(parameters, backendPath, value) {
  const pathSegments = splitBackendPath(backendPath);
  setNestedValue(parameters, pathSegments, value);
}

function splitBackendPath(backendPath) {
  return String(backendPath).split('.').filter(Boolean);
}

function setNestedValue(target, pathSegments, value) {
  let currentNode = target;

  for (let index = 0; index < pathSegments.length - 1; index += 1) {
    const segment = pathSegments[index];
    if (!currentNode[segment] || typeof currentNode[segment] !== 'object') {
      currentNode[segment] = {};
    }
    currentNode = currentNode[segment];
  }

  const lastSegment = pathSegments[pathSegments.length - 1];
  currentNode[lastSegment] = value;
}
