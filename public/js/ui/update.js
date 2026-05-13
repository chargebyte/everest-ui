// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderFileUploadBlock } from './fileUpload.js';

export function renderUpdateBlock(blockConfig, options = {}) {
  const scopeName = requireScopeName(options.namePrefix);
  const updaterDefinition = readUpdaterDefinition(blockConfig);
  const requestResponseObject = createRequestResponseObject(updaterDefinition);

  const element = document.createElement('section');
  element.className = `${scopeName}-update section`;

  const titleElement = document.createElement('h2');
  titleElement.textContent = updaterDefinition.title || options.title || 'Update';
  element.appendChild(titleElement);

  const detailsGrid = document.createElement('div');
  detailsGrid.className = `${scopeName}-update-grid form-grid`;

  const versionLabel = createLabel(options.versionLabel || 'Current Version:');
  const versionValue = document.createElement('span');
  versionValue.className = `${scopeName}-update-version value-text`;

  const progressLabel = createLabel(options.progressLabel || 'Progress:');
  const progressValue = document.createElement('span');
  progressValue.className = `${scopeName}-update-progress value-text`;

  const rebootLabel = createLabel(options.rebootLabel || 'Reboot:');
  const rebootValue = document.createElement('span');
  rebootValue.className = `${scopeName}-update-reboot value-text`;

  detailsGrid.appendChild(versionLabel);
  detailsGrid.appendChild(versionValue);
  detailsGrid.appendChild(progressLabel);
  detailsGrid.appendChild(progressValue);
  detailsGrid.appendChild(rebootLabel);
  detailsGrid.appendChild(rebootValue);
  element.appendChild(detailsGrid);

  const uploaderHost = document.createElement('div');
  uploaderHost.className = `${scopeName}-update-uploader`;
  element.appendChild(uploaderHost);

  const controlsElement = document.createElement('div');
  controlsElement.className = 'controls';

  const updateButton = document.createElement('button');
  updateButton.type = 'button';
  updateButton.className = `${scopeName}-update-button btn btn-start`;
  updateButton.textContent = options.buttonLabel || 'Start Update';
  controlsElement.appendChild(updateButton);

  const rebootButton = document.createElement('button');
  rebootButton.type = 'button';
  rebootButton.className = `${scopeName}-reboot-button btn btn-secondary`;
  rebootButton.textContent = options.rebootButtonLabel || 'Reboot';
  rebootButton.disabled = true;
  controlsElement.appendChild(rebootButton);

  element.appendChild(controlsElement);

  const fileUpload = renderFileUploadBlock({
    namePrefix: `${scopeName}-file-upload`,
    chunkSizeBytes: updaterDefinition.chunkSizeBytes,
    buttonLabel: options.browseButtonLabel || 'Browse...',
    placeholder: options.filePlaceholder || 'No file selected',
    onError(errorMessage) {
      if (typeof options.onFileError === 'function') {
        options.onFileError(errorMessage);
      }
    }
  });
  uploaderHost.replaceChildren(fileUpload.element);

  const state = {
    progressText: options.progressPlaceholder || 'Idle',
    rebootRequired: false,
    updating: false
  };

  let updateHandler = () => {};
  let rebootHandler = () => {};

  fileUpload.bindSelect((fileInfo) => {
    const imageValue = getImageValue(requestResponseObject, updaterDefinition.id);

    imageValue.file_name = fileInfo?.file_name || '';
    imageValue.size_bytes = normalizeInteger(fileInfo?.size_bytes, 0);
    imageValue.chunk_size_bytes = normalizeInteger(
      fileInfo?.chunk_size_bytes,
      updaterDefinition.chunkSizeBytes
    );
    imageValue.chunk_count = normalizeInteger(fileInfo?.chunk_count, 0);
    imageValue.chunk_index = normalizeInteger(fileInfo?.chunk_index, 0);
    imageValue.dataB64 = normalizeText(fileInfo?.dataB64, '');

    if (!state.rebootRequired) {
      state.progressText = options.progressPlaceholder || 'Idle';
    }

    syncView();
  });

  updateButton.addEventListener('click', () => {
    updateHandler();
  });

  rebootButton.addEventListener('click', () => {
    rebootHandler();
  });

  syncView();

  return {
    element,
    requestResponseObject,
    bindUpdate(handler) {
      updateHandler = typeof handler === 'function' ? handler : () => {};
    },
    bindReboot(handler) {
      rebootHandler = typeof handler === 'function' ? handler : () => {};
    },
    hasSelectedFile() {
      return hasSelectedFile(requestResponseObject, updaterDefinition.id);
    },
    getSelectedFileInfo() {
      if (!fileUpload.hasSelection()) {
        return null;
      }

      return buildSelectedFileInfo(
        getImageValue(requestResponseObject, updaterDefinition.id)
      );
    },
    async readSelectedFileChunk(chunkIndex) {
      const chunk = await fileUpload.readChunk(chunkIndex);
      const imageValue = getImageValue(requestResponseObject, updaterDefinition.id);

      imageValue.chunk_index = normalizeInteger(chunk.chunk_index, 0);
      imageValue.dataB64 = normalizeText(chunk.dataB64, '');

      return {
        chunk_index: imageValue.chunk_index,
        dataB64: imageValue.dataB64
      };
    },
    getValues(sourceRequestResponseObject) {
      return getUpdateValues(sourceRequestResponseObject, state);
    },
    setValues(sourceRequestResponseObject) {
      setUpdateValues(sourceRequestResponseObject, requestResponseObject, fileUpload, state);
      syncView();
    },
    setVersion(text) {
      getImageValue(requestResponseObject, updaterDefinition.id).version =
        normalizeText(text, '');
      syncView();
    },
    setSelectedFile(fileInfo) {
      const imageValue = getImageValue(requestResponseObject, updaterDefinition.id);
      imageValue.file_name = fileInfo?.file_name || '';
      imageValue.size_bytes = normalizeInteger(fileInfo?.size_bytes, 0);
      imageValue.chunk_size_bytes = normalizeInteger(
        fileInfo?.chunk_size_bytes,
        updaterDefinition.chunkSizeBytes
      );
      imageValue.chunk_count = normalizeInteger(fileInfo?.chunk_count, 0);
      imageValue.chunk_index = normalizeInteger(fileInfo?.chunk_index, 0);
      imageValue.dataB64 = fileInfo?.dataB64 || '';

      if (!fileInfo) {
        fileUpload.clear();
      }

      syncView();
    },
    setProgress(progressState) {
      state.updating = Boolean(progressState);

      if (!progressState) {
        state.progressText = options.progressPlaceholder || 'Idle';
        syncView();
        return;
      }

      if (typeof progressState === 'string') {
        state.progressText = progressState;
        syncView();
        return;
      }

      const progress = Object.hasOwn(progressState, 'progress') ? progressState.progress : null;
      const stage = normalizeText(progressState.stage, '');
      const message = normalizeText(progressState.message, '');
      const fragments = [];

      if (stage) {
        fragments.push(stage);
      }

      if (progress !== null && progress !== undefined && progress !== '') {
        fragments.push(`${progress}%`);
      }

      if (message) {
        fragments.push(message);
      }

      state.progressText = fragments.length > 0
        ? fragments.join(' - ')
        : (options.progressActiveText || 'Updating...');

      syncView();
    },
    setRebootRequired(flag) {
      state.rebootRequired = flag === true;
      state.updating = false;

      if (state.rebootRequired) {
        state.progressText = options.rebootRequiredText || 'Reboot required';
      } else if (!hasSelectedFile(requestResponseObject, updaterDefinition.id)) {
        state.progressText = options.progressPlaceholder || 'Idle';
      }

      syncView();
    },
    destroy() {}
  };

  function syncView() {
    const imageValue = getImageValue(requestResponseObject, updaterDefinition.id);

    versionValue.textContent =
      normalizeText(imageValue.version, options.versionPlaceholder || 'Not loaded');
    progressValue.textContent = state.progressText || options.progressPlaceholder || 'Idle';
    rebootValue.textContent = state.rebootRequired
      ? (options.rebootRequiredText || 'Reboot required')
      : (options.rebootPlaceholder || 'Not required');

    updateButton.disabled =
      state.updating ||
      state.rebootRequired ||
      !hasSelectedFile(requestResponseObject, updaterDefinition.id);

    rebootButton.disabled = !state.rebootRequired;
  }
}

