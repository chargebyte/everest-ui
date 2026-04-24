// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kFileColumns = [
  { key: 'name', label: 'File Name' },
  { key: 'size', label: 'Size' },
  { key: 'lastModified', label: 'Last Modified' }
];

export function renderFilesDownloadBlock(blockConfig, options = {}) {
  const sections = blockConfig.sections || [];
  const element = document.createElement('div');
  const tableContexts = sections.map((section) => {
    const context = createFilesDownloadSection(section);
    element.appendChild(context.element);
    return context;
  });

  const downloadButton = createDownloadButton(options.buttonLabel);
  const controlsElement = document.createElement('div');
  controlsElement.className = 'controls settings-action-controls';
  controlsElement.appendChild(downloadButton);
  element.appendChild(controlsElement);

  function selectedFiles() {
    return tableContexts.flatMap((context) => context.getSelectedFiles());
  }

  function downloadRequestResponseObject() {
    return buildDownloadRequestResponseObject(selectedFiles());
  }

  function syncDownloadEnabled() {
    downloadButton.disabled = selectedFiles().length === 0;
  }

  tableContexts.forEach((context) => {
    context.onSelectionChange(syncDownloadEnabled);
  });
  syncDownloadEnabled();

  return {
    element,
    bindDownload(handler) {
      downloadButton.addEventListener('click', () => {
        handler(selectedFiles());
      });
    },
    getSelectedFiles: selectedFiles,
    getDownloadRequestResponseObject: downloadRequestResponseObject,
    downloadFile: downloadBase64File,
    setFiles(filesBySection) {
      tableContexts.forEach((context) => {
        context.setFiles(resolveSectionFiles(filesBySection, context.sectionId));
      });
      syncDownloadEnabled();
    }
  };
}

function createFilesDownloadSection(section) {
  const sectionElement = document.createElement('section');
  sectionElement.className = 'section';

  const titleElement = document.createElement('h2');
  titleElement.textContent = section.title;
  sectionElement.appendChild(titleElement);

  const tableWrapElement = document.createElement('div');
  tableWrapElement.className = 'logs-table-wrap';

  const tableElement = document.createElement('table');
  tableElement.className = 'logs-table';

  const selectAllInput = document.createElement('input');
  selectAllInput.type = 'checkbox';
  selectAllInput.id = `${section.id}-select-all`;

  const tbodyElement = document.createElement('tbody');
  tableElement.appendChild(createTableHead(selectAllInput));
  tableElement.appendChild(tbodyElement);
  tableWrapElement.appendChild(tableElement);
  sectionElement.appendChild(tableWrapElement);

  let files = [];
  let selectionChangeHandler = () => {};

  function renderRows() {
    tbodyElement.replaceChildren();

    files.forEach((file, index) => {
      const rowElement = document.createElement('tr');
      const checkboxCell = document.createElement('td');
      const checkboxElement = document.createElement('input');
      checkboxElement.type = 'checkbox';
      checkboxElement.className = 'log-row-check';
      checkboxElement.dataset.fileIndex = String(index);
      checkboxElement.addEventListener('change', syncSelectAllState);

      checkboxCell.appendChild(checkboxElement);
      rowElement.appendChild(checkboxCell);

      kFileColumns.forEach((column) => {
        const cellElement = document.createElement('td');
        cellElement.textContent = file[column.key] || '';
        rowElement.appendChild(cellElement);
      });

      tbodyElement.appendChild(rowElement);
    });

    syncSelectAllState();
  }

  function rowCheckboxes() {
    return Array.from(tbodyElement.querySelectorAll('.log-row-check'));
  }

  function syncSelectAllState() {
    const checkboxes = rowCheckboxes();
    const checkedCount = checkboxes.filter((checkbox) => checkbox.checked).length;
    selectAllInput.checked = checkboxes.length > 0 && checkedCount === checkboxes.length;
    selectAllInput.indeterminate = checkedCount > 0 && checkedCount < checkboxes.length;
    selectionChangeHandler();
  }

  selectAllInput.addEventListener('change', () => {
    rowCheckboxes().forEach((checkbox) => {
      checkbox.checked = selectAllInput.checked;
    });
    syncSelectAllState();
  });

  renderRows();

  return {
    sectionId: section.id,
    element: sectionElement,
    onSelectionChange(handler) {
      selectionChangeHandler = handler;
    },
    getSelectedFiles() {
      return rowCheckboxes()
        .filter((checkbox) => checkbox.checked)
        .map((checkbox) => files[Number.parseInt(checkbox.dataset.fileIndex, 10)])
        .filter(Boolean);
    },
    setFiles(nextFiles) {
      files = normalizeFiles(nextFiles);
      renderRows();
    }
  };
}

