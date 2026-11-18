// SPDX-License-Identifier: MIT
// Actual page requests and install actions, with a scripted device and virtual clock.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const core = read("core.js");
const apps = read("apps.js");
const offer = { source: 0, app: { name: "iss", files: [{ name: "app.wasm" }] } };

async function run(replies, action = "install", options = {}) {
  const requests = [], messages = [], delays = [], listeners = {};
  const classes = new Set();
  const button = { textContent: "Install", style: {}, offsetWidth: 90, disabled: false,
    classList: { add: name => classes.add(name), remove: name => classes.delete(name) },
    addEventListener: (name, handler) => { listeners[name] = handler; } };
  let reloads = 0;
  const context = vm.createContext({
    button, offer, Error, encodeURIComponent,
    t: text => text,
    document: { getElementById: () => button },
    compatibilityProblem: () => options.incompatible ? "Update the firmware." : null,
    confirmed: async () => false,
    offerSays: () => "ISS",
    updates: () => [offer, { source: 0, app: { name: "clock", files: [] } }],
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
  context.toast = (text, kind, stays) => {
    const message = { text, kind, stays, dismissed: false };
    messages.push(message);
    return () => { message.dismissed = true; };
  };
  vm.runInContext(apps.slice(apps.indexOf("async function install("), apps.indexOf("/* An icon is")), context);
  const updateStart = apps.indexOf('byId("update-all").addEventListener');
  vm.runInContext(apps.slice(updateStart, apps.indexOf("async function loadApps()", updateStart)), context);
  vm.runInContext(apps.slice(apps.indexOf("async function loadCatalogs()"), apps.indexOf("/* ---- sources ---- */")), context);
  if (action === "update-all") await listeners.click();
  else if (action === "catalog") await context.loadCatalogs();
  else if (action === "save") await vm.runInContext('send(button, "/api/settings", post({font: "jersey"}))', context);
  else await context.install(button, offer);
  assert.equal(button.disabled, false);
  assert.equal(classes.has("busy"), false);
  assert(messages.filter(message => message.stays).every(message => message.dismissed));
  return { requests, messages, delays, reloads };
}

(async () => {
  const memory = [503, "Not enough memory."];
  const occupied = [503, "Device busy."];
  const interrupted = [502, "Download failed."];
  const success = [200];
  let result = await run([memory, success]);
  assert.equal(result.requests.length, 2);
  assert.equal(result.reloads, 1);
  assert.deepEqual(result.delays, [2000]);
  assert.equal(result.messages[0].text, "Device busy. Retrying");
  assert(!result.messages.some(message => message.kind === "error"));
  assert.deepEqual(result.requests[0], result.requests[1]);

  result = await run([occupied, interrupted, success], "update-all");
  assert.equal(result.requests.length, 4);
  assert(result.requests[2].url.endsWith("app=iss"));
  assert(result.requests[3].url.endsWith("app=clock"));
  assert.equal(result.messages[1].text, "Download interrupted. Retrying");
  assert.equal(result.reloads, 1);

  result = await run([memory]);
  assert.equal(result.requests.length, 4);
  assert.deepEqual(result.delays, [2000, 4000, 8000]);
  assert.equal(result.reloads, 0);
  assert.equal(result.messages.at(-1).text, "Device busy. Try again shortly.");
  assert.equal(result.messages.at(-1).kind, "error");

  for (const refusal of [[502, "Checksum mismatch."], [409, "Not enough storage."],
                         [500, "Storage error."], [403, "Not confirmed."],
                         [503, "Service unavailable."], new Error("Failed to fetch")]) {
    result = await run([refusal]);
    assert.equal(result.requests.length, 1);
    assert.equal(result.delays.length, 0);
    assert.equal(result.reloads, 0);
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
  assert.equal(result.messages.at(-1).text, "Download failed.");
  result = await run([memory], "update-all");
  assert.equal(result.requests.length, 4);
  assert(result.requests.every(request => request.url.endsWith("app=iss")));
  console.log("App requests: retry, exhaustion, update all and refusal checks passed.");
})().catch(error => { console.error(error); process.exitCode = 1; });
