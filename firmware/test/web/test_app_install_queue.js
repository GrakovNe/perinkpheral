// SPDX-License-Identifier: MIT
// Real queue, request/retry and button code with deferred HTTP responses.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const apps = read("apps.js"), core = read("core.js");
const flush = async () => { for (let step = 0; step < 30; step++) await Promise.resolve(); };
function harness() {
  const requests = [], messages = [], events = {}, buttons = new Map();
  let active = 0, maximumActive = 0, reloads = 0, confirms = true;
  function button(name) {
    if (!buttons.has(name)) buttons.set(name, {
      textContent: name === "all" ? "Update all" : "Update", disabled: false, style: {}, offsetWidth: 80,
      classList: { add() {}, remove() {} }, listeners: {},
      addEventListener(event, callback) { this.listeners[event] = callback; },
    });
    return buttons.get(name);
  }
  const offers = ["air", "clock", "weather"].map(name => ({ source: 0, generation: 0,
    app: { name, title: name, version: "1.2.0", files: [] } }));
  const context = vm.createContext({
    offered: offers, catalogsRead: true,
    installed: offers.map(offer => ({ name: offer.app.name, version: "1.0.0" })),
    document: { getElementById: id => button(id === "update-all" ? "all" : id) },
    window: { addEventListener: (event, callback) => { events[event] = callback; } },
    t: (text, ...values) => text.replace(/\{(\d+)\}/g, (_, index) => values[index]),
    compatibilityProblem: () => "", offerSays: offer => offer.app.title,
    confirmed: async () => confirms,
    updates: () => offers,
    drawTiles() {}, loadApps: async () => { reloads++; },
    setTimeout: callback => callback(),
    fetch: (url, options) => new Promise((resolve, reject) => {
      active++; maximumActive = Math.max(active, maximumActive);
      requests.push({ url, options,
        reply(status = 200, message = "Saved.") {
          active--; resolve({ ok: status === 200, status,
            json: async () => status === 200 ? { ok: message } : { error: message } });
        },
        lose() { active--; reject(new Error("Failed to fetch")); },
      });
    }),
  });
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  vm.runInContext(read("compatibility.js"), context);
  context.toast = (text, kind, stays) => {
    const message = { text, kind, stays, dismissed: false }; messages.push(message);
    return () => { message.dismissed = true; };
  };
  vm.runInContext(apps.slice(apps.indexOf("const appInstallQueue"), apps.indexOf("/* An icon is")), context);
  vm.runInContext(apps.slice(apps.indexOf("let offeredSources"), apps.indexOf("/* One reading at a time")), context);
  Object.assign(context, { catalogAddress: line => line, catalogsReading: null, loadCatalogs: async () => {} });
  const sourcesStart = apps.indexOf('byId("sources").addEventListener');
  vm.runInContext(apps.slice(sourcesStart, apps.indexOf('byId("refresh-catalogs").addEventListener', sourcesStart)), context);
  button("source-rows").values = () => ["https://example.test/b"];
  const bulk = apps.indexOf('byId("update-all").addEventListener');
  vm.runInContext(apps.slice(bulk, apps.indexOf('byId("auto-update-apps").addEventListener', bulk)), context);
  return { context, requests, messages, events, offers, button,
    start: index => context.install(button(String(index)), offers[index]),
    all: () => button("all").listeners.click(),
    get maximumActive() { return maximumActive; }, get reloads() { return reloads; },
    refusePermission() { confirms = false; },
    saveSources: () => button("sources").listeners.submit({ preventDefault() {}, submitter: button("save") }),
  };
}

