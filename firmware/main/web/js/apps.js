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
          .map(place => ({ ...place, name: place.name.filter(part => part).join(", ").slice(0, input.maxLength) }))
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
    if (filled.has(setting.key)) continue;
    const field = settingField(app, setting);
    if (setting.type === "place")
      field[1] = placeField(field[1], hidden[setting.latitude], hidden[setting.longitude]);
    parts.push(...field);
  }
  return parts;
}

/* A list setting: a line an item, stored as the items with a line break between them. */
const LIST_ITEM_MAX = 63;

function listField(value) {
  const box = editableList(make("div", { className: "rows" }),
                           { values: value ? value.split("\n") : [], input: { maxLength: LIST_ITEM_MAX } });
  box.setValue = text => box.setValues(text ? text.split("\n") : []);
  box.listValue = () => box.values().join("\n");
  return box;
}

/* One field of an application's settings, as its manifest describes it. */
function settingField(app, setting) {
  const preset = Array.isArray(setting.default) ? setting.default.join("\n") : setting.default;
  const id = "setting-" + setting.key, value = app.values[setting.key] || preset || "";
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
    input = make("input", { maxLength: 255, autocomplete: "off" });
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
    } else {
      input.inputMode = setting.type === "number" ? "decimal" : "text";
      if (setting.type === "url" || setting.type === "link") {
        /* A server of the user's own with no path after it, or a whole address. */
        input.type = "url";
        input.placeholder = "https://";
      }
      if (setting.type === "link") input.maxLength = 255;
      input.value = value;
    }
  }
  input.id = id;
  input.dataset.key = setting.key;
  input.dataset.type = setting.type;
  const parts = setting.type === "toggle" ? [label] : [label, input];
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
    if (typeof value !== "string" || value.length > 255) continue;
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

/* Installs the application from its source, or installs it over itself: an update. */
async function install(button, offer) {
  /* The system API is the owner's to give: the page asks, once, in so many words. */
  if (offer.app.system &&
      !await confirmed(offer.app.title + " requests system access.", "Install")) return;
  /* So is a certificate authority it trusts for its own requests. */
  const trusts = offer.app.trusts || [];
  if (trusts.length &&
      !await confirmed(offer.app.title + " trusts its own certificate " +
                       (trusts.length > 1 ? "authorities: " : "authority: ") + trusts.join(", ") + ".", "Install")) return;
  const url = "/api/apps/install?source=" + offer.source + "&app=" + encodeURIComponent(offer.app.name);
  if (await send(button, url, post({ files: offer.app.files }))) await loadApps();
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

/* An application on the device. `offer` is what a catalog has under its name, if any. */
function tile(have, offer) {
  const box = make("div", { className: "tile" });
  box.append(picture(isIcon(have.icon) ? have.icon : offer && offer.app.icon, 5),
             make("b", { textContent: have.title }), make("small", { textContent: have.version }));
  /* What the device shows by itself is marked: after a restart, and when the menu is left. */
  if (settings.default_app === have.name) box.append(make("span", { className: "mark", textContent: "Default" }));
  box.append(make("div", { className: "spring" }));
  const tools = make("div", { className: "tools" });
  if (have.settings.length) tools.append(iconButton(GEAR, "Settings", () => openAppSettings(have)));
  tools.append(iconButton(INFO, "Info", () => openAppInfo(have)));
  box.append(tools);
  if (offer && offer.app.version !== have.version) {
    const update = make("button", { className: "action", textContent: "Update" });
    update.title = offer.app.version;
    update.addEventListener("click", () => install(update, offer));
    box.append(update);
  }
  const remove = make("button", { className: "action quiet", textContent: "Remove" });
  remove.addEventListener("click", async () => {
    if (await confirmed("Remove " + have.title + "? Settings are kept.", "Remove") &&
        await send(remove, "/api/apps/remove?app=" + encodeURIComponent(have.name), { method: "POST" }))
      await loadApps();
  });
  box.append(remove);
  return box;
}

/* ---- what an application is, as its manifest says it ---- */

const pace = seconds => seconds % 3600 === 0 ? seconds / 3600 + " h"
                      : seconds % 60 === 0 ? seconds / 60 + " min" : seconds + " s";

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
    return (kept ? kept.title : key) + (app.values[key] ? "" : " (not set)");
  }
  const whole = key ? address.replace("{{" + key + "}}", app.values[key] || "") : address;
  try {
    return new URL(whole.includes("://") ? whole : "http://" + whole).host;
  } catch (error) {
    const setting = app.settings.find(have => have.key === key);
    return setting ? setting.title + " (not set)" : address;
  }
}

