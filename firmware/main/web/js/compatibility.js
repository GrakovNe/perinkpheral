/* SPDX-License-Identifier: MIT
 * Catalog compatibility is a set membership check and a firmware minimum. */
"use strict";

function appCompatibility(app, device) {
  if (!Array.isArray(device.supported_abis) || !device.supported_abis.includes(app.abi)) return "abi";
  if (app.firmware && (!device.firmware || isNewer(releaseOf(app.firmware), releaseOf(device.firmware))))
    return "firmware";
  return "";
}

/* Stable app releases use the daily updater's X.Y.Z order: parts 0..999,
 * without leading zeroes. Other labels have no ordering contract and keep
 * the existing manual replacement behavior, including prereleases. */
function appUpdateAvailable(installedVersion, offeredVersion) {
  const stable = version => typeof version === "string" &&
    /^(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})$/.exec(version);
  const installed = stable(installedVersion), offered = stable(offeredVersion);
  if (!installed || !offered) return installedVersion !== offeredVersion;
  const number = parts => parts.slice(1).reduce((value, part) => value * 1000 + Number(part), 0);
  return number(offered) > number(installed);
}
