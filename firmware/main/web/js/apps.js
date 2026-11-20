/* SPDX-License-Identifier: MIT
 * Applications: what the device has, their settings and what each is; what the catalogs
 * have; the catalogs themselves. */
"use strict";

let installed = [], sources = [], offered = [], removed = [];

/* Photon: a search of OpenStreetMap made for typing, in any language the name has. */
const PLACES_URL = "https://photon.komoot.io/api/?limit=8&lang=en&layer=city&q=";
const PLACES_OFFERED = 6;
const PLACE_TYPING_PAUSE_MS = 250;

/* The device keeps ASCII: "Sao Paulo" for what the service calls "São Paulo". A name
   that is not Latin letters under its accents has no such form. */
function ascii(text) {
  const apart = text.normalize("NFD");
  return /^[ -~\u0300-\u036f]*$/.test(apart) ? apart.replace(/[^ -~]/g, "") : "";
}

/* A place is typed by its name and chosen from what the name finds; the choice fills
   the two hidden numbers its setting names. A name that was typed and not chosen is
   not a place: the form says so when it is saved. */
function placeField(input, latitude, longitude) {
  const list = make("ul"), box = make("div", { className: "place" });
  let found = [], current = -1, pause = 0, asked = 0;
  input.dataset.chosen = input.value;
  const offer = places => {
    found = places;
    current = -1;
    list.replaceChildren(...places.map((place, index) => {
      const row = make("li", { textContent: place.name });
      row.addEventListener("mousedown", event => { event.preventDefault(); choose(index); });
      return row;
    }));
  };
  const choose = index => {
    input.value = input.dataset.chosen = found[index].name;
    latitude.value = found[index].latitude;
    longitude.value = found[index].longitude;
    offer([]);
  };
  const point = index => {
    current = (index + found.length) % found.length;
    [...list.children].forEach((row, at) => row.classList.toggle("current", at === current));
  };
  input.addEventListener("input", () => {
    clearTimeout(pause);
    const name = input.value.trim(), question = ++asked;
    if (!name) {
      input.dataset.chosen = latitude.value = longitude.value = "";
      return offer([]);
    }
    pause = setTimeout(async () => {
      try {
        const answer = await (await fetch(PLACES_URL + encodeURIComponent(name))).json();
        if (question !== asked) return;
        offer((answer.features || []).map(({ properties: place, geometry }) => ({
          name: [place.name, place.state, place.country].map(part => ascii(part || "")),
          longitude: geometry.coordinates[0].toFixed(4), latitude: geometry.coordinates[1].toFixed(4) }))
          .filter(place => place.name[0])
          /* "Saint Petersburg, Saint Petersburg, Russia": a region named as its city is said once. */
          .map(place => ({ ...place, name: place.name.filter((part, at, all) => part && all.indexOf(part) === at)
                                                     .join(", ").slice(0, input.maxLength) }))
          .filter((place, index, all) => all.findIndex(other => other.name === place.name) === index)
          .slice(0, PLACES_OFFERED));
      } catch (error) {
        offer([]);
      }
    }, PLACE_TYPING_PAUSE_MS);
  });
  input.addEventListener("keydown", event => {
    if (!found.length) return;
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      point(current + (event.key === "ArrowDown" ? 1 : -1));
    } else if (event.key === "Enter") {
      event.preventDefault();
      choose(current < 0 ? 0 : current);
    } else if (event.key === "Escape") {
      event.stopPropagation();
      event.preventDefault();
      offer([]);
    }
  });
  input.addEventListener("blur", () => offer([]));
  box.append(input, list);
  return box;
}

