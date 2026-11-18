// SPDX-License-Identifier: MIT
// Actual page requests and install actions, with a scripted device and virtual clock.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const core = read("core.js");
const apps = read("apps.js");
const installCode = apps.slice(apps.indexOf("const appInstallQueue"), apps.indexOf("/* An icon is"));
const offer = { source: 0, app: { name: "iss", version: "1.2.3", files: [{ name: "app.wasm" }] } };

async function run(replies, action = "install", options = {}) {
  const requests = [], messages = [], delays = [], listeners = {};
  const classes = new Set();
  const button = { textContent: "Install", style: {}, offsetWidth: 90, disabled: false,
    classList: { add: name => classes.add(name), remove: name => classes.delete(name) },
    addEventListener: (name, handler) => { listeners[name] = handler; } };
  let reloads = 0;
  const context = vm.createContext({
    button, offer: { ...offer, app: { ...offer.app, version: options.offeredVersion || offer.app.version } },
    Error, encodeURIComponent, TextEncoder, MANIFEST_AUTHOR_MAX: 63,
    window: { addEventListener() {} },
    t: (text, ...values) => text.replace(/\{(\d+)\}/g, (_, at) => values[at]),
    installed: options.fresh ? [] : [{ name: "iss", version: options.installedVersion || "1.0.0" }, { name: "clock", version: "1.0.0" }],
    document: { getElementById: () => button },
    compatibilityProblem: () => options.incompatible ? "Update the firmware." : null,
    confirmed: async () => false,
    offerSays: offer => offer.app.name === "clock" ? "Clock" : "ISS",
    updates: () => [offer, { source: 0, app: { name: "clock", version: "2.0.0", files: [] } }],
    loadApps: async () => { reloads++; },
    loadSources: async () => {}, sources: ["https://example.test/catalog"],
    offered: [], catalogsRead: false, drawTiles() {}, loadGlyphs() {},
    setTimeout: (callback, delay) => {
      if (delay !== 1400) {
        delays.push(delay);
        assert.equal(button.disabled, action !== "catalog");
        assert.equal(classes.has("busy"), action !== "catalog");
      }
      callback();
    },
    fetch: async (url, request) => {
      requests.push({ url, request });
      const reply = replies[Math.min(requests.length - 1, replies.length - 1)];
      if (reply instanceof Error) throw reply;
      return { ok: reply[0] === 200, status: reply[0], json: async () =>
        reply[0] === 200 ? { ok: "Saved.", apps: [] } : { error: reply[1] } };
    },
  });
  vm.runInContext(core.slice(0, core.indexOf('const CONFIRM_ON_DEVICE')), context);
  vm.runInContext(read("compatibility.js"), context);
  context.toast = (text, kind, stays) => {
    const message = { text, kind, stays, dismissed: false };
    messages.push(message);
    return () => { message.dismissed = true; };
  };
  vm.runInContext(installCode, context);
  const updateStart = apps.indexOf('byId("update-all").addEventListener');
  vm.runInContext(apps.slice(updateStart, apps.indexOf('byId("auto-update-apps").addEventListener', updateStart)), context);
  vm.runInContext(apps.slice(apps.indexOf("function catalogAuthorIsAllowed("), apps.indexOf("/* ---- sources ---- */")), context);
  if (action === "update-all") await listeners.click();
  else if (action === "catalog") await context.loadCatalogs();
  else if (action === "save") await vm.runInContext('send(button, "/api/settings", post({font: "jersey"}))', context);
  else await context.install(button, context.offer);
  assert.equal(button.disabled, false);
  assert.equal(classes.has("busy"), false);
  assert(messages.filter(message => message.stays).every(message => message.dismissed));
  return { requests, messages, delays, reloads };
}

