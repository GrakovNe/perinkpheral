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
  const kept = new Map();
  const store = { getItem: key => kept.has(key) ? kept.get(key) : null, setItem: (key, value) => kept.set(key, value),
                  removeItem: key => kept.delete(key) };
  const { context, messages, byId } = page({
    TextEncoder, MANIFEST_AUTHOR_MAX: 63, CATALOG_INSTALLED_MAX: 24, pageLanguage: "en",
    sources: ["https://example.test/catalog"], offered: [], installed: [], removed: [], catalogsRead: false,
    loadSources: async () => {}, loadRemoved() {}, cancelQueuedInstalls() {}, compatibilityProblem: () => "", localStorage: store,
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

  answer.apps.push({ name: "bad", version: "1.0.0", abi: 3, firmware: 1 });
  await context.loadCatalogs();
  assert.equal(rows().length, 1, "a malformed firmware minimum does not hide valid entries");
  assert.equal(context.offered[0].app.name, "iss");

  answer = null;
  await context.loadCatalogs();
  assert.equal(rows().length, 1, "a reading that failed leaves what was read");
  assert.equal(buttons()[0].disabled, false);

  /* The browser keeps what was read, without the lists of files, and nothing else. */
  const written = JSON.parse(kept.get("perinkpheral-catalog"));
  assert.deepEqual(written.sources, ["https://example.test/catalog"]);
  assert.deepEqual(written.offers, [{ source: 0, app: { name: "iss", title: "ISS", version: "1.0.0" } }]);

  /* Another visit: the kept offers are shown at once, say they are loading, and install nothing. */
  context.catalogsRead = false;
  context.offered = [];
  vm.runInContext("delete loadState.catalog", context);
  context.showKeptCatalog();
  context.drawCatalog();
  assert.deepEqual(said(view).slice(0, 1), ["loading"]);
  assert.equal(rows().length, 1);
  assert.equal(buttons()[0].disabled, true, "nothing is installed from what was not read now");
  answer = { apps: [{ name: "iss", title: "ISS", version: "1.1.0", files: [] }] };
  await context.loadCatalogs();
  assert.equal(view.children.length, 1, "replaced by what the sources say now");
  assert.equal(context.offered[0].app.version, "1.1.0");
  assert.equal(buttons()[0].disabled, false);

  /* Offers kept from other sources than the device has now are dropped before they mislead. */
  context.catalogsRead = false;
  vm.runInContext("delete loadState.catalog", context);
  context.showKeptCatalog();
  context.sources = ["https://example.test/another"];
  answer = null;
  await context.loadCatalogs();
  assert.equal(rows().length, 0);
  assert.deepEqual(said(view), ["failed"]);

  /* Live offers must be invalidated too: indices shift when a source is removed. */
  context.sources = ["https://example.test/official", "https://example.test/a", "https://example.test/b"];
  context.askForApp = async url => ({ apps: url.endsWith("=1") ?
    [{ name: "from-a", version: "1", files: [] }] : [] });
  await context.loadCatalogs();
  assert.equal(context.offered[0].source, 1);
  let release;
  context.askForApp = () => new Promise(resolve => { release = resolve; });
  const oldReading = context.loadCatalogs();
  for (let step = 0; step < 5; step++) await Promise.resolve();
  context.invalidateCatalogs();
  context.sources = ["https://example.test/official", "https://example.test/b"];
  release({ apps: [{ name: "stale", version: "1", files: [] }] });
  await oldReading;
  assert.equal(context.offered.length, 0, "a stale reading cannot restore old indices");
  assert.equal(kept.has("perinkpheral-catalog"), false);
  context.askForApp = async () => { throw new Error("Failed to fetch"); };
  await context.loadCatalogs();
  assert.equal(context.offered.length, 0, "total failure after saving sources leaves no old offers");
  assert.equal(context.catalogsRead, false);
  assert.deepEqual(said(view), ["failed"]);

  context.askForApp = async () => ({ apps: [{ name: "another", version: "1", files: [] }] });
  await context.loadCatalogs();
  context.sources = ["https://example.test/changed-elsewhere"];
  context.askForApp = async () => { throw new Error("Failed to fetch"); };
  await context.loadCatalogs();
  assert.equal(context.offered.length, 0, "a changed list returned by the device also invalidates offers");
  assert.equal(context.catalogsRead, false);

  /* What is not this page's writing is not shown. */
  for (const text of ["", "{", "[]", '{"sources":[1],"offers":[]}', '{"sources":[],"offers":[{"source":0}]}',
                      '{"sources":[],"offers":[{"source":"0","app":{"name":"a","version":"1"}}]}',
                      '{"sources":[],"offers":[{"source":0,"app":{"name":"a","version":"1","files":[]}}]}',
                      /* a title that is no text would stop the page where it sorts the offers */
                      '{"sources":[],"offers":[{"source":0,"app":{"name":"a","version":"1","title":5}}]}',
                      '{"sources":[],"offers":[{"source":0,"app":{"name":"a","version":"1","translations":{"ru":{"title":{}}}}}]}']) {
    kept.set("perinkpheral-catalog", text);
    assert.equal(context.keptCatalog(store), null, text);
  }
  assert.equal(context.keptCatalog(null), null, "a browser with no store");
  kept.set("perinkpheral-catalog", '{"sources":["s"],"offers":[{"source":0,"app":{"name":"a","version":"1","title":"A","translations":{"ru":{"title":"Б"}}}}]}');
  assert.notEqual(context.keptCatalog(store), null, "a sound catalog is kept");
  assert.equal(context.isDrawable({ name: "a", version: "1" }), true);
  assert.equal(context.isDrawable({ name: "a", version: "1", title: 5 }), false);
  assert.equal(context.isDrawable({ name: "a", version: 1 }), false);
  for (const firmware of [1, true, [], {}]) {
    assert.equal(context.isDrawable({ name: "a", version: "1", firmware }), false);
    kept.set("perinkpheral-catalog", JSON.stringify({ sources: ["s"], offers: [
      { source: 0, app: { name: "a", version: "1", firmware } }] }));
    assert.equal(context.keptCatalog(store), null, "malformed firmware is not restored from storage");
  }
  for (const firmware of [undefined, null, "", "0.1.30"])
    assert.equal(context.isDrawable({ name: "a", version: "1", firmware }), true);
  assert.equal(context.isDrawable(null), false);
  /* Where an application connects: a host of the user's own list is said to be the user's. */
  context.URL = URL;
  vm.runInContext(apps.slice(apps.indexOf("function hostOf("), apps.indexOf("function appFacts(")), context);
  const listed = { settings: [{ key: "services", type: "list" }], values: { services: "a.example\nb.example" } };
  assert.equal(context.hostOf(listed, "https://{{services}}/api/v2/status.json"), context.t("Set by the user"));
  assert.equal(context.hostOf(listed, "https://api.example.org/v1?q={{services}}"), "api.example.org");
  context.keepCatalog(null, [], []);
  context.keepCatalog(store, [], [{ source: 0, app: { name: "a", version: "1", description: "x".repeat(200001) } }]);
  assert.equal(kept.has("perinkpheral-catalog"), false, "too large to keep");
}

async function testSourcesSave() {
  for (const refusal of ["Invalid source URL.", "Too many sources.", "Failed to fetch", null]) {
    for (const readFails of [false, true]) {
      const official = "https://example.test/official", stored = [official, "https://example.test/stored"];
      let saved = false;
      const reads = [];
      const { context, byId, messages } = page({
        sources: [], offered: [], catalogsRead: false, CATALOG_SOURCES_MAX: 4,
        drawTiles() {}, loadRemoved() {}, cancelQueuedInstalls() {},
        fetch: async (url, options) => {
          if (options && options.method === "POST") {
            assert.deepEqual(JSON.parse(options.body).sources, ["https://example.test/valid", "typo"]);
            if (refusal === "Failed to fetch") throw new Error(refusal);
            saved = !refusal;
            return { ok: saved, status: saved ? 200 : 400,
              json: async () => saved ? { ok: "Saved." } : { error: refusal } };
          }
          reads.push(url);
          if (readFails) throw new Error("Device unreachable.");
          return { ok: true, json: async () => ({ sources: stored }) };
        },
        askForApp: async url => { reads.push(url); return { apps: [] }; },
        editableList: (box, { values }) => {
          box.replaceChildren(...values.map(value => element("input", { value })));
          box.values = () => box.children.slice(1).map(row => row.value.trim()).filter(Boolean);
        },
      });
      vm.runInContext(apps.slice(apps.indexOf("async function loadSources("), apps.indexOf("function catalogAuthorIsAllowed(")), context);
      vm.runInContext(apps.slice(apps.indexOf("function catalogAuthorIsAllowed("), apps.indexOf("/* ---- sources ---- */")), context);
      vm.runInContext(apps.slice(apps.indexOf("function catalogAddress("), apps.indexOf('byId("open-sources")')), context);
      vm.runInContext("const pendingAppInstalls = new Map();", context);
      const submitStart = apps.indexOf('byId("sources").addEventListener');
      vm.runInContext(apps.slice(submitStart, apps.indexOf('byId("refresh-catalogs").addEventListener', submitStart)), context);
      const rows = byId("source-rows"), typed = [official, " https://example.test/valid ", "typo", ""];
      context.editableList(rows, { values: typed });
      const originalRows = rows.children;
      byId("save-sources").textContent = "Save";
      const saving = byId("sources").listeners.submit({ preventDefault() {}, submitter: byId("save-sources") });
      if (readFails) await assert.rejects(saving, /Device unreachable/);
      else await saving;
      assert(reads.includes("/api/sources"), "the stored source list is always fetched after a save");
      if (refusal) {
        assert.strictEqual(rows.children, originalRows, "a refused save preserves rows even if the stored-list read fails");
        assert.deepEqual(rows.children.map(row => row.value), typed, "valid rows, typo, whitespace and blank row survive");
        assert(messages.some(message => message.kind === "error"));
      } else if (!readFails) {
        assert.equal(saved, true);
        assert.deepEqual(rows.children.map(row => row.value), stored, "successful saves show the stored list");
      }
      if (!readFails) {
        assert.deepEqual(reads, ["/api/sources", "/api/catalog?source=0", "/api/catalog?source=1"],
          "catalogs use the stored sources even when the edited rows are preserved");
        assert.equal(context.catalogsRead, true);
      }
    }
  }
}

module.exports = async function testLoadStates() {
  await testReading();
  await testCatalog();
  await testSourcesSave();
  console.log("Load states: loading, failed with Refresh, empty, offers kept while read again and between visits passed.");
};
if (require.main === module) module.exports().catch(error => { console.error(error); process.exitCode = 1; });