/* The fields of an application's settings, as its manifest describes them. */
function settingFields(app) {
  const filled = new Set(app.settings.filter(setting => setting.type === "place")
    .flatMap(place => [place.latitude, place.longitude]));
  const hidden = {}, parts = [];
  for (const setting of app.settings) {
    if (!filled.has(setting.key)) continue;
    hidden[setting.key] = make("input", { type: "hidden", value: app.values[setting.key] });
    hidden[setting.key].dataset.key = setting.key;
    hidden[setting.key].dataset.type = setting.type;
    parts.push(hidden[setting.key]);
  }
  for (const setting of app.settings) {
    /* "hidden": set by something else - an integration, a companion page - and not typed here. */
    if (filled.has(setting.key) || setting.hidden) continue;
    const field = settingField(app, setting);
    if (setting.type === "place")
      field[1] = placeField(field[1], hidden[setting.latitude], hidden[setting.longitude]);
    parts.push(...field);
  }
  return parts;
}

/* A list setting: a line an item, stored as the items with a line break between them. */

function listField(value) {
  const box = editableList(make("div", { className: "rows" }),
                           { values: value ? value.split("\n") : [], input: { maxLength: MANIFEST_LIST_ITEM_MAX } });
  box.setValue = text => box.setValues(text ? text.split("\n") : []);
  box.listValue = () => box.values().join("\n");
  return box;
}

/* One field of an application's settings, as its manifest describes it. */
function settingField(app, setting) {
  const preset = Array.isArray(setting.default) ? setting.default.join("\n") : setting.default;
  const id = "setting-" + setting.key;
  const value = setting.type === "link" && preset ? (app.values[setting.key] ?? preset) :
    (app.values[setting.key] || preset || "");
  const label = make("label", { htmlFor: id, textContent: setting.title });
  let input;
  if (setting.type === "list") {
    input = listField(value);
  } else if (setting.type === "choice") {
    input = make("select");
    /* An option is what is stored; its label, when the device gives one, what is read. */
    /* With no translation an option is its own label: "celsius" reads "Celsius". */
    const labels = setting.labels || [];
    const label = (option, at) => labels[at] || option.charAt(0).toUpperCase() + option.slice(1);
    input.append(make("option", { value: "", textContent: "-" }));
    setting.options.forEach((option, at) =>
      input.append(make("option", { value: option, textContent: label(option, at) })));
    input.value = value;
  } else {
    input = make("input", { maxLength: MANIFEST_SETTING_VALUE_MAX, autocomplete: "off" });
    if (setting.type === "toggle") {
      input.type = "checkbox";
      input.checked = value === "1";
      label.className = "check";
      label.prepend(input);
    } else if (setting.type === "secret" || setting.type === "link") {
      /* The device does not give a secret back: an empty field keeps the one it has.
       * A link - a calendar's address - is a secret too, though it is typed in the open. */
      input.type = setting.type === "secret" ? "password" : "url";
      input.placeholder = value ? "Unchanged" : setting.type === "link" ? "https://" : "Not set";
      if (setting.type === "link" && preset) {
        /* Defaults are public, custom addresses remain write-only. */
        if (typeof value === "string") input.value = value;
        input.dataset.changed = typeof value === "string" ? "1" : "0";
        input.addEventListener("input", () => { input.dataset.changed = "1"; });
      }
    } else {
      input.inputMode = setting.type === "number" ? "decimal" : "text";
      if (setting.type === "url" || setting.type === "link") {
        /* A server of the user's own with no path after it, or a whole address. */
        input.type = "url";
        input.placeholder = "https://";
      }
      if (setting.type === "link") input.maxLength = MANIFEST_SETTING_VALUE_MAX;
      input.value = value;
    }
  }
  input.id = id;
  input.dataset.key = setting.key;
  input.dataset.type = setting.type;
  const parts = setting.type === "toggle" ? [label] : [label, input];
  if (setting.type === "link" && preset) {
    const clear = make("button", { type: "button", textContent: "Remove" });
    clear.addEventListener("click", () => {
      input.value = "";
      input.dataset.changed = "1";
      input.placeholder = "Not set";
    });
    parts.push(clear);
  }
  if (setting.help) input.title = setting.help;
  return parts;
}

let appInDialog = null;

function openAppSettings(app) {
  appInDialog = app;
  show("app-dialog-title", app.title);
  byId("app-fields").replaceChildren(...settingFields(app));
  byId("app-companion").hidden = !app.companion;
  byId("app-dialog").showModal();
}

