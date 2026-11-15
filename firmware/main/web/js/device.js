/* SPDX-License-Identifier: MIT
 * The device: what it is and how it is doing, its screen and buttons when the firmware has
 * the development tools, its firmware, its log, a restart and a reset. */
"use strict";

const RELEASES = "https://api.github.com/repos/GrakovNe/perinkpheral/releases/latest";
const RELEASE_IMAGE = "perinkpheral.bin";
let status = {};

function duration(seconds) {
  const days = Math.floor(seconds / 86400), hours = Math.floor(seconds % 86400 / 3600);
  const minutes = Math.floor(seconds % 3600 / 60);
  return days ? days + " d " + hours + " h" : hours ? hours + " h " + minutes + " min" : minutes + " min";
}

const kb = bytes => Math.round(bytes / 1024) + " KB";

/* Why the device started this time: esp_reset_reason_t. */
const STARTS = { 1: "Power on", 2: "Reset pin", 3: "Restart", 4: "Fault", 5: "Interrupt watchdog",
                 6: "Task watchdog", 7: "Watchdog", 8: "Wake from sleep", 9: "Brownout" };

/* The record of the last fault: "6 \"Interrupt wdt timeout o\" wifi pc ...": so many starts ago,
 * why, in which task, and where - the rest for whoever has the firmware's symbols. */
function lastFault(record) {
  const found = (record || "").match(/^(\d+) "([^"]*)" (\S+)/);
  if (!found) return null;
  const [, ago, why, task] = found;
  return why.trim() + ", " + task + " - " + ago + (ago === "1" ? " start ago" : " starts ago");
}

/* The device's facts as a person reads them; what it does not say is left out. */
function statusFacts() {
  const language = (settings.languages || []).find(known => known.code === settings.language);
  const faults = [];
  if (status.memory_refusals) faults.push(status.memory_refusals + " refused");
  if (status.failed_allocations) faults.push(status.failed_allocations + " failed");
  return [
    ["Name", status.name],
    ["Hostname", status.hostname && status.hostname + ".local"],
    ["Firmware", status.firmware && status.firmware + (status.on_probation ? " (verifying)" : "")],
    ["Built", status.built],
    ["ESP-IDF", status.idf],
    ["Slot", status.slot],
    ["Wi-Fi", status.wifi],
    ["SSID", status.ssid],
    ["IP address", status.address],
    ["Time", status.time],
    ["Uptime", status.uptime_s !== undefined && duration(status.uptime_s)],
    ["Last start", STARTS[status.reset_reason] || (status.reset_reason && "Code " + status.reset_reason)],
    ["Last fault", lastFault(status.last_panic)],
    ["Free memory", status.free_memory !== undefined &&
      kb(status.free_memory) + ", " + kb(status.least_free_memory) + " at least, " +
      kb(status.largest_free_block) + " in one piece"],
    ["Memory", faults.join(", ")],
    ["App area", status.app_area && kb(status.app_area_used) + " of " + kb(status.app_area)],
    ["Storage", status.storage_ok === false ? "Error"
      : status.storage_total && kb(status.storage_used) + " of " + kb(status.storage_total)],
    ["Language", language && language.name],
  ].filter(([, value]) => value);
}

async function loadStatus() {
  status = await ask("/api/status");
  drawStatus();
  show("nav-name", status.name);
  show("nav-firmware", status.firmware);
}

function drawStatus() {
  byId("status").replaceChildren(...statusFacts().flatMap(([name, value]) =>
    [make("dt", { textContent: name }), make("dd", { textContent: value })]));
  drawNetworkNow();
}

/* ---- the screen and the buttons: the development tools, when the firmware has them ---- */

const LONG_PRESS_MS = 600, SCREEN_EVERY_MS = 1000;
let frame = null, screenTimer = 0;

async function lookAtScreen() {
  clearTimeout(screenTimer);
  if (currentSection() !== "device" || document.hidden) return;
  try {
    const answer = await fetch("/api/dev/screen");
    /* A firmware for release has no such thing: the block stays away. */
    if (answer.status === 404) return;
    if (answer.ok) {
      byId("screen-block").hidden = false;
      const number = answer.headers.get("X-Frame");
      if (number !== frame) {
        frame = number;
        const old = byId("screen").src;
        byId("screen").src = URL.createObjectURL(await answer.blob());
        if (old) URL.revokeObjectURL(old);
      }
    }
  } catch (error) { /* the device is away for a moment: looked at again */ }
  screenTimer = setTimeout(lookAtScreen, SCREEN_EVERY_MS);
}

async function press(key, isLong) {
  try {
    await ask("/api/dev/button?button=" + key + "&press=" + (isLong ? "long" : "short"), { method: "POST" });
  } catch (error) {
    toast(error.message, "error");
  }
  lookAtScreen();
}