async function testVersionOrdering() {
  const context = vm.createContext({});
  vm.runInContext(read("compatibility.js"), context);
  const cases = [
    ["0.1.3", "0.1.2", false], ["0.1.3", "0.1.3", false], ["0.1.3", "0.1.4", true],
    ["0.1.9", "0.1.10", true], ["0.1.10", "0.1.9", false],
    ["0.2.0", "0.1.999", false], ["0.999.999", "1.0.0", true],
    ["999.999.999", "0.0.0", false], ["0.0.0", "0.0.1", true],
    // No new ordering is imposed on labels outside the existing stable-version contract.
    ["0.1.3-beta", "0.1.3", true], ["0.1.3", "0.1.3-beta", true],
    ["0.1.3-beta", "0.1.3-beta", false], ["0.1.3+build", "0.1.3", true],
    ["nightly", "custom", true], ["custom", "custom", false],
    ["01.2.3", "1.2.3", true], ["1000.0.0", "999.0.0", true],
  ];
  for (const [installedVersion, offeredVersion, expected] of cases)
    assert.equal(context.appUpdateAvailable(installedVersion, offeredVersion), expected,
      `${installedVersion} -> ${offeredVersion}`);

  // A click saved before another refresh must not reinstall an equal or older package.
  for (const offeredVersion of ["0.1.2", "0.1.3"]) {
    const result = await run([[200]], "install", { installedVersion: "0.1.3", offeredVersion });
    assert.equal(result.requests.length, 0);
    assert.equal(result.reloads, 0);
    assert.equal(result.messages.length, 0);
  }
  for (const offeredVersion of ["0.1.4", "nightly", "0.1.4-beta"]) {
    const result = await run([[200]], "install", { installedVersion: "0.1.3", offeredVersion });
    assert.equal(result.requests.length, 1);
    assert.equal(JSON.parse(result.requests[0].request.body).version, offeredVersion);
  }
  const fresh = await run([[200]], "install", { fresh: true, offeredVersion: "0.1.2" });
  assert.equal(fresh.requests.length, 1, "first installation has no older installed version to compare");
}

async function testCachedCatalogOffers() {
  const rendered = [], requests = [], elements = new Map();
  const make = (tag, attributes = {}) => {
    const node = { tag, ...attributes, children: [], listeners: {},
      append(...children) { this.children.push(...children); },
      replaceChildren(...children) { this.children = children; },
      classList: { add() {}, remove() {} },
      addEventListener(event, handler) { this.listeners[event] = handler; },
    };
    rendered.push(node);
    return node;
  };
  const byId = id => {
    if (!elements.has(id)) elements.set(id, make("div", { value: "" }));
    return elements.get(id);
  };
  const context = vm.createContext({
    installed: [{ name: "air", title: "Air quality", version: "0.1.3", settings: [] }],
    offered: [], removed: [], settings: {}, pageLanguage: "en", catalogsRead: true,
    CATALOG_INSTALLED_MAX: 24, GEAR: [], INFO: [], byId, make,
    isIcon: () => false, picture: () => ({}), iconButton: () => ({}),
    compatibilityProblem: () => "", offerSays: (offer, field) => offer.app[field] || "",
    t: text => text, toast: () => () => {}, loadApps: async () => {}, post: body => body,
    window: { addEventListener() {} }, drawTiles() {},
    busy: async (_, work) => { await work(); return true; },
    askForApp: async (url, body) => { requests.push({ url, body }); },
  });
  vm.runInContext(read("compatibility.js"), context);
  vm.runInContext(installCode, context);
  vm.runInContext(apps.slice(apps.indexOf("function tile("), apps.indexOf("/* ---- what an application is")), context);
  const updatesStart = apps.indexOf("const updates = () =>");
  vm.runInContext(apps.slice(updatesStart, apps.indexOf('byId("catalog-search").addEventListener')), context);
  const bulkStart = apps.indexOf('byId("update-all").addEventListener');
  vm.runInContext(apps.slice(bulkStart, apps.indexOf('byId("auto-update-apps").addEventListener', bulkStart)), context);
  for (const version of ["0.1.2", "0.1.3", "0.1.4"]) {
    const expected = version === "0.1.4";
    context.offered = [{ source: 0, app: { name: "air", title: "Air quality", version, files: [] } }];
    rendered.length = 0;
    context.tile(context.installed[0], context.offered[0]);
    assert.equal(rendered.some(node => node.tag === "button" && node.textContent === "Update"), expected,
      `installed tile with cached/catalog ${version}`);
    rendered.length = 0;
    context.drawCatalog();
    const button = rendered.find(node => node.tag === "button");
    assert.equal(button.textContent, expected ? "Update" : "Installed");
    assert.equal(button.disabled, !expected);
    assert.equal(typeof button.listeners.click, expected ? "function" : "undefined");
    assert.equal(vm.runInContext("updates().length", context), expected ? 1 : 0);
    requests.length = 0;
    await byId("update-all").listeners.click();
    assert.equal(requests.length, expected ? 1 : 0, `bulk update with catalog ${version}`);
    if (expected) assert.equal(requests[0].body.version, version);
  }
}