/* ---- the companion ----
 * A page of the application's author that does what a form cannot: signs
 * the user in somewhere, lets them choose from what is there. It is opened
 * in a window of its own and talks to this page in messages:
 *   it says   { perinkpheral: "ready" }
 *   we answer { perinkpheral: "app", name, title, settings, values }  (no secrets)
 *   it says   { perinkpheral: "settings", values: { key: "value" } }
 * and the values go into the form, for the user to save. Only the window
 * this page opened is listened to, and only from the companion's address. */
let companion = null;

byId("app-companion").addEventListener("click", () => {
  companion = { app: appInDialog, origin: new URL(appInDialog.companion).origin,
                page: window.open(appInDialog.companion, "perinkpheral-companion",
                                  "popup,width=520,height=720") };
  if (!companion.page) toast("Allow pop-ups for this page.", "error");
});

function fillFromCompanion(values) {
  let filled = 0;
  for (const input of byId("app-fields").querySelectorAll("[data-key]")) {
    const value = values[input.dataset.key];
    if (typeof value !== "string" || value.length > MANIFEST_SETTING_VALUE_MAX) continue;
    if (input.dataset.type === "toggle") input.checked = value === "1";
    else if (input.dataset.type === "list") input.setValue(value);
    else input.value = value;
    if (input.dataset.type === "place") input.dataset.chosen = value;
    filled++;
  }
  toast(filled ? "Filled in. Save to apply." : "Nothing to fill in.", filled ? "" : "error");
}

window.addEventListener("message", event => {
  const said = event.data;
  if (!companion || event.source !== companion.page || event.origin !== companion.origin ||
      !said || companion.app !== appInDialog || !byId("app-dialog").open) return;
  if (said.perinkpheral === "ready") {
    const app = companion.app, values = {};
    for (const setting of app.settings)
      if (setting.type !== "secret" && setting.type !== "link") values[setting.key] = app.values[setting.key];
    companion.page.postMessage({ perinkpheral: "app", name: app.name, title: app.title,
                                 settings: app.settings, values }, companion.origin);
  } else if (said.perinkpheral === "settings" && said.values && typeof said.values === "object") {
    fillFromCompanion(said.values);
    companion.page.close();
  }
});

/* One page sends one install at a time. Pending clicks are kept only in memory:
 * leaving the page drops them, and an uncertain POST is never replayed. */
const appInstallQueue = [], pendingAppInstalls = new Map();
let appInstallRunning = false, appInstallClosed = false;

function restoreInstallButton(job) {
  if (!job.button) return;
  job.button.textContent = job.label;
  job.button.disabled = false;
}

function install(button, offer, notifyQueued = true) {
  if (appInstallClosed) return Promise.resolve(false);
  const name = offer.app.name;
  if (pendingAppInstalls.has(name)) return pendingAppInstalls.get(name).promise;
  const have = installed.find(app => app.name === name);
  if (have && !appUpdateAvailable(have.version, offer.app.version)) return Promise.resolve(false);
  const problem = compatibilityProblem(offer.app);
  if (problem) { toast(problem); return Promise.resolve(false); }
  const job = { button, offer, label: button && button.textContent };
  job.promise = new Promise(resolve => { job.resolve = resolve; });
  pendingAppInstalls.set(name, job);
  appInstallQueue.push(job);
  if (button) button.disabled = true;
  if (appInstallRunning) {
    if (button) button.textContent = t("Queued");
    if (notifyQueued) toast(t("{0} queued.", offerSays(offer, "title")), "passive");
    drawTiles();
  }
  void runAppInstalls();
  return job.promise;
}