function createTableHead(selectAllInput) {
  const theadElement = document.createElement('thead');
  const rowElement = document.createElement('tr');
  const selectAllCell = document.createElement('th');

  selectAllCell.appendChild(selectAllInput);
  rowElement.appendChild(selectAllCell);

  kFileColumns.forEach((column) => {
    const columnHeaderElement = document.createElement('th');
    columnHeaderElement.textContent = column.label;
    rowElement.appendChild(columnHeaderElement);
  });

  theadElement.appendChild(rowElement);
  return theadElement;
}

function createDownloadButton(buttonLabel) {
  const buttonElement = document.createElement('button');
  buttonElement.type = 'button';
  buttonElement.id = 'download-logs';
  buttonElement.className = 'btn btn-start';
  buttonElement.textContent = buttonLabel || 'Download Selected';
  return buttonElement;
}

function resolveSectionFiles(filesBySection, sectionId) {
  if (Array.isArray(filesBySection)) {
    return filesBySection;
  }

  if (isFileMap(filesBySection)) {
    return filesBySection;
  }

  return filesBySection?.[sectionId] || [];
}

function normalizeFiles(files) {
  if (Array.isArray(files)) {
    return files.map((file) => normalizeFileEntry(file));
  }

  if (isFileMap(files)) {
    return Object.entries(files).map(([fileId, file]) => normalizeFileEntry(file, fileId));
  }

  return [];
}

function normalizeFileEntry(file, fallbackId = '') {
  const fileObject = isPlainObject(file) ? file : {};
  const normalizedId = String(fallbackId !== '' ? fallbackId : fileObject.id ?? '');

  return {
    ...fileObject,
    id: normalizedId,
    name: fileObject.name || fileObject.fileName || fileObject.file || '',
    size: fileObject.size || formatSizeBytes(fileObject.size_bytes),
    lastModified: fileObject.lastModified || formatDateTime(fileObject.last_modified)
  };
}

function buildDownloadRequestResponseObject(selectedFiles) {
  const requestResponseObject = {};

  selectedFiles.forEach((file) => {
    const fileId = String(file.id ?? '');
    if (!fileId || !file.name) {
      return;
    }

    requestResponseObject[`file-${fileId}`] = {
      backend_path: `files.${fileId}`,
      value_type: 'string',
      value: file.name
    };
  });

  return requestResponseObject;
}

function downloadBase64File(parameters = {}) {
  const fileName = parameters.file || 'logs_bundle.zip';
  const dataB64 = parameters.dataB64 || '';
  if (!dataB64) {
    return;
  }

  const bytes = base64ToBytes(dataB64);
  const blob = new Blob([bytes], { type: 'application/zip' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');

  link.href = url;
  link.download = fileName;
  link.click();
  setTimeout(() => {
    URL.revokeObjectURL(url);
  }, 0);
}

function base64ToBytes(base64Data) {
  const binary = atob(base64Data);
  const bytes = new Uint8Array(binary.length);

  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }

  return bytes;
}

function formatSizeBytes(sizeBytes) {
  if (!Number.isFinite(sizeBytes)) {
    return '';
  }

  const units = ['B', 'KB', 'MB', 'GB'];
  let value = sizeBytes;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  const fractionDigits = value >= 10 || unitIndex === 0 ? 0 : 1;
  return `${value.toFixed(fractionDigits)} ${units[unitIndex]}`;
}

function formatDateTime(timestamp) {
  if (!timestamp) {
    return '';
  }

  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) {
    return timestamp;
  }

  return date.toLocaleString();
}

function isFileMap(files) {
  return isPlainObject(files) &&
    Object.values(files).every((file) => isPlainObject(file) && hasFileIdentity(file));
}

function hasFileIdentity(file) {
  return Object.hasOwn(file, 'name') ||
    Object.hasOwn(file, 'fileName') ||
    Object.hasOwn(file, 'file');
}

function isPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}
