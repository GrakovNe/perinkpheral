/* SPDX-License-Identifier: MIT
 * The start: everything the device has to say, read once, and again after a restart. */
"use strict";

/* A device on its setup access point is not online: the catalogs, which it
 * would fetch for the page, wait for a network, and the page opens where the
 * network is set. */
const isOffline = () => status.wifi === "setup" || status.wifi === "rescue";

async function loadAll() {
  try {
    await Promise.all([loadStatus(), loadSettings(), loadApps(false)]);
  } catch (error) {
    wantGlyphs();
    /* The catalog is not asked for then, and must not go on saying it is. */
    if (!catalogsRead) loadState.catalog = "failed";
    if (stateOf("sources") !== "ready") sayState(byId("source-rows"), "failed", loadSources);
    drawTiles();
    throw error;
  }
  if (!catalogsRead) delete loadState.catalog;
  wantGlyphs();
  /* Control is for a firmware with the development tools: the status says so. */
  byId("nav-control").hidden = !status.dev_tools;
  if (!status.dev_tools && currentSection() === "control") location.hash = "#device";
  if (isOffline()) {
    await loadSources();
    /* the catalog is on the internet */
    loadState.catalog = "offline";
    drawTiles();
    if (!location.hash) location.hash = "#settings:network";
    return;
  }
  loadCatalogs();
}

window.addEventListener("hashchange", showSection);
showPane("general");
showSection();
/* What the browser kept of the catalog is in sight before the device is asked anything. */
try {
  showKeptCatalog();
  drawTiles();
} catch (error) {
  /* What was kept cannot be drawn: it is dropped, and the page starts as it does without it. */
  forgetKeptCatalog();
  drawTiles();
}
loadAll().catch(() => afterRestart("Connecting", 2000));