async function runAppInstalls() {
  if (appInstallRunning || appInstallClosed) return;
  appInstallRunning = true;
  try {
    while (appInstallQueue.length && !appInstallClosed) {
      const job = appInstallQueue.shift();
      restoreInstallButton(job);
      job.started = true;
      drawTiles();
      let done = false;
      try {
        done = await performInstall(job.button, job.offer);
      } catch (error) {
        if (!appInstallClosed) toast(offerSays(job.offer, "title") + ": " + error.message, "error");
      } finally {
        pendingAppInstalls.delete(job.offer.app.name);
        if (!appInstallClosed) drawTiles();
        job.resolve(done);
      }
    }
  } finally {
    appInstallRunning = false;
  }
}

window.addEventListener("beforeunload", event => {
  if (!pendingAppInstalls.size) return;
  event.preventDefault();
  event.returnValue = "";
});
window.addEventListener("pagehide", () => {
  appInstallClosed = true;
  for (const job of appInstallQueue.splice(0)) {
    restoreInstallButton(job);
    pendingAppInstalls.delete(job.offer.app.name);
    job.resolve(false);
  }
});

window.addEventListener("pageshow", event => {
  if (!event.persisted) return;
  appInstallClosed = false;
  void loadApps().catch(error => toast(error.message, "error"));
});

function markPendingInstall(button, offer) {
  const job = pendingAppInstalls.get(offer.app.name);
  if (!job) return;
  button.disabled = true;
  button.textContent = t(job.started ? (installed.some(app => app.name === offer.app.name) ? "Updating" : "Installing") : "Queued");
}

/* Recheck a queued offer before it starts: another completed action may
 * already have installed it. Permission dialogs also run one at a time. */
async function performInstall(button, offer) {
  const have = installed.find(app => app.name === offer.app.name);
  if (have && !appUpdateAvailable(have.version, offer.app.version)) return false;
  const problem = compatibilityProblem(offer.app);
  if (problem) { toast(problem); return false; }
  if (offer.app.system &&
      !await confirmed(t("{0} requests system access.", offerSays(offer, "title")), "Install")) return false;
  const trusts = offer.app.trusts || [];
  if (trusts.length &&
      !await confirmed(t(trusts.length > 1 ? "{0} trusts its own certificate authorities: {1}."
                                           : "{0} trusts its own certificate authority: {1}.",
                         offerSays(offer, "title"), trusts.join(", ")), "Install")) return false;
  if (appInstallClosed) return false;
  const url = "/api/apps/install?source=" + offer.source + "&app=" + encodeURIComponent(offer.app.name);
  const work = async () => {
    const dismiss = toast(t(have ? "Updating {0}…" : "Installing {0}…", offerSays(offer, "title")), "busy passive", true);
    try {
      await askForApp(url, post({ files: offer.app.files, version: offer.app.version }));
      if (!appInstallClosed)
        toast(have ? t("{0} updated to {1}.", offerSays(offer, "title"), offer.app.version) : "Installed.");
    } catch (error) {
      throw new Error(offerSays(offer, "title") + ": " +
        (error.message === "Failed to fetch" ? t("Device unreachable.") : error.message));
    } finally {
      dismiss();
    }
  };
  let done;
  if (button) done = await busy(button, work);
  else {
    try { await work(); done = true; }
    catch (error) { if (!appInstallClosed) toast(error.message, "error"); done = false; }
  }
  if (done && !appInstallClosed) await loadApps();
  return done;
}

/* An icon is eleven rows of eleven cells; what a catalog gives that is not one is no icon. */
const isIcon = icon => Array.isArray(icon) && icon.length === 11 &&
  icon.every(row => typeof row === "string" && row.length === 11);

function picture(icon, cell) {
  const box = make("div", { className: "picture" + (isIcon(icon) ? "" : " none") });
  box.style.width = box.style.height = 11 * cell + "px";
  if (isIcon(icon)) box.append(pixels(icon, cell));
  return box;
}

/* One result for Install, Update and Update all; downloaded manifests are
 * checked again by the device. Missing ABI information is incompatible. */
function compatibilityProblem(app) {
  const result = appCompatibility(app, status);
  if (result === "abi") return t("Update the app.");
  if (result === "firmware") return t("needs firmware {0}", app.firmware);
  return "";
}

