/* SPDX-License-Identifier: MIT
 * The device: what it is and how it is doing, its screen and buttons when the firmware has
 * the development tools, its firmware, its log, a restart and a reset. */
"use strict";

const RELEASES = "https://api.github.com/repos/Perinkpheral/perinkpheral/releases/latest";
const RELEASE_IMAGE = "perinkpheral.bin";
let status = {}, statusAt = 0; /* statusAt: when it was read, for the log's clock */

function duration(seconds) {
  const days = Math.floor(seconds / 86400), hours = Math.floor(seconds % 86400 / 3600);
  const minutes = Math.floor(seconds % 3600 / 60);
  return days ? t("{0} d {1} h", days, hours) : hours ? t("{0} h {1} min", hours, minutes) : t("{0} min", minutes);
}

const kb = bytes => t("{0} KB", Math.round(bytes / 1024));

/* Why the device started this time: esp_reset_reason_t. */
const STARTS = { 1: "Power on", 2: "Reset pin", 3: "Restarted", 4: "Fault", 5: "Interrupt watchdog",
                 6: "Task watchdog", 7: "Watchdog", 8: "Wake from sleep", 9: "Brownout" };

/* Extract the cause from the diagnostic record; task and addresses remain in the API. */
const FAULTS = { "Interrupt wdt": "Interrupt watchdog", "Task watchdog": "Task watchdog", "abort()": "Abort",
                 "Load access fault": "Memory fault", "Store access fault": "Memory fault",
                 "Illegal instruction": "Illegal instruction", "Breakpoint": "Breakpoint",
                 "Stack protection": "Stack overflow", "Unhandled debug exception": "Stack overflow" };

function lastFault(record) {
  const found = (record || "").match(/^\d+ "([^"]*)"/);
  if (!found) return null;
  const [, said] = found;
  /* The chip's words, cut to the record's room: said as a person names the fault. */
  const why = Object.entries(FAULTS).find(([start]) => said.startsWith(start))?.[1] || said.trim();
  return t(why.trim());
}

/* When the firmware was built ("Nov 17 2026 12:04:05", as the compiler says it), in the page's language. */
function builtOn(text) {
  const when = text && new Date(text.replace(/^(\w+) +(\d+) (\d+) /, "$1 $2, $3 "));
  const hasTime = /\d:\d/.test(text || "");
  return when && !isNaN(when) ? when.toLocaleString(pageLanguage, { day: "numeric", month: "short", year: "numeric",
                                                                    ...(hasTime && { hour: "2-digit", minute: "2-digit" }) }) : text;
}

/* The network's state, as the device names it, in words. */
const WIFI = { online: "Online", connecting: "Connecting", setup: "Setup mode", rescue: "Not connected" };
const wifiState = () => WIFI[status.wifi] || status.wifi;

/* The device's facts as a person reads them, in groups; what it does not say is left out. */
function statusFacts() {
  const language = (settings.languages || []).find(known => known.code === settings.language);
  const faults = [];
  if (status.memory_refusals) faults.push(t("{0} refused", status.memory_refusals));
  if (status.failed_allocations) faults.push(t("{0} failed", status.failed_allocations));
  const has = number => number !== undefined;
  return [
    ["Device", [
      ["Name", status.name],
      ["Firmware", status.firmware && (status.on_probation ? t("{0} (verifying)", status.firmware) : status.firmware)],
      ["Application ABIs", (status.supported_abis || []).join(", ")],
      ["Built", builtOn(status.built)],
      ["ESP-IDF", status.idf],
      ["Time", status.time],
      ["Uptime", has(status.uptime_s) && duration(status.uptime_s)],
      ["Reset reason", STARTS[status.reset_reason] || (status.reset_reason && t("Code {0}", status.reset_reason))],
      ["Last fault", lastFault(status.last_panic)],
      ["Language", language && language.name],
    ]],
    ["Network", [
      ["Wi-Fi", wifiState()],
      ["SSID", status.ssid],
      ["IP address", status.address],
      ["Hostname", status.hostname && status.hostname + ".local"],
    ]],
    ["Memory", [
      ["Free", has(status.free_memory) && kb(status.free_memory)],
      ["Errors", faults.join(", ")],
      ["App area", status.app_area && t("{0} of {1}", kb(status.app_area_used), kb(status.app_area))],
      ["Storage", status.storage_ok === false ? "Error"
        : status.storage_total && t("{0} of {1}", kb(status.storage_used), kb(status.storage_total))],
    ]],
  ].map(([name, facts]) => [name, facts.filter(([, value]) => value)]).filter(([, facts]) => facts.length);
}

