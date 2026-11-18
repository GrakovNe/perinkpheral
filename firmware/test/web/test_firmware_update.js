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

async function run(statuses, { dropped = false, rejected = false, payload = image } = {}) {
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
      if (rejected && url === "/api/ota") {
        return { ok: false, status: 400, text: async () => "Not confirmed." };
      }
      if (dropped) throw TypeError("connection disappeared after request was accepted");
      return { ok: true };
    },
  });
  vm.runInContext(workflow, context);
  await context.updateFirmware(payload);
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
