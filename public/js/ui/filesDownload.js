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
      files = Array.isArray(nextFiles) ? nextFiles.map(normalizeFileEntry) : [];
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

  return filesBySection?.[sectionId] || [];
}

function normalizeFileEntry(file) {
  return {
    ...file,
    name: file.name || file.fileName || file.file || '',
    size: file.size || '',
    lastModified: file.lastModified || file.last_modified || ''
  };
}
