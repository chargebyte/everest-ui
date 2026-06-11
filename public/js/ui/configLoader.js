// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderConfigLoaderBlock(blockConfig, options = {}) {
  const element = document.createElement('section');
  element.className = 'section';

  const titleElement = document.createElement('h2');
  titleElement.textContent = blockConfig.title || 'Direct Config';
  element.appendChild(titleElement);

  const gridElement = document.createElement('div');
  gridElement.className = 'form-grid everest-direct-grid';

  const downloadLabelElement = createLabel('Current config.yaml');
  const downloadButtonElement = createButton(
    options.downloadButtonLabel || 'Download config.yaml',
    'btn btn-download everest-direct-download-btn'
  );

  const uploadLabelElement = createLabel('Upload config.yaml');
  const uploadRowElement = document.createElement('div');
  uploadRowElement.className = 'inline-inputs everest-direct-upload-row';

  const fileInputElement = document.createElement('input');
  fileInputElement.type = 'file';
  fileInputElement.accept = '.yaml,.yml,text/yaml,text/plain';
  fileInputElement.className = 'input everest-direct-file-input';

  const uploadButtonElement = createButton(
    options.uploadButtonLabel || 'Upload and Apply',
    'btn btn-start'
  );
  uploadButtonElement.disabled = true;

  uploadRowElement.appendChild(fileInputElement);
  uploadRowElement.appendChild(uploadButtonElement);

  gridElement.appendChild(downloadLabelElement);
  gridElement.appendChild(downloadButtonElement);
  gridElement.appendChild(uploadLabelElement);
  gridElement.appendChild(uploadRowElement);
  element.appendChild(gridElement);

  const state = {
    fileName: '',
    configYaml: ''
  };

  fileInputElement.addEventListener('change', () => {
    state.fileName = '';
    state.configYaml = '';
    uploadButtonElement.disabled = true;

    const file = fileInputElement.files?.[0];
    if (!file) {
      return;
    }

    readFileAsText(file)
      .then((text) => {
        state.fileName = file.name;
        state.configYaml = text;
        uploadButtonElement.disabled = text.trim() === '';
      })
      .catch((error) => {
        fileInputElement.value = '';
        options.onFileError?.(error instanceof Error ? error.message : String(error));
      });
  });

  return {
    element,
    bindDownload(handler) {
      downloadButtonElement.addEventListener('click', handler);
    },
    bindUpload(handler) {
      uploadButtonElement.addEventListener('click', () => {
        handler({
          fileName: state.fileName,
          configYaml: state.configYaml
        });
      });
    },
    getUploadRequestResponseObject(actionConfig) {
      const payloadField = actionConfig?.payload_field || 'config_yaml';
      return {
        direct_config_file_name: {
          backend_path: 'file_name',
          value_type: 'string',
          value: state.fileName
        },
        [blockConfig.id || 'direct_config']: {
          backend_path: payloadField,
          value_type: 'string',
          value: state.configYaml
        }
      };
    },
    downloadConfigFile(parameters = {}) {
      downloadTextFile(
        parameters.file || 'config.yaml',
        parameters.config_yaml || ''
      );
    },
    clearSelection() {
      state.fileName = '';
      state.configYaml = '';
      fileInputElement.value = '';
      uploadButtonElement.disabled = true;
    }
  };
}

function createLabel(text) {
  const labelElement = document.createElement('div');
  labelElement.className = 'label';
  labelElement.textContent = text;
  return labelElement;
}

function createButton(label, className) {
  const buttonElement = document.createElement('button');
  buttonElement.type = 'button';
  buttonElement.className = className;
  buttonElement.textContent = label;
  return buttonElement;
}

function readFileAsText(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
      if (typeof reader.result !== 'string') {
        reject(new Error('Unexpected file reader result'));
        return;
      }

      resolve(reader.result);
    };
    reader.onerror = () => {
      reject(new Error(reader.error?.message || 'Failed to read file'));
    };
    reader.readAsText(file);
  });
}

function downloadTextFile(fileName, text) {
  if (!text) {
    return;
  }

  const blob = new Blob([text], { type: 'text/yaml' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = fileName;
  link.click();

  setTimeout(() => {
    URL.revokeObjectURL(url);
  }, 0);
}
