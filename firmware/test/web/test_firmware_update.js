// SPDX-License-Identifier: MIT
// Run with node: the actual page workflow, with a scripted device and a virtual clock.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../../main/web/js/device.js"), "utf8");
const workflow = source.slice(source.indexOf("/* ---- firmware ---- */"), source.indexOf('byId("updating-reload")'));
const layout = 0xf1234567;
const header = new Uint8Array(512);
const view = new DataView(header.buffer);
header[0] = 0xe9;
view.setUint32(32, 0xabcd5432, true);
header.set(new TextEncoder().encode("0.1.34"), 48);
header.set(new TextEncoder().encode("perinkpheral"), 80);
view.setUint32(288, 0x54524150, true);
view.setUint32(292, layout, true);
header.fill(0x12, 176, 208);
const image = new Blob([header]);
const main = (probation = false, id = "12".repeat(32)) => ({
  mode: "application", ota_protocol: 1, partition_table: layout,
  firmware: "0.1.34", image_id: id, on_probation: probation,
});
const recovery = (boot = 7, state = "idle") => ({
  mode: "recovery", ota_protocol: 1, partition_table: layout, boot_id: boot, upload_state: state,
});

async function run(statuses, { dropped = false, rejected = false, payload = image, expectedSize = null } = {}) {
  let clock = 0, reads = 0, reloads = 0;
  const requests = [], shown = [];
  const element = { addEventListener() {}, removeEventListener() {}, showModal() {} };
  const context = vm.createContext({
    Uint8Array, DataView, TextDecoder, Blob, Array, Number, Error,
    Date: { now: () => clock },
    setInterval: () => 1, clearInterval() {},
    setTimeout: (callback, delay) => { clock += delay; callback(); },
    AbortSignal: { timeout: () => undefined },
    byId: () => element,
    show: (id, text) => shown.push([id, text]),
    location: { reload: () => { reloads++; } },
    CONFIRM_ON_DEVICE: "Confirm on device.",
    status: main(),
    fetch: async (url, options) => {
      if (url === "/api/status") {
        const current = statuses[Math.min(reads++, statuses.length - 1)];
        return { ok: true, json: async () => current };
      }
      requests.push(url);
      if (url.startsWith("https://")) return { ok: true, blob: async () => image };
      if (rejected && url === "/api/ota") {
        return { ok: false, status: 400, text: async () => "Not confirmed." };
      }
      if (dropped) throw TypeError("connection disappeared after request was accepted");
      return { ok: true };
    },
  });
  vm.runInContext(workflow, context);
  await context.updateFirmware(payload, expectedSize);
  return { requests, shown, reloads, reads };
}

(async () => {
  let result = await run([recovery(), recovery(7, "restarting"), main(true), main()], { dropped: true });
  assert.equal(result.reloads, 1);
  assert.deepEqual(result.requests, ["/api/ota/recovery", "/api/ota"]);
  assert.equal(result.reads, 4, "wait for probation, do not reject old recovery while it restarts");
  result = await run([recovery(), recovery(8)]);
  assert.equal(result.reloads, 0);
  assert.ok(result.shown.some(([, text]) => text.includes("did not start")));
  result = await run([recovery(), main(false, "34".repeat(32))]);
  assert.equal(result.reloads, 0);
  assert.ok(result.shown.some(([, text]) => text.includes("different firmware")));
  result = await run([recovery()], { rejected: true });
  assert.ok(result.shown.some(([, text]) => text === "Not confirmed."), "plain recovery errors survive parsing");
  result = await run([recovery()], { payload: new Blob([new Uint8Array(512)]) });
  assert.deepEqual(result.requests, [], "invalid file must not restart the device");
  result = await run([recovery(), recovery(7, "failed")], { dropped: true });
  assert.equal(result.reloads, 0);
  assert.ok(result.shown.some(([, text]) => text.includes("Upload stopped")));
  result = await run([recovery(), main()], { payload: "https://mirror/image.bin", expectedSize: image.size });
  assert.equal(result.reloads, 1, "decoded download size matches the release");
  result = await run([recovery(), main()], { payload: "https://mirror/image.bin", expectedSize: image.size + 1 });
  assert.deepEqual(result.requests, ["https://mirror/image.bin"], "size mismatch must not restart main");
  assert.ok(result.shown.some(([, text]) => text.includes("size does not match")));
  console.log("Browser firmware checks passed: lost responses, stale recovery, probation, rollback, image identity and rejected uploads.");
})().catch(error => { console.error(error); process.exitCode = 1; });