/* Facts as a list of names and values. */
/* What the user named - the device, the network - stays as they wrote it, whatever it reads like. */
const NAMED_BY_USER = new Set(["Name", "SSID", "IP address", "Hostname"]);
const factList = facts => facts.flatMap(([name, value]) =>
  [make("dt", { textContent: name }), make("dd", { textContent: value, translate: !NAMED_BY_USER.has(name) })]);

async function loadStatus() {
  try {
    status = await reading("status", () => ask("/api/status"));
  } finally {
    drawStatus();
  }
  statusAt = Date.now();
  drawTiles();
  show("nav-name", status.name);
  show("nav-firmware", status.firmware);
  const release = (status.firmware || "").match(/^(\d+\.\d+\.\d+)(?:-debug)?$/);
  byId("nav-firmware").href = "https://github.com/Perinkpheral/perinkpheral/releases" +
    (release ? "/tag/v" + release[1] : "");
}

function drawStatus() {
  const state = stateOf("status");
  show("ota-installed", state === "ready" ? status.firmware || "" : t(STATE_WORDS[state]));
  if (state !== "ready") sayState(byId("status"), state, loadStatus);
  else byId("status").replaceChildren(...statusFacts().map(([name, facts]) => {
    const group = make("div", { className: "panel" }), list = make("dl");
    list.append(...factList(facts));
    group.append(make("h3", { textContent: name }), list);
    return group;
  }));
  drawNetworkNow();
}

/* ---- the screen and the buttons: the development tools, when the firmware has them ---- */

const LONG_PRESS_MS = 600, SCREEN_EVERY_MS = 1000;
let frame = null, screenTimer = 0;

/* The screen, while its section is in sight, a frame a second: a firmware with the development
 * tools only, as its status says. */
async function lookAtScreen() {
  clearTimeout(screenTimer);
  if (!status.dev_tools || currentSection() !== "control" || document.hidden) return;
  try {
    const answer = await fetch("/api/dev/screen");
    if (answer.ok) {
      const number = answer.headers.get("X-Frame");
      if (number !== frame) {
        frame = number;
        const old = byId("screen-image").src;
        byId("screen-image").src = URL.createObjectURL(await answer.blob());
        if (old) URL.revokeObjectURL(old);
      }
    }
  } catch (error) { /* the device is away for a moment: looked at again */ }
  screenTimer = setTimeout(lookAtScreen, SCREEN_EVERY_MS);
}

async function press(key, isLong) {
  try {
    /* The answer is a word, not JSON. */
    const answer = await fetch("/api/dev/button?button=" + key + "&press=" + (isLong ? "long" : "short"), { method: "POST" });
    if (!answer.ok) throw new Error((await answer.text()).trim() || "HTTP " + answer.status);
  } catch (error) {
    toast(error.message, "error");
  }
  lookAtScreen();
}

/* A press as the device knows it: short, or long when held. From the keyboard
 * a button is pressed short: the browser gives it a click and no pointer. */
for (const button of byId("keys").children) {
  let held = 0, isLong = false;
  button.addEventListener("click", event => { if (event.detail === 0) press(button.dataset.key, false); });
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
  byId("image-install").disabled = !file;
});

/* A firmware on its way: a screen over the page, the time since it was sent, what is going on,
 * and the page read anew once the device is back with it. */
const BACK_EVERY_MS = 2000, BACK_WITHIN_MS = 5 * 60 * 1000;

