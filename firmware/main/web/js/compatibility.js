/* SPDX-License-Identifier: MIT
 * Catalog compatibility is a set membership check and a firmware minimum. */
"use strict";

function appCompatibility(app, device) {
  if (!Array.isArray(device.supported_abis) || !device.supported_abis.includes(app.abi)) return "abi";
  if (app.firmware && (!device.firmware || isNewer(releaseOf(app.firmware), releaseOf(device.firmware))))
    return "firmware";
  return "";
}
