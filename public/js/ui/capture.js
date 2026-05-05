// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kCaptureFields = {
  string: createCaptureTextInput,
  integer: createCaptureTextInput,
  float: createCaptureTextInput
};

export function renderCaptureBlock(blockConfig, options = {}) {
  const fieldMap = new Map();
  const element = document.createElement('div');
  const sectionElements = renderCaptureSections(blockConfig.sections, fieldMap);

  sectionElements.forEach((sectionElement) => {
    element.appendChild(sectionElement);
  });

  const controls = createCaptureControlSection(options);
  element.appendChild(controls.element);

  const requestResponseObject = createRequestResponseObject(blockConfig.sections);
  let connected = false;
  let recordingState = 'idle';
  let captureStartTs = null;
  let elapsedTimer = null;
  let lastCaptureUrl = null;
  let lastCaptureName = '';

  function stopElapsed() {
    if (elapsedTimer) {
      clearInterval(elapsedTimer);
      elapsedTimer = null;
    }
  }

  function startElapsed() {
    stopElapsed();
    elapsedTimer = setInterval(() => {
      if (!captureStartTs) {
        controls.elapsedEl.textContent = '00:00:00';
        return;
      }
      controls.elapsedEl.textContent = formatElapsedMs(Date.now() - captureStartTs);
    }, 500);
  }

  function updateStateView() {
    let dotClass = 'idle';
    let statusText = 'Idle';

    if (recordingState === 'running') {
      dotClass = 'running';
      statusText = 'Capturing...';
    } else if (recordingState === 'processing') {
      dotClass = 'processing';
      statusText = 'Processing...';
    }

    const statusDotElement = document.createElement('span');
    statusDotElement.className = `dot ${dotClass}`;
    controls.statusEl.replaceChildren(statusDotElement, document.createTextNode(statusText));
    controls.startBtn.disabled = !connected || recordingState !== 'idle' || !hasStartParameters(fieldMap);
    controls.stopBtn.disabled = !connected || recordingState !== 'running';
    controls.downloadBtn.disabled = !lastCaptureUrl ||
      recordingState === 'running' ||
      recordingState === 'processing';

    if (recordingState === 'running') {
      startElapsed();
      return;
    }

    stopElapsed();
    if (recordingState === 'idle') {
      controls.elapsedEl.textContent = '00:00:00';
    }
  }

  function clearDownload() {
    if (lastCaptureUrl) {
      URL.revokeObjectURL(lastCaptureUrl);
      lastCaptureUrl = null;
      lastCaptureName = '';
    }
  }

  controls.startBtn.addEventListener('click', () => {
    clearDownload();
  });

  controls.downloadBtn.addEventListener('click', () => {
    if (!lastCaptureUrl) {
      return;
    }

    const link = document.createElement('a');
    link.href = lastCaptureUrl;
    link.download = lastCaptureName || `pcap_${Date.now()}.pcap`;
    link.click();
  });

  fieldMap.forEach((fieldElement) => {
    fieldElement.addEventListener('input', updateStateView);
  });

  updateStateView();

  return {
    element,
    requestResponseObject,
    bindSubmit(handler) {
      controls.startBtn.addEventListener('click', handler);
    },
    bindStop(handler) {
      controls.stopBtn.addEventListener('click', handler);
    },
    getValues(sourceRequestResponseObject) {
      return getCaptureValues(sourceRequestResponseObject, fieldMap);
    },
    setValues(sourceRequestResponseObject) {
      setCaptureValues(sourceRequestResponseObject, fieldMap);
      updateStateView();
    },
    setConnectionState(nextConnected) {
      connected = nextConnected === true;
      if (!connected && recordingState === 'processing') {
        recordingState = 'idle';
      }
      updateStateView();
    },
    setRecordingState(nextRecordingState) {
      recordingState = nextRecordingState;
      if (recordingState === 'running') {
        captureStartTs = Date.now();
      }
      if (recordingState === 'idle') {
        captureStartTs = null;
      }
      updateStateView();
    },
    setCaptureResult(parameters = {}) {
      clearDownload();
      const dataB64 = parameters.dataB64 || '';
      if (dataB64) {
        lastCaptureUrl = toBlobUrl(dataB64);
        lastCaptureName = fileNameFromPath(parameters.file) || `pcap_${Date.now()}.pcap`;
      }
      recordingState = 'idle';
      captureStartTs = null;
      updateStateView();
    },
    reset() {
      recordingState = 'idle';
      captureStartTs = null;
      updateStateView();
    },
    destroy() {
      stopElapsed();
      clearDownload();
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
    });
  });

  return requestResponseObject;
}

function renderCaptureSections(sections, fieldMap) {
  return (sections || []).map((section) => {
    const sectionElement = document.createElement('section');
    sectionElement.className = 'section';

    renderCaptureSectionTitle(sectionElement, section.title);
    renderCaptureSectionRows(sectionElement, section, fieldMap);
    return sectionElement;
  });
}

function renderCaptureSectionTitle(sectionElement, sectionTitle) {
  const titleElement = document.createElement('h2');
  titleElement.textContent = sectionTitle;
  sectionElement.appendChild(titleElement);
}

