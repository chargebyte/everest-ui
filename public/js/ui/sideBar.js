// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kNavEntries = [
  { page: 'safety', label: 'Safety Controller' },
  { page: 'everest', label: 'EVerest Config' },
  { page: 'ocpp', label: 'OCPP Config' },
  { page: 'pcap', label: 'PCAP Trace' },
  { page: 'firmware', label: 'Firmware Update' },
  { page: 'logs', label: 'Error Logs' }
];

export function renderSideBar() {
  const items = kNavEntries
    .map((entry) => `<li class="nav-item" data-page="${entry.page}">${entry.label}</li>`)
    .join('');

  return `
    <aside class="sidebar">
      <ul class="nav-list">
        ${items}
      </ul>
    </aside>
  `;
}
