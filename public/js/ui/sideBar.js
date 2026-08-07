// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

const kNavEntries = [
  { page: 'everest', label: 'EVerest Config' },
  { page: 'safety', label: 'Safety Controller' },
  { page: 'ocpp', label: 'OCPP Config' },
  { page: 'pcap', label: 'PCAP Trace' },
  { page: 'firmware', label: 'Firmware Update' },
  { page: 'system_logs', label: 'System Logs' }
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