/* The numbers a place fills are the place's: not settings of their own to a person. */
const hiddenKeys = app => new Set(app.settings.filter(setting => setting.type === "place")
  .flatMap(place => [place.latitude, place.longitude]));

function appFacts(app) {
  const hosts = [...new Set([...(app.hosts || []), ...(app.polls || []).map(poll => poll.url)]
                            .map(address => hostOf(app, address)))];
  const updates = (app.polls || []).map(poll => "Every " + paceOf(app, poll.every));
  const hidden = hiddenKeys(app);
  const stores = app.settings.filter(setting => !hidden.has(setting.key))
    .map(setting => setting.title + (setting.type === "secret" || setting.type === "link" ? " (secret)" : ""));
  return [
    ["Name", app.name],
    ["Version", app.version || "-"],
    ["Connects to", hosts.join("\n") || "Nothing"],
    ["Updates", [...new Set(updates)].join(", ") || "On request"],
    ["Settings", stores.join(", ") || "None"],
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
  byId("info-facts").replaceChildren(...appFacts(app).flatMap(([name, value]) =>
    [make("dt", { textContent: name }), make("dd", { textContent: value })]));
  byId("info-dialog").showModal();
}

/* ---- the tiles, and adding one ---- */

/* What the catalogs have and the device does not. */
const available = () => offered.filter(offer => !installed.some(have => have.name === offer.app.name));

/* The last tile: a way to add an application, while there is a place for one. */
function addTile() {
  const add = make("button", { className: "tile add", title: "Add app" });
  add.append(pixels(PLUS, 4));
  add.disabled = installed.length >= SLOTS;
  add.addEventListener("click", openStore);
  return add;
}

function openStore() {
  /* What was here before comes first: it returns with its settings. */
  const kept = offer => removed.includes(offer.app.name);
  const rows = available().sort((a, b) => kept(b) - kept(a)).map(offer => {
    const row = make("div", { className: "offer" }), text = make("div");
    text.append(make("b", { textContent: offer.app.title }),
                make("small", { textContent: offer.app.version + (kept(offer) ? " - settings kept" : "") }));
    const button = make("button", { textContent: "Install" });
    button.addEventListener("click", async () => {
      await install(button, offer);
      byId("store-dialog").close();
    });
    row.append(picture(offer.app.icon, 3), text, button);
    return row;
  });
  byId("store-rows").replaceChildren(...rows);
  if (!rows.length) byId("store-rows").textContent = "No apps available.";
  byId("store-dialog").showModal();
}

function drawTiles() {
  const tiles = installed.map(have => tile(have, offered.find(offer => offer.app.name === have.name)));
  byId("tiles").replaceChildren(...tiles, addTile());
  show("app-count", installed.length + " of " + SLOTS);
}

async function loadApps() {
  installed = await ask("/api/apps");
  removed = await ask("/api/apps/removed").catch(() => []);
  drawTiles();
  drawDefaultApp();
  openInside(wantedInside);
}

/* The list of sources, as the device keeps it: there with no network too. */
async function loadSources() {
  sources = (await ask("/api/sources")).sources;
  editableList(byId("source-rows"), { values: sources, most: SOURCES_MAX, fixedFirst: true,
                                      input: { placeholder: "https://" } });
}

async function loadCatalogs() {
  await loadSources();
  const found = [], silent = [];
  for (let source = 0; source < sources.length; source++) {
    try {
      const catalog = await ask("/api/catalog?source=" + source);
      /* The first catalog that has an application is the one it comes from. */
      for (const app of catalog.apps)
        if (!found.some(offer => offer.app.name === app.name)) found.push({ app, source });
    } catch (error) {
      silent.push(sources[source]);
    }
  }
  offered = found;
  drawTiles();
  loadGlyphs();
  for (const source of silent) toast("Source unavailable: " + source, "error");
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
      if (input.value.trim()) values[input.dataset.key] = input.value.trim().replace(/^webcals?:\/\//i, "https://");
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