/* Recovery errors can be plain text. A lost POST response is read back, never resent. */
async function firmwarePost(path, body, timeout) {
  let response;
  try {
    response = await fetch(path, { method: "POST", body, signal: AbortSignal.timeout(timeout) });
  } catch (error) { return; }
  if (!response.ok) {
    let message = "HTTP " + response.status;
    try {
      const text = await response.text();
      try { message = JSON.parse(text).error || text; } catch (error) { message = text; }
    } catch (error) { /* Keep the HTTP status if the error body was interrupted. */ }
    throw Error(message);
  }
}

async function updateFirmware(imageSource, expectedSize = null) {
  const dialog = byId("updating");
  const started = Date.now();
  const step = (title, text) => { show("updating-title", title); show("updating-step", text); };
  const clock = setInterval(() => {
    const seconds = Math.floor((Date.now() - started) / 1000);
    show("updating-timer", Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0"));
  }, 250);
  byId("updating-buttons").hidden = true;
  dialog.showModal();
  const preventCancel = event => event.preventDefault();
  dialog.addEventListener("cancel", preventCancel);
  const pause = () => new Promise(resolve => setTimeout(resolve, BACK_EVERY_MS));
  const readStatus = async () => {
    try {
      const response = await fetch("/api/status", { cache: "no-store", signal: AbortSignal.timeout(4000) });
      return response.ok ? await response.json() : null;
    } catch (error) { return null; }
  };
  try {
    step("Updating", "Downloading firmware.");
    let image = imageSource;
    if (typeof imageSource === "string") {
      const response = await fetch(imageSource, { signal: AbortSignal.timeout(120000) });
      if (!response.ok) throw Error("Firmware download failed. Try again after publication finishes.");
      image = await response.blob();
    }
    if (!image || image.size < 304 || image.size > 2 * 1024 * 1024) throw Error("Invalid image size.");
    if (expectedSize !== null && image.size !== expectedSize) {
      throw Error("Firmware download size does not match the release. Try again.");
    }
    const header = new Uint8Array(await image.slice(0, 304).arrayBuffer());
    const view = new DataView(header.buffer);
    const field = offset => new TextDecoder().decode(header.slice(offset, offset + 32)).split("\0")[0];
    if (header[0] !== 0xe9 || view.getUint32(32, true) !== 0xabcd5432 ||
        field(80) !== "perinkpheral" || view.getUint32(288, true) !== 0x54524150 ||
        view.getUint32(292, true) !== status.partition_table) {
      throw Error("Image is for another partition table. Flash over USB.");
    }
    const expectedVersion = field(48);
    const expectedImage = Array.from(header.slice(176, 208), byte => byte.toString(16).padStart(2, "0")).join("");
    if (!expectedVersion || !header.slice(48, 80).includes(0) || status.ota_protocol !== 1) {
      throw Error("Unsupported firmware update protocol.");
    }
    step("Restarting", "Entering recovery.");
    await firmwarePost("/api/ota/recovery", undefined, 15000);
    let recovery = null;
    const recoveryDeadline = Date.now() + 90000;
    while (Date.now() < recoveryDeadline) {
      await pause();
      const current = await readStatus();
      if (current && current.mode === "recovery") {
        if (current.ota_protocol !== 1 || !Number.isInteger(current.boot_id) ||
            current.partition_table !== view.getUint32(292, true)) {
          throw Error("Unsupported firmware update protocol.");
        }
        recovery = current;
        break;
      }
    }
    if (!recovery) throw Error("Recovery not found. Check the address on the device screen.");
    step("Updating", CONFIRM_ON_DEVICE);
    await firmwarePost("/api/ota", image, 300000);
    step("Restarting", "Checking the new firmware.");
    const deadline = Date.now() + BACK_WITHIN_MS;
    while (Date.now() < deadline) {
      await pause();
      const current = await readStatus();
      if (!current) continue;
      if (current.mode === "recovery") {
        if (current.boot_id !== recovery.boot_id) {
          throw Error("Firmware did not start. Open recovery to try again.");
        }
        if (["idle", "failed"].includes(current.upload_state)) {
          throw Error("Upload stopped in recovery. Choose a file and confirm again.");
        }
        // The same recovery still serves HTTP while its restart screen is being drawn.
      }
      if (current.mode === "application" && current.on_probation === false) {
        if (current.firmware !== expectedVersion || current.image_id !== expectedImage) {
          throw Error("A different firmware started. The uploaded image was not confirmed.");
        }
        location.reload();
        return;
      }
    }
    throw Error("Device not found. Check its screen and reconnect to its Wi-Fi if needed.");
  } catch (error) {
    step("Update stopped", error.message);
    byId("updating-buttons").hidden = false;
  } finally {
    clearInterval(clock);
    dialog.removeEventListener("cancel", preventCancel);
  }
}

byId("updating-reload").addEventListener("click", () => location.reload());

byId("custom-firmware").addEventListener("submit", event => {
  event.preventDefault();
  updateFirmware(byId("image").files[0]);
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
  const isNewerOne = isNewer(releaseOf(version), releaseOf(status.firmware || ""));
  /* The latest, and what it brings when it is newer: the version's section of CHANGES.md,
   * the flasher's line after it. */
  show("ota-latest", isNewerOne ? version : t("{0} - up to date", version));
  byId("ota-latest-row").hidden = false;
  const notes = (release.body || "").split(/\n\s*\nA first flash/)[0].trim();
  show("ota-notes", isNewerOne ? notes : "");
  byId("ota-notes").hidden = !isNewerOne || !notes;
  byId("ota-update").hidden = true;
  latestImage = null;
  if (isNewerOne) {
    const mirror = "https://perinkpheral.github.io/perinkpheral/flash/" + encodeURIComponent(release.tag_name) + "/perinkpheral.bin";
    let mirrored;
    try {
      mirrored = await fetch(mirror, { method: "HEAD", cache: "no-store", signal: AbortSignal.timeout(10000) });
    } catch (error) { throw Error("Could not check the firmware download. Try again."); }
    if (mirrored.status === 404) return toast("Firmware is being published. Check again shortly.");
    if (!mirrored.ok) throw Error("Could not check the firmware download. Try again.");
    /* Content-Length describes the encoded transfer, which Pages may gzip.
     * Check the decoded blob's size before entering recovery instead. */
    latestImage = { url: mirror, size: image.size };
    byId("ota-update").hidden = false;
  }
  if (!isNewerOne) toast("Up to date.");
}));

