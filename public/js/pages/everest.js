// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

import { renderSettingsTableBlock } from '../ui/settingsTable.js';
import { loadPageConfig } from '../config/pageConfigAdapter.js';
import { MODULE_IDS } from '../protocol/constants.js';
import { buildRequest } from '../protocol/requestBuilder.js';
import { mapResponse } from '../protocol/responseMapper.js';

export function renderEverestPage(container, {
  parameterCatalog,
  sendPayload,
  addLog
}) {
  // load runtime parameter object from parameter catalog
  const pageConfig = loadPageConfig(MODULE_IDS.EVEREST, parameterCatalog);
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
    const writeEverestConfigRequest = buildRequest(
      pageConfig.actions.write_config_parameters.group,
      pageConfig.actions.write_config_parameters.action,
      values
    );
    sendEverestRequest(
      sendPayload,
      addLog,
      writeEverestConfigRequest,
      pageConfig.actions.write_config_parameters.group,
      pageConfig.actions.write_config_parameters.action
    );
  });

  pageElement.appendChild(settingsTable.element);
  container.appendChild(pageElement);

  return {
    onMessage(message) {
      if (message.type === 'everest.read_config_parameters.result') {
        addLog('everest.read_config_parameters.result received');
        settingsTable.setValues(
          mapResponse('settings_table', settingsTable.requestResponseObject, message)
        );
        return;
      }

      if (message.type === 'everest.write_config_parameters.ack') {
        addLog('everest.write_config_parameters.ack received');
      }

      if (message.type === 'everest.read_config_parameters.error') {
        const error = message.parameters.error
        addLog(`everest.read_config_parameters.error: ${error}`);
      }

      if (message.type === 'everest.write_config_parameters.error') {
        const error = message.parameters.error
        addLog(`everest.write_config_parameters.error: ${error}`);
      }
    },
    onConnectionChange(connected) {
      // request current EVerest configuration after page is loaded and WS is connected
      if (connected === true) {
        const values = settingsTable.getValues(settingsTable.requestResponseObject);
        const readEverestConfigRequest = buildRequest(
          pageConfig.actions.read_config_parameters.group,
          pageConfig.actions.read_config_parameters.action,
          values
        );
        sendEverestRequest(
          sendPayload,
          addLog,
          readEverestConfigRequest,
          pageConfig.actions.read_config_parameters.group,
          pageConfig.actions.read_config_parameters.action
        );
      }
    },
    destroy() {}
  };
}

function sendEverestRequest(sendPayload, addLog, request, group, action) {
  const ok = sendPayload(request);
  addLog(`${group}.${action} ${ok ? 'sent' : 'rejected'}`);
  return ok;
}