/* An application on the device. `offer` is what a catalog has under its name, if any. */
function tile(have, offer) {
  const box = make("div", { className: "tile" });
  box.append(picture(isIcon(have.icon) ? have.icon : offer && offer.app.icon, 5),
             make("b", { textContent: have.title, translate: false }), make("small", { textContent: have.version }));
  /* What the device shows by itself is marked: after a restart, and when the menu is left. */
  if (settings.default_app === have.name) box.append(make("span", { className: "mark", textContent: "Default" }));
  box.append(make("div", { className: "spring" }));
  const tools = make("div", { className: "tools" });
  if (have.settings.some(setting => !setting.hidden))
    tools.append(iconButton(GEAR, "Settings", () => openAppSettings(have)));
  tools.append(iconButton(INFO, "Info", () => openAppInfo(have)));
  box.append(tools);
  if (offer && catalogsRead && appUpdateAvailable(have.version, offer.app.version)) {
    /* An update this firmware is too old for is not offered: the tile says
     * what it needs instead, where it can be read. */
    const needed = compatibilityProblem(offer.app);
    if (needed) {
      box.append(make("small", { className: "needs", textContent: needed }));
    } else {
      const update = make("button", { className: "action", textContent: "Update" });
      update.title = offer.app.version;
      markPendingInstall(update, offer);
      update.addEventListener("click", () => install(update, offer));
      box.append(update);
    }
  }
  const remove = make("button", { className: "action quiet", textContent: "Remove" });
  remove.addEventListener("click", async () => {
    if (await confirmed(t("Remove {0}? Settings are kept.", have.title), "Remove") &&
        await send(remove, "/api/apps/remove?app=" + encodeURIComponent(have.name), { method: "POST" }))
      await loadApps();
  });
  box.append(remove);
  return box;
}

/* ---- what an application is, as its manifest says it ---- */

const pace = seconds => seconds % 3600 === 0 ? t("{0} h", seconds / 3600)
                      : seconds % 60 === 0 ? t("{0} min", seconds / 60) : t("{0} s", seconds);

/* A poll's pace: so many seconds, or "{{key}}" of a setting the user chose it in. */
function paceOf(app, every) {
  const key = typeof every === "string" ? (every.match(/^\{\{(.+)\}\}$/) || [])[1] : null;
  if (!key) return pace(every);
  const setting = app.settings.find(have => have.key === key);
  const chosen = app.values[key] || (setting && setting.default) || "";
  const at = setting && setting.options ? setting.options.indexOf(chosen) : -1;
  return at >= 0 && setting.labels ? setting.labels[at] : chosen || "-";
}

/* The host of an address; "{{server}}" is wherever the user said the server is. */
function hostOf(app, address) {
  const key = (address.match(/^\{\{(\w+)\}\}/) || [])[1];
  /* An address the device keeps to itself - a calendar's - is named, not shown. */
  if (key && typeof app.values[key] !== "string") {
    const kept = app.settings.find(have => have.key === key);
    return (kept ? kept.title : key) + (app.values[key] ? "" : " (" + t("not set") + ")");
  }
  const whole = key ? address.replace("{{" + key + "}}", app.values[key] || "") : address;
  try {
    return new URL(whole.includes("://") ? whole : "http://" + whole).host;
  } catch (error) {
    const setting = app.settings.find(have => have.key === key);
    return setting ? setting.title + " (" + t("not set") + ")" : address;
  }
}

function appFacts(app) {
  const hosts = [...new Set([...(app.hosts || []), ...(app.polls || []).map(poll => poll.url)]
                            .map(address => hostOf(app, address)))];
  const updates = (app.polls || []).map(poll => t("Every {0}", paceOf(app, poll.every)));
  return [
    ["Name", app.name],
    ["Version", app.version || "-"],
    ["Connects to", hosts.join("\n") || "Nothing"],
    ["Updates", [...new Set(updates)].join(", ") || "On request"],
    ["Starts with device", (app.intents || []).includes("started") ? "Yes" : "No"],
    ["System access", app.system ? "Yes" : "No"],
    ["Writes files", app.writes_files ? "Yes, 32 KB" : "No"],
    ["Own certificate authorities", (app.trusts || []).join(", ") || "None"],
    ["Screen", app.fullscreen ? "Full" : "With header"],
    ...(app.companion ? [["Set up page", new URL(app.companion).host]] : []),
  ];
}