async function testAutoUpdateSetting() {
  let click, saved = true;
  const sent = [], attributes = {}, messages = [];
  const button = {
    textContent: "Auto-update", style: {}, offsetWidth: 90, disabled: false,
    classList: { add() {}, remove() {} },
    addEventListener: (event, handler) => { assert.equal(event, "click"); click = handler; },
    setAttribute: (key, value) => { attributes[key] = value; },
  };
  const context = vm.createContext({
    settings: { app_auto_update: false }, document: { getElementById: () => button },
    t: text => text, setTimeout: callback => callback(),
    fetch: async (url, request) => {
      assert.equal(url, "/api/settings", "Auto-update must not install applications");
      assert.equal(request.method, "POST");
      const body = JSON.parse(request.body);
      sent.push({ url, method: request.method, body });
      // /api/settings value_of(APPCONF_FLAG) accepts JSON booleans only.
      const valid = typeof body.app_auto_update === "boolean";
      const status = !valid ? 400 : saved ? 200 : 500;
      return { ok: status === 200, status, json: async () => status === 200 ?
        { ok: "Saved." } : { error: valid ? "Storage error." : "Invalid value." } };
    },
  });
  vm.runInContext(core.slice(0, core.indexOf('const CONFIRM_ON_DEVICE')), context);
  vm.runInContext(read("compatibility.js"), context);
  context.toast = text => messages.push(text);
  vm.runInContext(apps.slice(apps.indexOf('byId("auto-update-apps").addEventListener'), apps.indexOf("async function loadApps()")), context);
  await click();
  assert.equal(context.settings.app_auto_update, true);
  assert.equal(attributes["aria-pressed"], "true");
  assert.equal(sent[0].url, "/api/settings");
  assert.equal(sent[0].method, "POST");
  assert.equal(sent[0].body.app_auto_update, true);
  assert.deepEqual(messages, []);
  saved = false;
  await click();
  assert.equal(context.settings.app_auto_update, true);
  assert.equal(attributes["aria-pressed"], "true");
  assert.deepEqual(messages, ["Storage error."]);
  saved = true;
  await click();
  assert.equal(context.settings.app_auto_update, false);
  assert.equal(attributes["aria-pressed"], "false");
  assert.deepEqual(sent.map(request => request.body.app_auto_update), [true, false, false]);
  assert.equal(sent.length, 3, "one settings request per toggle, including a failed save");
  assert.equal(button.disabled, false);
}

