// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderJournalExtractBlock(blockConfig) {
  let requestedOutputMode = blockConfig.outputOptions[0]?.id || 'download';
  const element = document.createElement('section');
  element.className = 'section journal-extract';

  const titleElement = document.createElement('h2');
  titleElement.textContent = blockConfig.title;
  element.appendChild(titleElement);

  const controlsElement = document.createElement('div');
  controlsElement.className = 'journal-extract-controls';

  const bootGroup = createRadioGroup(
    'Boot range',
    'journal-extract-boot',
    blockConfig.bootOptions
  );
  const serviceFilter = createCheckbox(
    blockConfig.serviceFilter.label,
    'journal-extract-service'
  );
  const outputGroup = createRadioGroup(
    'Output',
    'journal-extract-output',
    blockConfig.outputOptions
  );

  controlsElement.appendChild(bootGroup.element);
  controlsElement.appendChild(serviceFilter.element);
  controlsElement.appendChild(outputGroup.element);
  element.appendChild(controlsElement);

  const generateButton = document.createElement('button');
  generateButton.type = 'button';
  generateButton.className = 'btn btn-start journal-extract-generate';
  generateButton.textContent = 'Generate';
  element.appendChild(generateButton);

  const resultElement = document.createElement('pre');
  resultElement.className = 'journal-extract-result';
  resultElement.hidden = true;
  element.appendChild(resultElement);

  function getRequestParameters() {
    return {
      boot: bootGroup.value(),
      service: serviceFilter.checked(),
      output: outputGroup.value()
    };
  }

  return {
    element,
    bindGenerate(handler) {
      generateButton.addEventListener('click', () => {
        resultElement.hidden = true;
        resultElement.textContent = '';
        const parameters = getRequestParameters();
        requestedOutputMode = parameters.output;
        handler(parameters);
      });
    },
    setResult(parameters) {
      const text = decodeBase64Text(parameters.dataB64 || '');
      if (requestedOutputMode === 'text') {
        resultElement.textContent = text;
        resultElement.hidden = false;
        return;
      }

      downloadTextFile(parameters.file || 'journal-extract.txt', text);
    }
  };
}

function createRadioGroup(label, name, options) {
  const fieldsetElement = document.createElement('fieldset');
  fieldsetElement.className = 'journal-extract-option-group';

  const legendElement = document.createElement('legend');
  legendElement.textContent = label;
  fieldsetElement.appendChild(legendElement);

  const inputElements = options.map((option, index) => {
    const labelElement = document.createElement('label');
    labelElement.className = 'journal-extract-option';

    const inputElement = document.createElement('input');
    inputElement.type = 'radio';
    inputElement.name = name;
    inputElement.value = option.id;
    inputElement.checked = index === 0;

    labelElement.appendChild(inputElement);
    labelElement.appendChild(document.createTextNode(option.label));
    fieldsetElement.appendChild(labelElement);
    return inputElement;
  });

  return {
    element: fieldsetElement,
    value() {
      return inputElements.find((inputElement) => inputElement.checked)?.value || '';
    }
  };
}

function createCheckbox(label, id) {
  const fieldsetElement = document.createElement('fieldset');
  fieldsetElement.className = 'journal-extract-option-group journal-extract-checkbox-group';

  const legendElement = document.createElement('legend');
  legendElement.textContent = 'Service filter';
  fieldsetElement.appendChild(legendElement);

  const labelElement = document.createElement('label');
  labelElement.className = 'journal-extract-checkbox';

  const inputElement = document.createElement('input');
  inputElement.type = 'checkbox';
  inputElement.id = id;

  labelElement.appendChild(inputElement);
  labelElement.appendChild(document.createTextNode(label));
  fieldsetElement.appendChild(labelElement);

  return {
    element: fieldsetElement,
    checked() {
      return inputElement.checked;
    }
  };
}

function decodeBase64Text(base64Data) {
  const binary = atob(base64Data);
  const bytes = new Uint8Array(binary.length);

  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }

  return new TextDecoder('utf-8').decode(bytes);
}

function downloadTextFile(fileName, text) {
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');

  link.href = url;
  link.download = fileName;
  link.click();
  setTimeout(() => {
    URL.revokeObjectURL(url);
  }, 0);
}