function openAppInfo(app) {
  show("info-dialog-title", app.title);
  show("info-description", app.description || "");
  byId("info-description").hidden = !app.description;
  byId("info-facts").replaceChildren(...appFacts(app).flatMap(([name, value]) =>
    [make("dt", { textContent: name }), make("dd", { textContent: value })]));
  byId("info-dialog").showModal();
}

/* ---- the tiles, and adding one ---- */

/* What a catalog says of an application - its title, its description - in the page's language
 * when the application has it so, in English otherwise. */
const offerSays = (offer, what) =>
  ((offer.app.translations || {})[pageLanguage] || {})[what] || offer.app[what] || "";

/* Whether the sources were read: until then the catalog says it is loading, and nothing is
 * installed or updated from it. */
let catalogsRead = false;

/* What the catalogs have and the device does not. */
const available = () => offered.filter(offer => !installed.some(have => have.name === offer.app.name));

/* What the catalogs have another version of, that this firmware can run. */
const updates = () => !catalogsRead ? [] : offered.filter(offer => !compatibilityProblem(offer.app) &&
  installed.some(have => have.name === offer.app.name && appUpdateAvailable(have.version, offer.app.version)));

/* The catalog: everything the sources have, the installed among it - Installed, or Update when
 * the source has another version. What was here before comes first: it returns with its settings. */
function drawCatalog() {
  const kept = offer => removed.includes(offer.app.name);
  const isFull = installed.length >= CATALOG_INSTALLED_MAX;
  const wanted = byId("catalog-search").value.trim().toLowerCase();
  const offers = offered;
  const found = offers.filter(offer => !wanted ||
    [offerSays(offer, "title"), offerSays(offer, "description"), offer.app.name].join(" ").toLowerCase().includes(wanted));
  const rows = found.sort((a, b) => kept(b) - kept(a) || offerSays(a, "title").localeCompare(offerSays(b, "title"), pageLanguage))
    .map(offer => {
      const row = make("div", { className: "offer" }), text = make("div");
      const needed = compatibilityProblem(offer.app);
      /* Already in the page's language, from the index: nothing to put into it. */
      text.append(make("b", { textContent: offerSays(offer, "title"), translate: false }));
      if (offerSays(offer, "description"))
        text.append(make("span", { className: "about", textContent: offerSays(offer, "description"), translate: false }));
      if (offer.app.author)
        text.append(make("small", { textContent: offer.app.author, translate: false }));
      text.append(make("small", { textContent: offer.app.version + (kept(offer) ? " - " + t("settings kept") : "") +
                                                (needed ? " - " + needed : "") }));
      const have = installed.find(app => app.name === offer.app.name);
      const isUpdate = have && appUpdateAvailable(have.version, offer.app.version);
      const button = make("button", { className: "quiet", textContent: !have ? "Install" : isUpdate ? "Update" : "Installed" });
      button.disabled = !catalogsRead || Boolean(needed) || (have ? !isUpdate : isFull);
      markPendingInstall(button, offer);
      if (!have || isUpdate) button.addEventListener("click", () => install(button, offer));
      row.append(picture(offer.app.icon, 3), text, button);
      return row;
    });
  const view = byId("catalog-rows");
  /* Until the sources answer the catalog says so, over whatever it has to show meanwhile. */
  const state = catalogsRead ? (offers.length ? "" : "empty") : stateOf("catalog");
  const note = state ? sameNote(view, state) || stateNote(state, loadCatalogs)
    : rows.length ? null : make("p", { className: "note", textContent: "Nothing found." });
  view.replaceChildren(...(note ? [note] : []), ...rows);
}

byId("catalog-search").addEventListener("input", drawCatalog);
byId("clear-catalog-search").addEventListener("click", () => { byId("catalog-search").value = ""; drawCatalog(); });