function testUpdateAllVisibility() {
  const elements = new Map();
  const byId = id => {
    if (!elements.has(id)) elements.set(id, {
      hidden: false, attributes: {}, firstElementChild: { style: {} },
      setAttribute(key, value) { this.attributes[key] = value; },
      replaceChildren() {},
    });
    return elements.get(id);
  };
  const context = vm.createContext({
    CATALOG_INSTALLED_MAX: 24, settings: { app_auto_update: false },
    installed: [{ name: "iss", version: "1.0.0" }, { name: "clock", version: "1.0.0" }],
    offered: [], byId, inMenuOrder: apps => apps, tile: () => ({}), drawCatalog() {},
    compatibilityProblem: app => app.incompatible ? "Update the firmware." : "",
    t: (text, ...values) => text.replace(/\{(\d+)\}/g, (_, at) => values[at]),
  });
  vm.runInContext(read("compatibility.js"), context);
  const updatesStart = apps.indexOf("const updates = () =>");
  vm.runInContext(apps.slice(updatesStart, apps.indexOf("/* The catalog:", updatesStart)), context);
  vm.runInContext(apps.slice(apps.indexOf("function drawTiles()"),
    apps.indexOf("/* Bulk and individual updates share the queue")), context);
  for (const automatic of [false, true]) {
    context.settings.app_auto_update = automatic;
    for (const count of [0, 1, 2]) {
      context.offered = context.installed.map((app, index) => ({
        source: 0, app: { ...app, version: index < count ? "2.0.0" : app.version },
      }));
      context.drawTiles();
      assert.equal(byId("update-all").hidden, count === 0,
        `Update all visibility with ${count} updates and Auto-update ${automatic}`);
      assert.equal(byId("auto-update-apps").attributes["aria-pressed"], String(automatic));
    }
  }
  context.offered = [
    { source: 0, app: { name: "iss", version: "2.0.0", incompatible: true } },
    { source: 0, app: { name: "weather", version: "2.0.0" } },
  ];
  context.drawTiles();
  assert.equal(byId("update-all").hidden, true, "only compatible installed updates count");
}

async function testAuthors() {
  const messages = [], rendered = [];
  const catalogs = [
    { apps: [{ name: "official", title: "Official", author: "Perinkpheral", version: "1" }] },
    { apps: [{ name: "personal", title: "Personal", author: "Иван", version: "1" }] },
  ];
  const view = { replaceChildren() {}, append() {} };
  const context = vm.createContext({
    TextEncoder, MANIFEST_AUTHOR_MAX: 63, CATALOG_INSTALLED_MAX: 24,
    sources: ["official", "personal"], offered: [], installed: [], removed: [], pageLanguage: "en",
    loadSources: async () => {}, loadGlyphs() {}, drawTiles() {},
    askForApp: async url => catalogs[Number(url.split("=")[1])],
    t: (text, value) => text.replace("{0}", value),
    toast: text => messages.push(text),
    byId: id => id === "catalog-search" ? { value: "" } : view,
    compatibilityProblem: () => "", picture: () => ({}), markPendingInstall() {},
    offerSays: (offer, field) => offer.app[field] || "",
    make: (tag, attributes = {}) => {
      rendered.push({ tag, ...attributes });
      return { append() {}, addEventListener() {} };
    },
  });
  vm.runInContext(apps.slice(apps.indexOf("function catalogAuthorIsAllowed("), apps.indexOf("/* ---- sources ---- */")), context);
  for (const author of [undefined, "Ada", "Иван", "a".repeat(63)])
    assert(context.catalogAuthorIsAllowed(author, 1));
  for (const author of [null, 42, "", " Ada", "Ada ", "Ada\n", "a".repeat(64), "Я".repeat(32)])
    assert(!context.catalogAuthorIsAllowed(author, 1));
  for (const author of ["Perinkpheral", "PERINKPHERAL", "pErInKpHeRaL"]) {
    assert(context.catalogAuthorIsAllowed(author, 0));
    assert(!context.catalogAuthorIsAllowed(author, 1));
  }
  await context.loadCatalogs();
  assert.equal(context.offered.length, 2);
  vm.runInContext(apps.slice(apps.indexOf("function drawCatalog()"), apps.indexOf('byId("catalog-search").addEventListener')), context);
  vm.runInContext(read("compatibility.js"), context);
  context.drawCatalog();
  assert(rendered.some(node => node.tag === "small" && node.textContent === "Perinkpheral" && node.translate === false));
  assert(rendered.some(node => node.tag === "small" && node.textContent === "Иван" && node.translate === false));
  catalogs[1].apps[0].author = "PERINKPHERAL";
  await context.loadCatalogs();
  assert.equal(context.offered.length, 1);
  assert.equal(context.offered[0].app.name, "official");
  assert.deepEqual(messages, ["Source unavailable: personal"]);
  delete catalogs[1].apps[0].author;
  await context.loadCatalogs();
  assert.equal(context.offered.length, 2); // Old catalogs remain installable.
}