function readUpdaterDefinition(blockConfig) {
  const sections = blockConfig?.sections || [];

  if (sections.length !== 1) {
    throw new Error('update.js expects exactly one updater definition');
  }

  const definition = sections[0];
  return {
    id: definition.id,
    title: definition.title || definition.display_name || '',
    displayName: definition.display_name || definition.title || 'Firmware Image',
    backendPath: definition.backend_path || 'image',
    valueType: definition.value_type || 'image',
    chunkSizeBytes: normalizeInteger(definition.chunk_size_bytes, 0)
  };
}

function createRequestResponseObject(updaterDefinition) {
  return {
    [updaterDefinition.id]: {
      backend_path: updaterDefinition.backendPath,
      value_type: updaterDefinition.valueType,
      value: {
        version: '',
        file_name: '',
        size_bytes: 0,
        chunk_size_bytes: updaterDefinition.chunkSizeBytes,
        chunk_count: 0,
        chunk_index: 0,
        dataB64: ''
      }
    }
  };
}

function getUpdateValues(sourceRequestResponseObject, state) {
  void state;
  return structuredClone(sourceRequestResponseObject);
}

function setUpdateValues(
  sourceRequestResponseObject,
  requestResponseObject,
  fileUpload,
  state
) {
  void state;
  const sourceEntries = Object.entries(sourceRequestResponseObject || {});
  let hasExplicitFileSelection = false;

  sourceEntries.forEach(([parameterId, parameterEntry]) => {
    if (!Object.hasOwn(requestResponseObject, parameterId)) {
      return;
    }

    const imageValue = requestResponseObject[parameterId].value;
    const nextValue = parameterEntry?.value || {};

    if (Object.hasOwn(nextValue, 'version')) {
      imageValue.version = normalizeText(nextValue.version, '');
    }
    if (Object.hasOwn(nextValue, 'file_name')) {
      imageValue.file_name = normalizeText(nextValue.file_name, '');
      hasExplicitFileSelection = true;
    }
    if (Object.hasOwn(nextValue, 'size_bytes')) {
      imageValue.size_bytes = normalizeInteger(nextValue.size_bytes, 0);
    }
    if (Object.hasOwn(nextValue, 'chunk_size_bytes')) {
      imageValue.chunk_size_bytes = normalizeInteger(nextValue.chunk_size_bytes, 0);
    }
    if (Object.hasOwn(nextValue, 'chunk_count')) {
      imageValue.chunk_count = normalizeInteger(nextValue.chunk_count, 0);
    }
    if (Object.hasOwn(nextValue, 'chunk_index')) {
      imageValue.chunk_index = normalizeInteger(nextValue.chunk_index, 0);
    }
    if (Object.hasOwn(nextValue, 'dataB64')) {
      imageValue.dataB64 = normalizeText(nextValue.dataB64, '');
    }
  });

  const activeEntry = Object.values(requestResponseObject)[0];
  if (hasExplicitFileSelection && !activeEntry?.value?.file_name) {
    fileUpload.clear();
  }
}

