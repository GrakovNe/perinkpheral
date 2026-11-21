// SPDX-License-Identifier: MIT
// A delayed save must preserve other forms without leaving stale settings open.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const apps = read("apps.js"), core = read("core.js");
const flush = async () => { for (let step = 0; step < 30; step++) await Promise.resolve(); };

async function testDialog() {
  const nodes = new Map(), requests = [], pauses = [];
  let reloads = 0, finishReload;
  const context = vm.createContext({
    document: { getElementById(id) {
      if (!nodes.has(id)) nodes.set(id, { listeners: {}, children: [],
        addEventListener(event, handler) { this.listeners[event] = handler; },
        replaceChildren(...children) { this.children = children; },
        querySelectorAll(selector) {
          return selector.includes("place") ? this.children.filter(input => input.dataset.type === "place") : this.children;
        },
        showModal() { this.open = true; }, close() { this.open = false; },
      });
      return nodes.get(id);
    } },
    t: text => text, show() {}, settingFields: app => app.fields,
    loadApps: () => { reloads++; return new Promise(resolve => { finishReload = resolve; }); },
    setTimeout: callback => pauses.push(callback),
    fetch: (url, options) => new Promise(resolve => requests.push({ url, options,
      reply: ok => resolve({ ok, status: ok ? 200 : 400,
        json: async () => ok ? { ok: "Saved." } : { error: "Invalid value." } }),
    })),
  });
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  context.toast = () => {};
  vm.runInContext(apps.slice(apps.indexOf("let appInDialog"), apps.indexOf("/* ---- the companion")), context);
  vm.runInContext(apps.slice(apps.indexOf('byId("app-settings").addEventListener')), context);
  const dialog = context.document.getElementById("app-dialog");
  const input = value => ({ value, dataset: { key: "text", type: "text" } });
  const first = { name: "first", fields: [input("first edit")] };
  const second = { name: "second", fields: [input("newer edit")] };
  const submit = () => context.document.getElementById("app-settings").listeners.submit({ preventDefault() {},
    submitter: { textContent: "Save", style: {}, offsetWidth: 80, classList: { add() {}, remove() {} } },
  });

  for (const phase of ["request", "success pause", "reload"]) for (const newer of [second, first]) {
    context.openAppSettings(first);
    const saved = submit();
    const request = requests.at(-1);
    assert(request.url.endsWith("app=first"));
    assert.equal(JSON.parse(request.options.body).text, "first edit");
    const reopen = () => { dialog.close(); context.openAppSettings(newer); };
    if (phase === "request") reopen();
    request.reply(true); await flush();
    assert.equal(pauses.length, 1, "the real busy() success delay is still pending");
    if (phase === "success pause") reopen();
    pauses.shift()();
    await flush();
    if (phase === "reload") reopen();
    finishReload();
    await saved;
    assert.equal(dialog.open, newer !== first,
      "a same-app form has stale values; a different app's edits must stay open during " + phase);
    assert.equal(context.document.getElementById("app-fields").children[0].value, newer.fields[0].value);
  }
  context.openAppSettings(first);
  let saved = submit();
  requests.at(-1).reply(true); await flush(); pauses.shift()(); await flush(); finishReload(); await saved;
  assert.equal(dialog.open, false, "a save still closes its own unchanged form");
  context.openAppSettings(first);
  saved = submit(); requests.at(-1).reply(false); await saved;
  assert.equal(dialog.open, true, "a refused save keeps the form open");
  assert.equal(reloads, 7);
  context.loadApps = async () => { throw new Error("Device unreachable."); };
  for (const newer of [first, second]) {
    context.openAppSettings(first);
    saved = submit();
    requests.at(-1).reply(true); await flush();
    dialog.close(); context.openAppSettings(newer);
    pauses.shift()();
    await assert.rejects(saved, /Device unreachable/);
    assert.equal(dialog.open, newer !== first, "a failed reload must not leave the saved app's stale form open");
  }
  console.log("App settings dialog: stale same-app forms close, other edits survive, ordinary saves and refusals passed.");
}
module.exports = testDialog;
if (require.main === module) testDialog().catch(error => { console.error(error); process.exitCode = 1; });