function renderCaptureSectionRows(sectionElement, section, fieldMap) {
  const gridElement = document.createElement('div');
  gridElement.className = 'form-grid';

  (section.parameters || []).forEach((parameter) => {
    const labelElement = createCaptureRowLabel(parameter);

    const createField = kCaptureFields[parameter.value_type];
    if (!createField) {
      throw new Error(`Unsupported capture value_type: ${parameter.value_type}`);
    }
    const inputElement = createField(parameter, fieldMap);

    gridElement.appendChild(labelElement);
    gridElement.appendChild(inputElement);
  });

  sectionElement.appendChild(gridElement);
}

function createCaptureRowLabel(parameter) {
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

  labelHeaderElement.appendChild(labelTextElement);
  labelWrapperElement.appendChild(labelHeaderElement);
  return labelWrapperElement;
}

function createCaptureTextInput(parameter, fieldMap) {
  const inputElement = document.createElement('input');
  inputElement.className = 'input';
  inputElement.id = parameter.id;
  inputElement.value = parameter.default_value ?? '';
  fieldMap.set(parameter.id, inputElement);

  return inputElement;
}

function createCaptureControlSection(options) {
  const element = document.createElement('section');
  element.className = 'section';

  const titleElement = document.createElement('h2');
  titleElement.textContent = options.title || 'Capture Control';
  element.appendChild(titleElement);

  const statusRowElement = document.createElement('div');
  statusRowElement.className = 'status-row';

  const statusLabelElement = createCaptureControlLabel('Status:');
  const statusElement = document.createElement('span');
  statusElement.className = 'status-value';

  const elapsedLabelElement = createCaptureControlLabel('Elapsed Time:');
  const elapsedElement = document.createElement('span');
  elapsedElement.className = 'elapsed-time';
  elapsedElement.textContent = '00:00:00';

  statusRowElement.appendChild(statusLabelElement);
  statusRowElement.appendChild(statusElement);
  statusRowElement.appendChild(elapsedLabelElement);
  statusRowElement.appendChild(elapsedElement);
  element.appendChild(statusRowElement);

  const controlsElement = document.createElement('div');
  controlsElement.className = 'controls';

  const startButtonElement = createCaptureControlButton('btn-start', 'Start Recording');
  const stopButtonElement = createCaptureControlButton('btn-stop', 'Stop Recording');
  const downloadButtonElement = createCaptureControlButton('btn-download', 'Download PCAP');

  controlsElement.appendChild(startButtonElement);
  controlsElement.appendChild(stopButtonElement);
  controlsElement.appendChild(downloadButtonElement);
  element.appendChild(controlsElement);

  return {
    element,
    statusEl: statusElement,
    elapsedEl: elapsedElement,
    startBtn: startButtonElement,
    stopBtn: stopButtonElement,
    downloadBtn: downloadButtonElement
  };
}

function createCaptureControlLabel(text) {
  const labelElement = document.createElement('span');
  labelElement.className = 'label';
  labelElement.textContent = text;

  return labelElement;
}

function createCaptureControlButton(buttonClass, buttonLabel) {
  const buttonElement = document.createElement('button');
  buttonElement.type = 'button';
  buttonElement.className = `btn ${buttonClass}`;
  buttonElement.textContent = buttonLabel;

  return buttonElement;
}

function getCaptureValues(requestResponseObject, fieldMap) {
  const updatedRequestResponseObject = structuredClone(requestResponseObject);

  Object.entries(updatedRequestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    parameterEntry.value = coerceCaptureValue(fieldElement.value, parameterEntry.value_type);
  });

  return updatedRequestResponseObject;
}

function setCaptureValues(requestResponseObject, fieldMap) {
  Object.entries(requestResponseObject || {}).forEach(([parameterId, parameterEntry]) => {
    const fieldElement = fieldMap.get(parameterId);
    if (!fieldElement) {
      return;
    }

    fieldElement.value = parameterEntry?.value ?? '';
  });
}

function coerceCaptureValue(value, valueType) {
  if (valueType === 'integer') {
    const integerValue = Number.parseInt(String(value).trim(), 10);
    return Number.isNaN(integerValue) ? value : integerValue;
  }

  if (valueType === 'float') {
    const floatValue = Number.parseFloat(String(value).trim());
    return Number.isNaN(floatValue) ? value : floatValue;
  }

  return value;
}

function hasStartParameters(fieldMap) {
  return Array.from(fieldMap.values()).every((fieldElement) => {
    return fieldElement.value.trim() !== '';
  });
}

function formatElapsedMs(ms) {
  const total = Math.max(0, Math.floor(ms / 1000));
  const h = String(Math.floor(total / 3600)).padStart(2, '0');
  const m = String(Math.floor((total % 3600) / 60)).padStart(2, '0');
  const s = String(total % 60).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

function toBlobUrl(base64Data) {
  const bytes = atob(base64Data || '');
  const out = new Uint8Array(bytes.length);
  for (let index = 0; index < bytes.length; index += 1) {
    out[index] = bytes.charCodeAt(index);
  }
  const blob = new Blob([out], { type: 'application/octet-stream' });
  return URL.createObjectURL(blob);
}

function fileNameFromPath(filePath) {
  if (!filePath) {
    return '';
  }

  return String(filePath).split(/[\\/]/).filter(Boolean).pop() || '';
}
