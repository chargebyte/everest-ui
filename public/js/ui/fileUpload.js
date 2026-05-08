// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderFileUploadBlock(options = {}) {
  const scopeName = requireScopeName(options.namePrefix);
  const element = document.createElement('div');
  element.className = `${scopeName}-file-upload-row`;
  element.style.display = 'flex';
  element.style.alignItems = 'center';
  element.style.gap = '8px';

  const fileInputId = `${scopeName}-file-upload-input`;
  const fileNameId = `${scopeName}-file-upload-name`;
  const browseButtonId = `${scopeName}-file-upload-browse`;

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.hidden = true;
  fileInput.accept = options.accept || '';
  fileInput.id = fileInputId;

  const fileNameInput = document.createElement('input');
  fileNameInput.className = `${scopeName}-file-upload-name input`;
  fileNameInput.type = 'text';
  fileNameInput.readOnly = true;
  fileNameInput.id = fileNameId;
  fileNameInput.value = options.placeholder || 'No file selected';
  fileNameInput.style.flex = '1 1 auto';
  fileNameInput.style.minWidth = '0';

  const browseButton = document.createElement('button');
  browseButton.type = 'button';
  browseButton.className = options.buttonClass || 'btn btn-secondary';
  browseButton.id = browseButtonId;
  browseButton.textContent = options.buttonLabel || 'Browse...';
  browseButton.style.flex = '0 0 auto';

  const statusElement = document.createElement('div');
  statusElement.className = `${scopeName}-file-upload-status`;
  statusElement.style.display = 'none';

  const selectHandler = {
    current: () => {}
  };

  element.appendChild(fileInput);
  element.appendChild(fileNameInput);
  element.appendChild(browseButton);
  element.appendChild(statusElement);

  browseButton.addEventListener('click', () => {
    fileInput.click();
  });

  fileInput.addEventListener('change', async () => {
    const file = fileInput.files?.[0];
    if (!file) {
      resetSelection();
      selectHandler.current(null);
      return;
    }

    try {
      const dataB64 = await readFileAsBase64(file);
      const selectedFile = {
        file_name: file.name,
        dataB64,
        size_bytes: file.size,
        mime_type: file.type || 'application/octet-stream'
      };

      setSelection(selectedFile);
      selectHandler.current(selectedFile);
    } catch (error) {
      resetSelection();
      setStatusText('Failed to read file');
      selectHandler.current(null);
      if (typeof options.onError === 'function') {
        options.onError(error instanceof Error ? error.message : String(error));
      }
    } finally {
      fileInput.value = '';
    }
  });

  function setStatusText(text) {
    statusElement.textContent = text || '';
  }

  function setSelection(fileInfo) {
    fileNameInput.value = fileInfo?.file_name || (options.placeholder || 'No file selected');
    setStatusText('');
  }

  function resetSelection() {
    fileNameInput.value = options.placeholder || 'No file selected';
    setStatusText('');
  }

  return {
    element,
    bindSelect(handler) {
      selectHandler.current = typeof handler === 'function' ? handler : () => {};
    },
    clear() {
      fileInput.value = '';
      resetSelection();
      selectHandler.current(null);
    }
  };
}

function requireScopeName(namePrefix) {
  if (typeof namePrefix !== 'string' || namePrefix.trim() === '') {
    throw new Error('renderFileUploadBlock requires a non-empty namePrefix');
  }

  return namePrefix.trim();
}

function readFileAsBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();

    reader.onload = () => {
      try {
        const buffer = reader.result;
        if (!(buffer instanceof ArrayBuffer)) {
          reject(new Error('Unexpected file reader result'));
          return;
        }

        resolve(arrayBufferToBase64(buffer));
      } catch (error) {
        reject(error);
      }
    };

    reader.onerror = () => {
      reject(reader.error || new Error('Failed to read file'));
    };

    reader.readAsArrayBuffer(file);
  });
}

function arrayBufferToBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  const chunkSize = 0x8000;
  let binary = '';

  for (let index = 0; index < bytes.length; index += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(index, index + chunkSize));
  }

  return btoa(binary);
}