async function testQueue() {
  let test = harness();
  const first = test.start(0);
  assert.equal(test.requests.length, 1);
  assert.equal(test.messages[0].text, "Updating air…", "start notice precedes the HTTP response");
  const second = test.start(1);
  assert.equal(test.messages.at(-1).text, "clock queued.");
  assert.equal(test.button("1").disabled, true);
  assert.equal(test.button("1").textContent, "Queued");
  assert.strictEqual(test.start(1), second, "duplicate queued clicks share completion");
  assert.strictEqual(test.start(0), first, "duplicate active clicks do not reinstall");
  const batch = test.all();
  assert.equal(test.requests.length, 1, "bulk joins the active individual queue");
  const redrawn = { disabled: false };
  test.context.markPendingInstall(redrawn, test.offers[1]);
  assert.equal(redrawn.disabled, true); assert.equal(redrawn.textContent, "Queued");
  test.requests[0].reply(); await flush();
  assert.equal(test.requests.length, 2);
  assert(test.requests[1].url.endsWith("app=clock"));
  test.requests[1].reply(403, "Not confirmed."); await flush();
  assert.equal(test.requests.length, 3, "one refused update does not stop the batch");
  assert(test.requests[2].url.endsWith("app=weather"));
  test.requests[2].reply();
  assert.deepEqual(await Promise.all([first, second]), [true, false]);
  await batch;
  assert.equal(test.maximumActive, 1);
  assert.equal(test.reloads, 2);
  assert.equal(test.button("all").disabled, false);
  assert(test.messages.some(message => message.text === "clock: Not confirmed." && message.kind === "error"));
  assert.deepEqual(test.messages.filter(message => message.text.includes("updated to")).map(message => message.text),
    ["air updated to 1.2.0.", "weather updated to 1.2.0."]);
  assert(test.messages.filter(message => message.stays).every(message => message.dismissed));

  test = harness();
  const bulkFirst = test.all();
  const overlap = test.start(2);
  for (let index = 0; index < 3; index++) { test.requests[index].reply(); await flush(); }
  await bulkFirst; assert.equal(await overlap, true);
  assert.equal(test.requests.length, 3); assert.equal(test.maximumActive, 1);

  test = harness();
  const uncertain = test.start(0), following = test.start(1);
  test.requests[0].lose(); await flush();
  assert.equal(test.requests.length, 2);
  assert(test.requests[1].url.endsWith("app=clock"), "lost response is not replayed");
  test.requests[1].reply();
  assert.equal(await uncertain, false); assert.equal(await following, true);
  assert(!test.messages.some(message => message.text === "air updated to 1.2.0."));

  test = harness();
  test.offers[0].app.system = true;
  test.refusePermission();
  const refused = test.start(0), permitted = test.start(1);
  await flush();
  assert.equal(await refused, false); assert.equal(test.requests.length, 1);
  assert(test.requests[0].url.endsWith("app=clock"));
  test.requests[0].reply(); await permitted;

  test = harness();
  const inFlight = test.start(0), abandoned = test.start(1);
  const leaving = { preventDefault() { this.prevented = true; } };
  test.events.beforeunload(leaving);
  assert(leaving.prevented && leaving.returnValue === "");
  test.events.pagehide();
  assert.equal(await abandoned, false);
  test.requests[0].reply(); await inFlight;
  assert.equal(test.requests.length, 1, "navigation drops only unsent work");
  assert.equal(test.reloads, 0, "hidden page does not start follow-up requests");
  assert.equal(await test.start(2), false);
  test.events.pageshow({ persisted: true }); await flush();
  assert.equal(test.reloads, 1, "BFCache return refreshes data without replaying POST");
  assert.equal(test.requests.length, 1);
  const resumed = test.start(2);
  assert.equal(test.requests.length, 2, "BFCache return accepts a new user action");
  test.requests[1].reply(); assert.equal(await resumed, true);
  const idle = { preventDefault() { throw new Error("idle page should not warn"); } };
  test.events.beforeunload(idle);
  test = harness();
  const active = test.start(0), staleQueued = test.start(1);
  test.context.invalidateCatalogs();
  assert.equal(await staleQueued, false, "a source change cancels unsent installs");
  assert.equal(await test.start(2), false, "an old tile cannot enqueue another install");
  assert.equal(test.button("1").disabled, false);
  test.requests[0].reply(); await active;
  assert.equal(test.requests.length, 1);

  test = harness();
  let consent;
  test.context.confirmed = () => new Promise(resolve => { consent = resolve; });
  test.offers[0].app.system = true;
  const waiting = test.start(0);
  test.context.invalidateCatalogs();
  consent(true);
  assert.equal(await waiting, false, "permission for an old offer cannot start an install");
  assert.equal(test.requests.length, 0);
  test = harness();
  const installing = test.start(0), queued = test.start(1);
  const saving = test.saveSources();
  await flush();
  const waitingNotice = test.messages.find(message => message.text === "Waiting for app installation.");
  assert(waitingNotice && waitingNotice.stays && waitingNotice.kind.includes("passive"));
  assert.equal(waitingNotice.dismissed, false);
  assert.equal(await queued, false);
  assert.equal(test.requests.length, 1, "source indices stay in place while a sent install may retry");
  test.requests[0].reply(503, "Device busy."); await flush();
  assert.equal(waitingNotice.dismissed, false, "the explanation stays during install retries");
  assert.equal(test.requests.length, 2);
  assert(test.requests[1].url.startsWith("/api/apps/install"));
  test.requests[1].reply(); await installing; await flush();
  assert.equal(test.requests.length, 3);
  assert.equal(test.requests[2].url, "/api/sources", "sources change only after the in-flight install finishes");
  assert.equal(waitingNotice.dismissed, true, "the explanation ends before the sources POST");
  test.requests[2].reply(); await saving;
  assert.equal(await test.start(1), false);
  test = harness();
  const idleSave = test.saveSources(); await flush();
  assert(!test.messages.some(message => message.text === "Waiting for app installation."), "idle saves do not claim to wait");
  test.requests[0].reply(); await idleSave;
  console.log("App install queue: sequential individual/bulk, dedupe, errors, permission cancellation and BFCache passed.");
}
module.exports = testQueue;
if (require.main === module) testQueue().catch(error => { console.error(error); process.exitCode = 1; });