/* The tiles stand as the device's menu does: in the order the user set on the device,
 * one it does not name yet - installed since - first. */
function inMenuOrder(apps) {
  const order = settings.menu_order || [];
  const place = app => order.indexOf(app.name);
  return apps.map((app, at) => ({ app, at })).sort((a, b) => place(a.app) - place(b.app) || a.at - b.at)
    .map(({ app }) => app);
}

function drawTiles() {
  byId("auto-update-apps").setAttribute("aria-pressed", String(Boolean(settings.app_auto_update)));
  const tiles = inMenuOrder(installed).map(have => tile(have, offered.find(offer => offer.app.name === have.name)));
  if (stateOf("apps") !== "ready") sayState(byId("tiles"), stateOf("apps"), loadApps);
  else if (!tiles.length) sayState(byId("tiles"), "empty");
  else byId("tiles").replaceChildren(...tiles);
  const capacity = byId("app-capacity");
  capacity.setAttribute("aria-valuemax", String(CATALOG_INSTALLED_MAX));
  capacity.setAttribute("aria-valuenow", String(installed.length));
  capacity.setAttribute("aria-valuetext", t("{0} of {1}", installed.length, CATALOG_INSTALLED_MAX));
  capacity.firstElementChild.style.width = Math.min(100, installed.length / CATALOG_INSTALLED_MAX * 100) + "%";
  byId("update-all").hidden = updates().length === 0;
  drawCatalog();
}

/* Bulk and individual updates share the queue, including duplicate clicks. */
byId("update-all").addEventListener("click", async () => {
  const button = byId("update-all"), label = button.textContent;
  if (button.disabled) return;
  button.disabled = true;
  button.classList.add("busy");
  button.textContent = t("Updating");
  try {
    await Promise.all(updates().map(offer => install(null, offer, false)));
  } finally {
    button.textContent = label;
    button.classList.remove("busy");
    button.disabled = false;
  }
});

byId("auto-update-apps").addEventListener("click", async () => {
  const button = byId("auto-update-apps");
  const enabled = !settings.app_auto_update;
  if (await send(button, "/api/settings", post({ app_auto_update: enabled }))) {
    settings.app_auto_update = enabled;
    button.setAttribute("aria-pressed", String(Boolean(enabled)));
  }
});

async function loadApps() {
  try {
    installed = await reading("apps", () => ask("/api/apps"));
    removed = await ask("/api/apps/removed").catch(() => []);
  } finally {
    drawTiles();
  }
  drawDefaultApp();
  openInside(wantedInside);
}

/* The list of sources, as the device keeps it: there with no network too. */
async function loadSources() {
  try {
    sources = (await reading("sources", () => ask("/api/sources"))).sources;
  } catch (error) {
    if (stateOf("sources") === "failed") sayState(byId("source-rows"), "failed", loadSources);
    throw error;
  }
  editableList(byId("source-rows"), { values: sources, most: CATALOG_SOURCES_MAX, fixedFirst: true,
                                      input: { placeholder: "https://" } });
}

function catalogAuthorIsAllowed(author, source) {
  if (author === undefined) return true; // Legacy packages predate author metadata.
  return typeof author === "string" && author.length > 0 &&
    new TextEncoder().encode(author).length <= MANIFEST_AUTHOR_MAX && !/[\x00-\x1f\x7f]/.test(author) &&
    !author.startsWith(" ") && !author.endsWith(" ") &&
    (author.toLowerCase() !== "perinkpheral" || source === 0);
}

/* One reading at a time, whoever asks: the start, Refresh, the sources saved. */
let catalogsReading = null;

function loadCatalogs() {
  catalogsReading = catalogsReading || readCatalogs().finally(() => { catalogsReading = null; });
  return catalogsReading;
}

