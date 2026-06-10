// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderSettingsTableBlock } from '../ui/settingsTable.js';
import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { mapResponse } from '../protocol/responseMapper.js';

export function renderOcppPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  // load runtime parameter object from parameter catalog
  const pageConfig = loadPageConfig(MODULE_IDS.OCPP, parameterCatalog);
  const settingsTableBlock = pageConfig.blocks.find((block) => block.kind === 'settings_table');

  // render UI elements
  container.innerHTML = '';

  const pageElement = document.createElement('div');
  pageElement.className = 'page';
  pageElement.innerHTML = `<h1>${pageConfig.title}</h1>`;

  const settingsTable = renderSettingsTableBlock(settingsTableBlock, {
    buttonLabel: 'Save Configuration'
  });

  settingsTable.bindSubmit(() => {
    const values = settingsTable.getValues(settingsTable.requestResponseObject);
    const writeOcppConfigRequest = buildRequest(
      pageConfig.actions.write_settings.group,
      pageConfig.actions.write_settings.action,
      values
    );
    sendOcppRequest(
      sendPayload,
      addLog,
      writeOcppConfigRequest,
      pageConfig.actions.write_settings.group,
      pageConfig.actions.write_settings.action
    );
  });

  pageElement.appendChild(settingsTable.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'ocpp.read_settings.result') {
        addLog('ocpp.read_settings.result received');
        settingsTable.setValues(
          mapResponse('settings_table', settingsTable.requestResponseObject, message)
        );
        return;
      }

      if (message.type === 'ocpp.write_settings.ack') {
        addLog('ocpp.write_settings.ack received');
      }

      if (message.type === 'ocpp.read_settings.error') {
        const error = message.parameters.error
        addLog(`ocpp.read_settings.error: ${error}`);
      }

      if (message.type === 'ocpp.write_settings.error') {
        const error = message.parameters.error
        addLog(`ocpp.write_settings.error: ${error}`);
      }
    },
    onConnectionChange(connected) {
      // request current EVerest configuration after page is loaded and WS is connected
      if (connected === true) {
        const values = settingsTable.getValues(settingsTable.requestResponseObject);
        const readOcppConfigRequest = buildRequest(
          pageConfig.actions.read_settings.group,
          pageConfig.actions.read_settings.action,
          values
        );
        sendOcppRequest(
          sendPayload,
          addLog,
          readOcppConfigRequest,
          pageConfig.actions.read_settings.group,
          pageConfig.actions.read_settings.action
        );
      }
    },
    destroy() {}
  };
}

function sendOcppRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
