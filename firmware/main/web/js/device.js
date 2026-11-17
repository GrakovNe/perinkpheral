/* SPDX-License-Identifier: MIT
 * The device: what it is and how it is doing, its screen and buttons when the firmware has
 * the development tools, its firmware, its log, a restart and a reset. */
"use strict";

const RELEASES = "https://api.github.com/repos/GrakovNe/perinkpheral/releases/latest";
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

/* The record of the last fault: "6 \"Interrupt wdt timeout o\" wifi pc ...": so many starts ago,
 * why, in which task, and where - the rest for whoever has the firmware's symbols. */
function lastFault(record) {
  const found = (record || "").match(/^(\d+) "([^"]*)" (\S+)/);
  if (!found) return null;
  const [, ago, why, task] = found;
  return t(ago === "1" ? "{0}, {1} - {2} start ago" : "{0}, {1} - {2} starts ago", why.trim(), task, ago);
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
      ["Built", status.built],
      ["ESP-IDF", status.idf],
      ["Slot", status.slot],
      ["Time", status.time],
      ["Uptime", has(status.uptime_s) && duration(status.uptime_s)],
      ["Last start", STARTS[status.reset_reason] || (status.reset_reason && t("Code {0}", status.reset_reason))],
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
      ["Lowest", has(status.least_free_memory) && kb(status.least_free_memory)],
      ["Largest block", has(status.largest_free_block) && kb(status.largest_free_block)],
      ["Errors", faults.join(", ")],
      ["App area", status.app_area && t("{0} of {1}", kb(status.app_area_used), kb(status.app_area))],
      ["Storage", status.storage_ok === false ? "Error"
        : status.storage_total && t("{0} of {1}", kb(status.storage_used), kb(status.storage_total))],
    ]],
  ].map(([name, facts]) => [name, facts.filter(([, value]) => value)]).filter(([, facts]) => facts.length);
}

/* Facts as a list of names and values. */
const factList = facts => facts.flatMap(([name, value]) =>
  [make("dt", { textContent: name }), make("dd", { textContent: value })]);

async function loadStatus() {
  status = await ask("/api/status");
  statusAt = Date.now();
  drawStatus();
  show("nav-name", status.name);
  show("nav-firmware", status.firmware);
  show("ota-installed", status.firmware || "");
  show("ota-slot", status.slot || "");
}

function drawStatus() {
  byId("status").replaceChildren(...statusFacts().map(([name, facts]) => {
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
    await ask("/api/dev/button?button=" + key + "&press=" + (isLong ? "long" : "short"), { method: "POST" });
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

async function updateFirmware(url, options) {
  const dialog = byId("updating");
  const started = Date.now();
  const step = (title, text) => { show("updating-title", title); show("updating-step", text); };
  const clock = setInterval(() => {
    const seconds = Math.floor((Date.now() - started) / 1000);
    show("updating-timer", Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0"));
  }, 250);
  byId("updating-buttons").hidden = true;
  step("Updating", CONFIRM_ON_DEVICE);
  dialog.showModal();
  /* While its firmware is written, the page has nothing else to offer. */
  dialog.addEventListener("cancel", event => event.preventDefault());
  /* The device answers once the image is written: until then it asks, and then fetches. */
  const asking = setTimeout(() => step("Updating", "Installing."), 15000);
  try {
    await ask(url, options);
  } catch (error) {
    /* The device said no ("Not confirmed.", "Invalid image."). One that went quiet may be
       restarting into the image already: it is looked for. */
    if (!(error instanceof TypeError)) {
      clearTimeout(asking);
      clearInterval(clock);
      dialog.close();
      toast(error.message, "error");
      return;
    }
  }
  clearTimeout(asking);
  step("Restarting", "Waiting for the device.");
  const sent = Date.now(), before = status.firmware;
  while (Date.now() - sent < BACK_WITHIN_MS) {
    await new Promise(resolve => setTimeout(resolve, BACK_EVERY_MS));
    let now;
    try { now = await (await fetch("/api/status", { cache: "no-store" })).json(); } catch (error) { continue; }
    /* Back, and started since: another firmware, or up for less time than it was away. */
    if (now.firmware !== before || now.uptime_s * 1000 < Date.now() - sent) {
      step("Restarting", "Back online.");
      clearInterval(clock);
      location.reload();
      return;
    }
  }
  clearInterval(clock);
  step("Restarting", "Device not found.");
  byId("updating-buttons").hidden = false;
}

byId("updating-reload").addEventListener("click", () => location.reload());

byId("custom-firmware").addEventListener("submit", event => {
  event.preventDefault();
  updateFirmware("/api/ota", { method: "POST", body: byId("image").files[0] });
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
  byId("ota-update").hidden = !isNewerOne;
  latestImage = isNewerOne ? image : null;
  if (!isNewerOne) toast("Up to date.");
}));

/* The release found newer by the check above: the device asks on its own screen before it is
 * written, so the page does not ask again. */
let latestImage = null;
byId("ota-update").addEventListener("click", () => {
  if (latestImage) updateFirmware("/api/ota/release", post({ url: latestImage.browser_download_url, size: latestImage.size }));
});

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

/* What a section does only while it is in sight. */
sectionShown.push(name => {
  if (name === "control") lookAtScreen();
});
document.addEventListener("visibilitychange", () => { if (!document.hidden) lookAtScreen(); });