async function readCatalogs() {
  const found = [], silent = [];
  try {
    await loadSources();
  } catch (error) {
    if (!catalogsRead) loadState.catalog = "failed";
    drawTiles();
    throw error;
  }
  for (let source = 0; source < sources.length; source++) {
    try {
      const catalog = await askForApp("/api/catalog?source=" + source);
      if (!catalog.apps.every(app => catalogAuthorIsAllowed(app.author, source)))
        throw new Error("Invalid app author.");
      /* The first catalog that has an application is the one it comes from. */
      for (const app of catalog.apps)
        if (!found.some(offer => offer.app.name === app.name)) found.push({ app, source });
    } catch (error) {
      silent.push({ source: sources[source], error });
    }
  }
  /* No source answered: what was read before stays, and a catalog never read says it failed. */
  if (found.length || !silent.length) {
    offered = found;
    catalogsRead = true;
    loadState.catalog = "ready";
  } else if (!catalogsRead) {
    loadState.catalog = "failed";
  }
  drawTiles();
  Promise.resolve().then(loadGlyphs).catch(() => {});
  for (const { source, error } of silent)
    toast(error.status === 503 ? error.message : t("Source unavailable: {0}", source), "error");
}

/* ---- sources ---- */

/* A link to a repository as people paste it, read as the catalog folder in it. */
function catalogAddress(line) {
  const repository = line.trim().replace(/\/+$/, "").replace(/\.git$/, "");
  let match = repository.match(/^https:\/\/github\.com\/([^/]+)\/([^/]+)$/);
  if (match) return "https://raw.githubusercontent.com/" + match[1] + "/" + match[2] + "/HEAD/catalog";
  match = repository.match(/^https:\/\/gitlab\.com\/(.+)$/);
  if (match && !repository.includes("/-/")) return repository + "/-/raw/HEAD/catalog";
  match = repository.match(/^https:\/\/codeberg\.org\/[^/]+\/[^/]+$/);
  if (match) return repository + "/raw/branch/main/catalog";
  return repository;
}

byId("open-sources").replaceWith(iconButton(GEAR, "Sources", () => byId("sources-dialog").showModal()));
/* The sources read again: what they have now, without reading the page anew. */
const refreshCatalog = iconButton(REFRESH, "Refresh", async () => {
  refreshCatalog.disabled = true;
  try { await loadCatalogs(); } finally { refreshCatalog.disabled = false; }
});
byId("refresh-catalog").replaceWith(refreshCatalog);

byId("sources").addEventListener("submit", async event => {
  event.preventDefault();
  const added = byId("source-rows").values().map(catalogAddress).filter(address => address);
  if (await send(event.submitter, "/api/sources", post({ sources: added }))) loadCatalogs();
});

byId("refresh-catalogs").addEventListener("click", event => busy(event.target, loadCatalogs));

byId("app-settings").addEventListener("submit", async event => {
  event.preventDefault();
  /* A setting may be named anything a manifest allows, "__proto__" too. */
  const values = Object.create(null);
  for (const input of byId("app-fields").querySelectorAll("[data-type=place]")) {
    if (input.value !== input.dataset.chosen) {
      toast("Select a place from the list.", "error");
      return input.focus();
    }
  }
  for (const input of byId("app-fields").querySelectorAll("[data-key]")) {
    if (input.dataset.type === "toggle") values[input.dataset.key] = input.checked ? "1" : "0";
    else if (input.dataset.type === "list") values[input.dataset.key] = input.listValue();
    /* A calendar's address is often given as webcal://, which is https. */
    else if (input.dataset.type === "link") {
      if (input.value.trim() || input.dataset.changed === "1")
        values[input.dataset.key] = input.value.trim().replace(/^webcals?:\/\//i, "https://");
    }
    else if (input.dataset.type !== "secret" || input.value) values[input.dataset.key] = input.value;
  }
  /* Saved, the form goes: what was typed in it - a secret above all - is not sent again
   * with the next save, to a server changed meanwhile. It opens anew from the device. */
  const url = "/api/apps/settings?app=" + encodeURIComponent(appInDialog.name);
  if (await send(event.submitter, url, post(values))) {
    byId("app-dialog").close();
    loadApps();
  }
});