const recoveryHeader = fs.readFileSync(path.join(__dirname, "../../../recovery/main/page.h"), "utf8");
const recoveryHtml = recoveryHeader.split("\n").filter(line => line.trim().startsWith('"'))
  .map(line => JSON.parse(line.trim().replace(/;$/, ""))).join("");
const recoveryScript = recoveryHtml.match(/<script>([\s\S]*)<\/script>/)[1];
async function runEmbedded(statuses, { dropped = false, rejected = false, payload = image } = {}) {
  let clock = 0, reads = 0, reloads = 0, uploads = 0;
  const elements = { upload: {}, image: { files: [payload] }, send: { disabled: false }, result: {} };
  const context = vm.createContext({
    Uint8Array, DataView, TextDecoder, Blob, Array, Number, Error,
    Date: { now: () => clock },
    setTimeout: (callback, delay) => { clock += delay; callback(); },
    AbortSignal: { timeout: () => undefined },
    document: { getElementById: id => elements[id] },
    location: { reload: () => { reloads++; } },
    fetch: async url => {
      if (url === "/api/status") {
        const current = statuses[Math.min(reads++, statuses.length - 1)];
        return { ok: true, json: async () => current };
      }
      assert.equal(url, "/api/ota");
      uploads++;
      if (rejected) return { ok: false, status: 400, text: async () => "Not confirmed." };
      if (dropped) throw TypeError("connection disappeared after upload was accepted");
      return { ok: true };
    },
  });
  vm.runInContext(recoveryScript, context);
  await elements.upload.onsubmit({ preventDefault() {} });
  assert.equal(elements.send.disabled, false, "retry remains available");
  return { uploads, reloads, reads, message: elements.result.textContent };
}

(async () => {
  let result = await runEmbedded([recovery(), recovery(7, "restarting"), main(true), main()], { dropped: true });
  assert.equal(result.reloads, 1);
  assert.equal(result.uploads, 1, "ambiguous upload must not be resent");
  assert.equal(result.reads, 4, "old recovery is not rollback; wait for probation");
  result = await runEmbedded([recovery(), recovery(8)]);
  assert.equal(result.reloads, 0);
  assert.match(result.message, /did not start/);
  result = await runEmbedded([recovery(), main(false, "34".repeat(32))]);
  assert.equal(result.reloads, 0);
  assert.match(result.message, /different firmware/);
  result = await runEmbedded([recovery()], { rejected: true });
  assert.equal(result.message, "Not confirmed.");
  result = await runEmbedded([recovery()], { payload: new Blob([new Uint8Array(512)]) });
  assert.equal(result.uploads, 0);
  result = await runEmbedded([recovery(), recovery(7, "failed")], { dropped: true });
  assert.equal(result.reloads, 0);
  assert.match(result.message, /Upload stopped/);
  result = await runEmbedded([main()]);
  assert.equal(result.uploads, 0);
  assert.match(result.message, /no longer active/);
  console.log("Embedded recovery page checks passed: header preflight, lost reply, boot identity, exact image and retry.");
})().catch(error => { console.error(error); process.exitCode = 1; });

const releaseWorkflow = source.slice(source.indexOf('/* "0.1.0"'), source.indexOf("/* ---- restart and reset ---- */"));
async function checkRelease({ length = "512", code = 200, unavailable = false, installed = "0.1.34" } = {}) {
  const elements = new Map(), messages = [], requests = [], updates = [];
  const byId = id => {
    if (!elements.has(id)) elements.set(id, {
      hidden: false, listeners: {}, addEventListener(event, handler) { this.listeners[event] = handler; },
    });
    return elements.get(id);
  };
  const context = vm.createContext({
    Number, Error, encodeURIComponent, AbortSignal: { timeout: () => undefined },
    RELEASES: "https://api.github.com/releases/latest", RELEASE_IMAGE: "perinkpheral.bin",
    status: { firmware: installed }, byId, show() {}, t: text => text,
    toast: message => messages.push(message), busy: (button, action) => action(),
    updateFirmware: (...args) => updates.push(args),
    fetch: async (url, options) => {
      requests.push(url);
      if (url === "https://api.github.com/releases/latest") return {
        ok: true, json: async () => ({ tag_name: "v0.1.35", assets: [{ name: "perinkpheral.bin", size: image.size }] }),
      };
      assert.equal(options.method, "HEAD");
      assert.equal(options.cache, "no-store");
      if (unavailable) throw TypeError("network unavailable");
      return { ok: code === 200, status: code, headers: { get: () => length } };
    },
  });
  vm.runInContext(releaseWorkflow, context);
  await byId("check-release").listeners.click({ target: {} });
  byId("ota-update").listeners.click();
  return { hidden: byId("ota-update").hidden, messages, requests, updates };
}