/* A press as the device knows it: short, or long when held. */
for (const button of byId("keys").children) {
  let held = 0, isLong = false;
  button.addEventListener("pointerdown", () => {
    isLong = false;
    held = setTimeout(() => { isLong = true; button.classList.add("held"); }, LONG_PRESS_MS);
  });
  const letGo = () => { clearTimeout(held); button.classList.remove("held"); };
  button.addEventListener("pointerup", () => { letGo(); press(button.dataset.key, isLong); });
  button.addEventListener("pointerleave", letGo);
}

/* ---- firmware ---- */

byId("image").addEventListener("change", () => {
  const file = byId("image").files[0];
  show("image-name", file ? file.name + ", " + kb(file.size) : "No file chosen");
});

byId("firmware").addEventListener("submit", async event => {
  event.preventDefault();
  toast(CONFIRM_ON_DEVICE);
  if (await send(event.submitter, "/api/ota", { method: "POST", body: byId("image").files[0] }))
    afterRestart();
});

/* "0.1.0" and "0.1.0-da983bb": the version a build says of itself is the release's, and its commit. */
const releaseOf = version => version.split("-")[0].split(".").map(Number);
const isNewer = (a, b) => {
  for (let i = 0; i < 3; i++) if ((a[i] || 0) !== (b[i] || 0)) return (a[i] || 0) > (b[i] || 0);
  return false;
};

byId("check-release").addEventListener("click", event => busy(event.target, async () => {
  let answer;
  try { answer = await fetch(RELEASES); } catch (error) { throw new Error("GitHub unreachable."); }
  const release = answer.ok ? await answer.json() : null;
  /* A release is tagged "v0.1.0" and says "0.1.0" of itself. */
  const version = release ? release.tag_name.replace(/^v/, "") : "";
  const image = release ? release.assets.find(asset => asset.name === RELEASE_IMAGE) : null;
  if (!release || !image) return toast("No releases.");
  if (!isNewer(releaseOf(version), releaseOf(status.firmware || ""))) return toast("Up to date.");
  if (!await confirmed("Update to " + version + "?", "Update")) return;
  toast(CONFIRM_ON_DEVICE);
  if (await send(event.target, "/api/ota/release", post({ url: image.browser_download_url, size: image.size })))
    afterRestart();
}));

/* ---- the log: what was written since the last look; after a restart, the new log whole ---- */

const LOG_EVERY_MS = 2000, LOG_KEPT_CHARS = 64 * 1024;
let logEnd = null, logTimer = 0;

async function readLog() {
  const answer = await fetch("/api/log" + (logEnd === null ? "" : "?since=" + logEnd));
  if (!answer.ok) throw new Error("HTTP " + answer.status);
  const end = Number(answer.headers.get("X-Log-End"));
  const text = await answer.text();
  const log = byId("log");
  /* A position behind the one before: the device started anew, and its log with it. */
  if (logEnd !== null && end < logEnd) {
    logEnd = null;
    log.append("--- restart ---\n");
    return readLog();
  }
  const atEnd = log.scrollTop + log.clientHeight >= log.scrollHeight - 8;
  if (logEnd === null && !log.textContent.endsWith("--- restart ---\n")) log.textContent = "";
  log.append(text);
  if (log.textContent.length > LOG_KEPT_CHARS) log.textContent = log.textContent.slice(-LOG_KEPT_CHARS);
  logEnd = end;
  if (atEnd || logEnd === null) log.scrollTop = log.scrollHeight;
}

async function followLog() {
  clearTimeout(logTimer);
  if (!byId("follow-log").checked || currentSection() !== "device") return;
  try { await readLog(); } catch (error) { /* away for a moment: asked again */ }
  logTimer = setTimeout(followLog, LOG_EVERY_MS);
}

byId("load-log").addEventListener("click", event => busy(event.target, async () => {
  logEnd = null;
  byId("log").textContent = "";
  await readLog();
}));
byId("follow-log").addEventListener("change", followLog);

/* ---- restart and reset ---- */

byId("restart").addEventListener("click", async event => {
  if (await send(event.target, "/api/restart", { method: "POST" })) afterRestart();
});

byId("reset").addEventListener("click", async event => {
  if (await confirmed("Factory reset? All settings and app data will be erased.", "Factory reset")) {
    toast(CONFIRM_ON_DEVICE);
    send(event.target, "/api/reset", { method: "POST" });
  }
});

/* What the device section does only while it is in sight. */
sectionShown.push(name => {
  if (name !== "device") return;
  lookAtScreen();
  followLog();
});
document.addEventListener("visibilitychange", () => { if (!document.hidden) { lookAtScreen(); followLog(); } });
