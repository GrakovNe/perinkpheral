/* SPDX-License-Identifier: MIT
 * The start: everything the device has to say, read once, and again after a restart. */
"use strict";

/* A device on its setup access point is not online: the catalogs, which it
 * would fetch for the page, wait for a network, and the page opens where the
 * network is set. */
const isOffline = () => status.wifi === "setup" || status.wifi === "rescue";

async function loadAll() {
  await Promise.all([loadStatus(), loadSettings(), loadApps()]);
  lookAtScreen(true);
  if (isOffline()) {
    await loadSources();
    /* the packs on offer are the official catalog's, on the internet */
    byId("glyph-rows").textContent = "Catalog unavailable.";
    if (!location.hash) location.hash = "#settings:network";
    return;
  }
  loadCatalogs();
}

window.addEventListener("hashchange", showSection);
showPane("general");
showSection();
loadAll().catch(() => { byId("tiles").textContent = "Device unreachable."; afterRestart(); });
