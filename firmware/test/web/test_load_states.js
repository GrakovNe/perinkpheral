// SPDX-License-Identifier: MIT
// What a place of the page says while it waits, when nothing came and when nothing is there.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const core = read("core.js"), apps = read("apps.js");

/* An element as far as the page's own code asks of one. */
function element(tag, properties = {}) {
  const node = {
    tag, children: [], dataset: {}, listeners: {}, disabled: false, style: {}, offsetWidth: 80, ...properties,
    classList: { contains: name => (node.className || "").split(" ").includes(name), add() {}, remove() {} },
    append(...nodes) { node.children.push(...nodes.filter(child => typeof child !== "string")); },
    replaceChildren(...nodes) { node.children = nodes; },
    addEventListener(event, handler) { node.listeners[event] = handler; },
  };
  return node;
}

function page(extra = {}) {
  const elements = new Map(), messages = [];
  const context = vm.createContext({
    document: { getElementById: id => elements.get(id) || elements.set(id, element("div", { value: "" })).get(id),
                createElement: tag => element(tag), querySelector: () => null, body: element("body") },
    t: (text, ...parts) => text.replace(/\{(\d)\}/g, (_, at) => parts[at]), setTimeout: callback => callback(), Error, Promise, ...extra,
  });
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  /* What a test says of the device comes after the page's own: it stands in for it. */
  Object.assign(context, extra, { toast: (text, kind) => messages.push({ text, kind }) });
  return { context, messages, byId: id => context.document.getElementById(id) };
}

const said = box => box.children.map(child => child.dataset.state || child.textContent);

async function testReading() {
  const { context, byId } = page();
  const state = what => vm.runInContext(`stateOf("${what}")`, context);
  assert.equal(state("apps"), "loading");
  await assert.rejects(context.reading("apps", async () => { throw new Error("Failed to fetch"); }));
  assert.equal(state("apps"), "failed", "nothing ever came");
  assert.equal(await context.reading("apps", async () => 7), 7);
  assert.equal(state("apps"), "ready");
  await assert.rejects(context.reading("apps", async () => { throw new Error("HTTP 503"); }));
  assert.equal(state("apps"), "ready", "what was read once stays in sight");

  const box = byId("tiles");
  context.sayState(box, "loading");
  assert.deepEqual(said(box), ["loading"]);
  assert.equal(box.children[0].textContent, "Loading...");
  assert.equal(box.children[0].children.length, 0, "nothing to press while it loads");
  let asked = 0, fail = true;
  const again = async () => { asked++; if (fail) throw new Error("Device busy."); };
  context.sayState(box, "failed", again);
  const note = box.children[0], refresh = note.children[0];
  assert.equal(note.textContent, "Could not load.");
  assert.equal(refresh.textContent, "Refresh");
  context.sayState(box, "failed", again);
  assert.equal(box.children[0], note, "the same note while it says the same: its button is not replaced under a press");
  await refresh.listeners.click();
  assert.equal(asked, 1);
  assert.equal(refresh.disabled, false, "pressable again after a failure");
  context.sayState(box, "empty");
  assert.equal(box.children[0].textContent, "Empty.");
}

/* The catalog: loading, then failed with Refresh; rows it had stay while it is read again. */
async function testCatalog() {
  let answer = null;
  const { context, messages, byId } = page({
    TextEncoder, MANIFEST_AUTHOR_MAX: 63, CATALOG_INSTALLED_MAX: 24, pageLanguage: "en",
    sources: ["https://example.test/catalog"], offered: [], installed: [], removed: [], catalogsRead: false,
    loadSources: async () => {}, loadGlyphs: async () => {}, compatibilityProblem: () => "",
    picture: () => element("div"), markPendingInstall() {}, install() {},
    offerSays: (offer, field) => offer.app[field] || "", appUpdateAvailable: () => false,
    askForApp: async () => { if (!answer) throw new Error("Failed to fetch"); return answer; },
  });
  context.drawTiles = () => context.drawCatalog();
  vm.runInContext(apps.slice(apps.indexOf("function drawCatalog()"), apps.indexOf('byId("catalog-search").addEventListener')), context);
  vm.runInContext(apps.slice(apps.indexOf("function catalogAuthorIsAllowed("), apps.indexOf("/* ---- sources ---- */")), context);
  const view = byId("catalog-rows");
  const rows = () => view.children.filter(child => child.className === "offer");
  const buttons = () => rows().map(row => row.children.find(child => child.tag === "button"));

  context.drawCatalog();
  assert.deepEqual(said(view), ["loading"], "never an empty place while the sources are asked");
  const twice = [context.loadCatalogs(), context.loadCatalogs()];
  assert.equal(twice[0], twice[1], "one reading at a time");
  await twice[0];
  assert.deepEqual(said(view), ["failed"]);
  assert.equal(view.children[0].children[0].textContent, "Refresh");
  assert.equal(messages.at(-1).text, "Source unavailable: https://example.test/catalog");

  answer = { apps: [] };
  await context.loadCatalogs();
  assert.deepEqual(said(view), ["empty"]);

  answer = { apps: [{ name: "iss", title: "ISS", version: "1.0.0", files: [] }] };
  await context.loadCatalogs();
  assert.equal(rows().length, 1);
  assert.equal(view.children.length, 1, "the rows and no note");
  assert.equal(buttons()[0].disabled, false);

  answer = null;
  await context.loadCatalogs();
  assert.equal(rows().length, 1, "a reading that failed leaves what was read");
  assert.equal(buttons()[0].disabled, false);

  /* What is shown before the sources answer - offers kept from another visit - installs nothing. */
  context.catalogsRead = false;
  vm.runInContext("delete loadState.catalog", context);
  context.drawCatalog();
  assert.deepEqual(said(view).slice(0, 1), ["loading"]);
  assert.equal(rows().length, 1);
  assert.equal(buttons()[0].disabled, true, "nothing is installed from what was not read now");
}

module.exports = async function testLoadStates() {
  await testReading();
  await testCatalog();
  console.log("Load states: loading, failed with Refresh, empty, and offers kept while read again passed.");
};
if (require.main === module) module.exports().catch(error => { console.error(error); process.exitCode = 1; });