(async () => {
  await testVersionOrdering();
  await testCachedCatalogOffers();
  testUpdateAllVisibility();
  await testAutoUpdateSetting();
  await testAuthors();
  const memory = [503, "Not enough memory."];
  const occupied = [503, "Device busy."];
  const interrupted = [502, "Download failed."];
  const success = [200];
  let result = await run([memory, success]);
  assert.equal(result.requests.length, 2);
  assert.equal(result.reloads, 1);
  assert.deepEqual(result.delays, [2000]);
  assert.equal(result.messages[0].text, "Updating ISS…");
  assert(result.messages.some(message => message.text === "Device busy. Retrying"));
  assert(!result.messages.some(message => message.kind === "error"));
  assert.deepEqual(result.requests[0], result.requests[1]);
  assert.equal(result.messages.at(-1).text, "ISS updated to 1.2.3.");

  result = await run([occupied, interrupted, success], "update-all");
  assert.equal(result.requests.length, 4);
  assert(result.requests[2].url.endsWith("app=iss"));
  assert(result.requests[3].url.endsWith("app=clock"));
  assert(result.messages.some(message => message.text === "Download interrupted. Retrying"));
  assert.equal(result.reloads, 2);
  assert.deepEqual(result.messages.filter(message => message.kind === undefined).map(message => message.text),
    ["ISS updated to 1.2.3.", "Clock updated to 2.0.0."]);

  result = await run([memory]);
  assert.equal(result.requests.length, 4);
  assert.deepEqual(result.delays, [2000, 4000, 8000]);
  assert.equal(result.reloads, 0);
  assert.equal(result.messages.at(-1).text, "ISS: Device busy. Try again shortly.");
  assert.equal(result.messages.at(-1).kind, "error");

  for (const refusal of [[502, "Checksum mismatch."], [409, "Not enough storage."],
                         [500, "Storage error."], [403, "Not confirmed."],
                         [503, "Service unavailable."], new Error("Failed to fetch")]) {
    result = await run([refusal]);
    assert.equal(result.requests.length, 1);
    assert.equal(result.delays.length, 0);
    assert.equal(result.reloads, 0);
    assert(!result.messages.some(message => message.text.includes("updated to")));
  }
  result = await run([memory], "save");
  assert.equal(result.requests.length, 1);
  result = await run([success], "install", { incompatible: true });
  assert.equal(result.requests.length, 0);
  result = await run([memory, success], "catalog");
  assert.equal(result.requests.length, 2);
  result = await run([memory], "catalog");
  assert.equal(result.requests.length, 4);
  assert.equal(result.messages.at(-1).text, "Device busy. Try again shortly.");
  result = await run([interrupted]);
  assert.equal(result.requests.length, 4);
  assert.equal(result.messages.at(-1).text, "ISS: Download failed.");
  result = await run([memory], "update-all");
  assert.equal(result.requests.length, 8);
  assert(result.requests.slice(0, 4).every(request => request.url.endsWith("app=iss")));
  assert(result.requests.slice(4).every(request => request.url.endsWith("app=clock")));
  await require("./test_app_install_queue.js")();
  await require("./test_settings_languages.js")();
  console.log("App requests: retry, exhaustion, update all and refusal checks passed.");
})().catch(error => { console.error(error); process.exitCode = 1; });