(async () => {
  for (const length of ["512", "123", null]) {
    const result = await checkRelease({ length });
    assert.equal(result.hidden, false, "compressed or absent transfer length does not hide a published image");
    assert.equal(result.messages.length, 0);
    assert.deepEqual(result.updates, [["https://perinkpheral.github.io/perinkpheral/flash/v0.1.35/perinkpheral.bin", image.size]]);
  }
  let result = await checkRelease({ code: 404 });
  assert.equal(result.hidden, true);
  assert.equal(result.updates.length, 0);
  assert.match(result.messages[0], /being published/);
  await assert.rejects(checkRelease({ code: 503 }), /Could not check/);
  await assert.rejects(checkRelease({ unavailable: true }), /Could not check/);
  result = await checkRelease({ installed: "0.1.35" });
  assert.equal(result.requests.length, 1, "up-to-date devices do not probe the mirror");
  assert.equal(result.hidden, true);
  console.log("Release availability checks passed: compressed/missing lengths, absent files, network errors and decoded size handoff.");
})().catch(error => { console.error(error); process.exitCode = 1; });

const recoveryButton = source.slice(source.indexOf('byId("enter-recovery")'), source.indexOf('byId("reset")'));
async function enterRecovery(statuses, { dropped = false, rejected = false } = {}) {
  let clock = 0, reads = 0;
  const requests = [], redirects = [], errors = [], handlers = {};
  const button = { addEventListener(event, handler) { handlers[event] = handler; } };
  const context = vm.createContext({
    Error, Date: { now: () => clock },
    setTimeout: (callback, delay) => { clock += delay; callback(); },
    AbortSignal: { timeout: () => undefined },
    byId: () => button, location: { assign: url => redirects.push(url) },
    busy: async (target, action) => {
      assert.equal(target, button);
      try { await action(); } catch (error) { errors.push(error.message); }
    },
    fetch: async (url, options) => {
      if (url === "/api/status") {
        const current = statuses[Math.min(reads++, statuses.length - 1)];
        if (!current) throw TypeError("device is restarting");
        return { ok: true, json: async () => current };
      }
      requests.push([url, options.method]);
      if (rejected) return { ok: false, status: 400, text: async () => '{"error":"Previous update is being verified."}' };
      if (dropped) throw TypeError("reply lost during restart");
      return { ok: true };
    },
  });
  vm.runInContext(workflow + recoveryButton, context);
  await handlers.click({ currentTarget: button });
  return { requests, redirects, errors, reads };
}

(async () => {
  for (const dropped of [false, true]) {
    const result = await enterRecovery([main(), null, recovery()], { dropped });
    assert.deepEqual(result.requests, [["/api/ota/recovery?manual=true", "POST"]]);
    assert.deepEqual(result.redirects, ["/"]);
    assert.equal(result.reads, 3, "open recovery only once its server is ready");
    assert.deepEqual(result.errors, []);
  }
  let result = await enterRecovery([main()], { rejected: true });
  assert.deepEqual(result.redirects, []);
  assert.equal(result.reads, 0, "a refused request must not wait for a restart");
  assert.deepEqual(result.errors, ["Previous update is being verified."]);
  result = await enterRecovery([main()]);
  assert.deepEqual(result.redirects, []);
  assert.deepEqual(result.errors, ["Recovery not found. Check the address on the device screen."]);
  assert.ok(result.reads <= 45, "waiting for recovery is bounded");
  console.log("Manual recovery checks passed: delayed startup, lost reply, refusal, timeout and page navigation.");
})().catch(error => { console.error(error); process.exitCode = 1; });
