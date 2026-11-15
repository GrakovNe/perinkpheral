/* SPDX-License-Identifier: MIT
 * The start: everything the device has to say, read once, and again after a restart. */
"use strict";

async function loadAll() {
  await Promise.all([loadStatus(), loadSettings(), loadApps()]);
  loadCatalogs();
}

window.addEventListener("hashchange", showSection);
showPane("general");
showSection();
loadAll().catch(() => { byId("tiles").textContent = "Device unreachable."; afterRestart(); });