/* The release found newer by the check above: the device asks on its own screen before it is
 * written, so the page does not ask again. */
let latestImage = null;
byId("ota-update").addEventListener("click", () => {
  if (latestImage) updateFirmware(latestImage.url, latestImage.size);
});

/* ---- restart and reset ---- */

byId("restart").addEventListener("click", async event => {
  if (!await confirmed("Restart device?", "Restart")) return;
  if (await send(event.target, "/api/restart", { method: "POST" })) afterRestart();
});

byId("enter-recovery").addEventListener("click", event => busy(event.currentTarget, async () => {
  await firmwarePost("/api/ota/recovery?manual=true", undefined, 15000);
  const deadline = Date.now() + 90000;
  while (Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, BACK_EVERY_MS));
    let current = null;
    try {
      const response = await fetch("/api/status", { cache: "no-store", signal: AbortSignal.timeout(4000) });
      if (response.ok) current = await response.json();
    } catch (error) { /* The device is restarting. */ }
    if (current && current.mode === "recovery") {
      location.assign("/");
      return;
    }
  }
  throw Error("Recovery not found. Check the address on the device screen.");
}));

byId("reset").addEventListener("click", async event => {
  if (await confirmed("Factory reset? All settings and app data will be erased.", "Factory reset")) {
    toast(CONFIRM_ON_DEVICE);
    send(event.target, "/api/reset", { method: "POST" });
  }
});

/* What a section does only while it is in sight. */
sectionShown.push(name => {
  if (name === "control") lookAtScreen();
});
document.addEventListener("visibilitychange", () => { if (!document.hidden) lookAtScreen(); });
