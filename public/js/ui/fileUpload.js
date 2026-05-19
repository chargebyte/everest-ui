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
      const chunkBytes = new Uint8Array(chunkBuffer);

      if (chunkIndex !== selectedFileState.nextHashChunkIndex) {
        throw new Error('Chunks must be read sequentially to compute sha256');
      }

      selectedFileState.sha256Hasher.update(chunkBytes);
      selectedFileState.nextHashChunkIndex += 1;

      if (selectedFileState.nextHashChunkIndex === selectedFileState.chunk_count) {
        selectedFileState.sha256 = selectedFileState.sha256Hasher.digestHex();
      }

      return {
        chunk_index: chunkIndex,
        dataB64: arrayBufferToBase64(chunkBuffer),
        sha256: selectedFileState.sha256
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
    dataB64: '',
    sha256: '',
    sha256Hasher: createSha256Hasher(),
    nextHashChunkIndex: 0
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
    dataB64: fileState.dataB64,
    sha256: fileState.sha256
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

function createSha256Hasher() {
  const k = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  ]);
  const h = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  ]);
  const w = new Uint32Array(64);
  const block = new Uint8Array(64);
  let blockLength = 0;
  let totalLengthBytes = 0;

  function rotr(value, shift) {
    return (value >>> shift) | (value << (32 - shift));
  }

  function processBlock(bytes) {
    for (let index = 0; index < 16; index += 1) {
      const offset = index * 4;
      w[index] =
        ((bytes[offset] << 24) | (bytes[offset + 1] << 16) |
         (bytes[offset + 2] << 8) | bytes[offset + 3]) >>> 0;
    }

    for (let index = 16; index < 64; index += 1) {
      const s0 =
        rotr(w[index - 15], 7) ^
        rotr(w[index - 15], 18) ^
        (w[index - 15] >>> 3);
      const s1 =
        rotr(w[index - 2], 17) ^
        rotr(w[index - 2], 19) ^
        (w[index - 2] >>> 10);
      w[index] = (w[index - 16] + s0 + w[index - 7] + s1) >>> 0;
    }

    let a = h[0];
    let b = h[1];
    let c = h[2];
    let d = h[3];
    let e = h[4];
    let f = h[5];
    let g = h[6];
    let hh = h[7];

    for (let index = 0; index < 64; index += 1) {
      const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const temp1 = (hh + s1 + ch + k[index] + w[index]) >>> 0;
      const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (s0 + maj) >>> 0;

      hh = g;
      g = f;
      f = e;
      e = (d + temp1) >>> 0;
      d = c;
      c = b;
      b = a;
      a = (temp1 + temp2) >>> 0;
    }

    h[0] = (h[0] + a) >>> 0;
    h[1] = (h[1] + b) >>> 0;
    h[2] = (h[2] + c) >>> 0;
    h[3] = (h[3] + d) >>> 0;
    h[4] = (h[4] + e) >>> 0;
    h[5] = (h[5] + f) >>> 0;
    h[6] = (h[6] + g) >>> 0;
    h[7] = (h[7] + hh) >>> 0;
  }

  return {
    update(bytes) {
      totalLengthBytes += bytes.length;

      let inputOffset = 0;
      while (inputOffset < bytes.length) {
        const copyLength = Math.min(64 - blockLength, bytes.length - inputOffset);
        block.set(bytes.subarray(inputOffset, inputOffset + copyLength), blockLength);
        blockLength += copyLength;
        inputOffset += copyLength;

        if (blockLength === 64) {
          processBlock(block);
          blockLength = 0;
        }
      }
    },
    digestHex() {
      block[blockLength] = 0x80;
      blockLength += 1;

      if (blockLength > 56) {
        while (blockLength < 64) {
          block[blockLength] = 0;
          blockLength += 1;
        }
        processBlock(block);
        blockLength = 0;
      }

      while (blockLength < 56) {
        block[blockLength] = 0;
        blockLength += 1;
      }

      const bitLengthLow = (totalLengthBytes << 3) >>> 0;
      const bitLengthHigh = Math.floor(totalLengthBytes / 0x20000000) >>> 0;

      block[56] = (bitLengthHigh >>> 24) & 0xff;
      block[57] = (bitLengthHigh >>> 16) & 0xff;
      block[58] = (bitLengthHigh >>> 8) & 0xff;
      block[59] = bitLengthHigh & 0xff;
      block[60] = (bitLengthLow >>> 24) & 0xff;
      block[61] = (bitLengthLow >>> 16) & 0xff;
      block[62] = (bitLengthLow >>> 8) & 0xff;
      block[63] = bitLengthLow & 0xff;
      processBlock(block);

      let hex = '';
      for (const value of h) {
        hex += value.toString(16).padStart(8, '0');
      }
      return hex;
    }
  };
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
