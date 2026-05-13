// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderFileUploadBlock(options = {}) {
  const scopeName = requireScopeName(options.namePrefix);
  const chunkSizeBytes = resolveChunkSizeBytes(options.chunkSizeBytes);
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
  let selectedFileState = null;

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
      const selectedFile = createSelectedFileState(file, chunkSizeBytes);
      selectedFileState = selectedFile;

      setSelection(selectedFile);
      selectHandler.current(buildSelectedFileInfo(selectedFile));
    } catch (error) {
      selectedFileState = null;
      resetSelection();
      setStatusText('Failed to prepare file');
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
    statusElement.style.display = text ? 'block' : 'none';
  }

  function setSelection(fileState) {
    fileNameInput.value = fileState.file_name;
    setStatusText(`${formatFileSize(fileState.size_bytes)} in ${fileState.chunk_count} chunks`);
  }

  function resetSelection() {
    selectedFileState = null;
    fileNameInput.value = options.placeholder || 'No file selected';
    setStatusText('');
  }

  return {
    element,
    bindSelect(handler) {
      selectHandler.current = typeof handler === 'function' ? handler : () => {};
    },
    hasSelection() {
      return selectedFileState !== null;
    },
    getSelectedFileInfo() {
      return selectedFileState ? buildSelectedFileInfo(selectedFileState) : null;
    },
    async readChunk(chunkIndex) {
      if (!selectedFileState) {
        throw new Error('No file selected');
      }

      if (!Number.isInteger(chunkIndex) || chunkIndex < 0) {
        throw new Error('Invalid chunk index');
      }

      if (chunkIndex >= selectedFileState.chunk_count) {
        throw new Error('Chunk index out of range');
      }

      const startOffset = chunkIndex * selectedFileState.chunk_size_bytes;
      const endOffset = Math.min(
        startOffset + selectedFileState.chunk_size_bytes,
        selectedFileState.size_bytes
      );
      const chunkBlob = selectedFileState.file.slice(startOffset, endOffset);
      const chunkBuffer = await readBlobAsArrayBuffer(chunkBlob);

      return {
        chunk_index: chunkIndex,
        dataB64: arrayBufferToBase64(chunkBuffer)
      };
    },
    clear() {
      fileInput.value = '';
      resetSelection();
      selectHandler.current(null);
    }
  };
}

function createSelectedFileState(file, chunkSizeBytes) {
  const chunkCount = Math.max(1, Math.ceil(file.size / chunkSizeBytes));

  return {
    file,
    file_name: file.name,
    size_bytes: file.size,
    mime_type: file.type || 'application/octet-stream',
    chunk_size_bytes: chunkSizeBytes,
    chunk_count: chunkCount,
    chunk_index: 0,
    dataB64: ''
  };
}

function buildSelectedFileInfo(fileState) {
  return {
    file_name: fileState.file_name,
    size_bytes: fileState.size_bytes,
    mime_type: fileState.mime_type,
    chunk_size_bytes: fileState.chunk_size_bytes,
    chunk_count: fileState.chunk_count,
    chunk_index: fileState.chunk_index,
    dataB64: fileState.dataB64
  };
}

function requireScopeName(namePrefix) {
  if (typeof namePrefix !== 'string' || namePrefix.trim() === '') {
    throw new Error('renderFileUploadBlock requires a non-empty namePrefix');
  }

  return namePrefix.trim();
}

function resolveChunkSizeBytes(value) {
  if (Number.isInteger(value) && value > 0) {
    return value;
  }

  throw new Error('renderFileUploadBlock requires a positive integer chunkSizeBytes');
}

function readBlobAsArrayBuffer(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();

    reader.onload = () => {
      const result = reader.result;
      if (!(result instanceof ArrayBuffer)) {
        reject(new Error('Unexpected file reader result'));
        return;
      }

      resolve(result);
    };

    reader.onerror = () => {
      reject(reader.error || new Error('Failed to read file chunk'));
    };

    reader.readAsArrayBuffer(blob);
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

function formatFileSize(sizeBytes) {
  if (!Number.isFinite(sizeBytes) || sizeBytes < 0) {
    return 'Unknown size';
  }

  if (sizeBytes < 1024) {
    return `${sizeBytes} B`;
  }

  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = sizeBytes / 1024;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  return `${value.toFixed(value >= 10 ? 0 : 1)} ${units[unitIndex]}`;
}