function getImageValue(requestResponseObject, parameterId) {
  const entry = requestResponseObject?.[parameterId];
  if (!entry) {
    throw new Error(`Missing updater request-response entry '${parameterId}'`);
  }

  if (!entry.value || typeof entry.value !== 'object') {
    entry.value = {
      version: '',
      file_name: '',
      size_bytes: 0,
      chunk_size_bytes: 0,
      chunk_count: 0,
      chunk_index: 0,
      dataB64: ''
    };
  }

  return entry.value;
}

function hasSelectedFile(requestResponseObject, parameterId) {
  const imageValue = getImageValue(requestResponseObject, parameterId);
  return imageValue.file_name.trim() !== '' &&
    imageValue.size_bytes > 0 &&
    imageValue.chunk_size_bytes > 0 &&
    imageValue.chunk_count > 0;
}

function buildSelectedFileInfo(imageValue) {
  return {
    file_name: imageValue.file_name,
    size_bytes: imageValue.size_bytes,
    chunk_size_bytes: imageValue.chunk_size_bytes,
    chunk_count: imageValue.chunk_count,
    chunk_index: imageValue.chunk_index,
    dataB64: imageValue.dataB64
  };
}

function createLabel(text) {
  const labelElement = document.createElement('span');
  labelElement.className = 'label';
  labelElement.textContent = text;
  return labelElement;
}

function requireScopeName(namePrefix) {
  if (typeof namePrefix !== 'string' || namePrefix.trim() === '') {
    throw new Error('renderUpdateBlock requires a non-empty namePrefix');
  }

  return namePrefix.trim();
}

function normalizeText(value, fallback) {
  if (typeof value === 'string' && value.trim() !== '') {
    return value;
  }

  if (value === '') {
    return '';
  }

  if (Number.isFinite(value)) {
    return String(value);
  }

  return fallback;
}

function normalizeInteger(value, fallback) {
  if (Number.isInteger(value)) {
    return value;
  }

  if (typeof value === 'string' && value.trim() !== '') {
    const parsed = Number.parseInt(value, 10);
    if (Number.isInteger(parsed)) {
      return parsed;
    }
  }

  return fallback;
}
