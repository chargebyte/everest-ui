// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

export function createFeatureAvailability({ setPageAvailable, getActiveRoute, returnToDefault }) {
  let features = {};
  const allows = (route) => !['network', 'safety'].includes(route) || features[route] === true;
  function update(values = {}) {
    features = { network: values.network === true, safety: values.safety === true };
    for (const route of ['network', 'safety']) {
      setPageAvailable(route, allows(route));
    }
    if (!allows(getActiveRoute())) returnToDefault();
  }
  return { allows, update };
}
