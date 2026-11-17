/* SPDX-License-Identifier: MIT
 * The device's own settings, a tab each: general, time, display, network, language. */
"use strict";

let settings = {};

/* The browser's time zone as a POSIX TZ string, which is what the device
 * understands: found by asking the browser where this year's clock changes are. */
function posixZone() {
  const QUARTER_HOUR = 15 * 60 * 1000, year = new Date().getFullYear();
  const offsetAt = time => -new Date(time).getTimezoneOffset();  /* minutes east of UTC */
  const pad = number => String(number).padStart(2, "0");
  const name = minutes => {
    const size = Math.abs(minutes), rest = size % 60;
    return "<" + (minutes < 0 ? "-" : "+") + pad((size - rest) / 60) + (rest ? pad(rest) : "") + ">";
  };
  const clock = (minutes, sign) => {
    const size = Math.abs(minutes), rest = size % 60;
    return sign + (size - rest) / 60 + (rest ? ":" + pad(rest) : "");
  };
  /* POSIX counts the other way: hours to add to local time to get UTC. */
  const zone = minutes => name(minutes) + clock(minutes, minutes > 0 ? "-" : "");

  const changes = [];
  const start = Date.UTC(year, 0, 1), end = Date.UTC(year + 1, 0, 1);
  for (let time = start + QUARTER_HOUR; time < end; time += QUARTER_HOUR) {
    const before = offsetAt(time - QUARTER_HOUR), after = offsetAt(time);
    if (before !== after) changes.push({ time, before, after });
  }
  const isYearlyPair = changes.length === 2 && changes[0].before === changes[1].after &&
                       changes[0].after === changes[1].before;
  if (!isYearlyPair) return zone(offsetAt(Date.now()));

  /* "the last Sunday of March at 2": month, week of the month (5 is the last), day of the week */
  const rule = change => {
    const wall = new Date(change.time + change.before * 60 * 1000);
    const month = wall.getUTCMonth() + 1, day = wall.getUTCDate();
    const daysInMonth = new Date(Date.UTC(year, month, 0)).getUTCDate();
    const week = day + 7 > daysInMonth ? 5 : Math.ceil(day / 7);
    const minutes = wall.getUTCHours() * 60 + wall.getUTCMinutes();
    return "M" + month + "." + week + "." + wall.getUTCDay() + (minutes === 120 ? "" : "/" + clock(minutes, ""));
  };
  const toSummer = changes.find(change => change.after > change.before);
  const toWinter = changes.find(change => change.after < change.before);
  const summerIsOneHour = toSummer.after - toSummer.before === 60;
  return zone(toSummer.before) + name(toSummer.after) +
         (summerIsOneHour ? "" : clock(toSummer.after, toSummer.after > 0 ? "-" : "")) +
         "," + rule(toSummer) + "," + rule(toWinter);
}

async function loadSettings() {
  settings = await ask("/api/settings");
  setLanguage(settings.language);
  for (const key of ["device_name", "ntp_server", "refresh_min_data_s", "refresh_keepalive_h"])
    byId(key).value = settings[key];
  /* A device that was never told its zone is offered the browser's. */
  if (settings.tz === ZONE_NOT_SET) showZone(detectedZone());
  else showZone({ tz: settings.tz, city: settings.tz_city });
  const fullEvery = byId("refresh_full_every");
  /* A value the list does not have - set some other way - gets a line of its own. */
  if (![...fullEvery.options].some(option => Number(option.value) === settings.refresh_full_every))
    fullEvery.append(make("option", { value: settings.refresh_full_every,
                                      textContent: t("Every {0} updates", settings.refresh_full_every) }));
  fullEvery.value = settings.refresh_full_every;
  byId("dither").checked = settings.dither;
  byId("font").value = settings.font;
  byId("refresh_fast").checked = settings.refresh_fast;
  byId("header_clock").checked = settings.header_clock;
  byId("time_auto").setAttribute("aria-pressed", String(Boolean(settings.time_auto)));
  byId("power_saving").value = settings.power_saving;
  const idle = byId("menu_idle_min");
  if (![...idle.options].some(option => Number(option.value) === settings.menu_idle_min))
    idle.append(make("option", { value: settings.menu_idle_min, textContent: t("{0} min", settings.menu_idle_min) }));
  idle.value = settings.menu_idle_min;
  byId("ssid").value = settings.wifi_ssid;
  byId("hostname").value = settings.hostname ? settings.hostname + ".local" : "";
  byId("language-choice").replaceChildren(...(settings.languages || []).map(
    language => make("option", { value: language.code, textContent: language.name })));
  byId("language-choice").value = settings.language;
  drawClock();
  drawDefaultApp();
  drawStatus();
  drawTiles();
}

/* None, and every application there is; one that was removed stays in the
 * list while it is the choice, so that saving the form does not change it. */
function drawDefaultApp() {
  const select = byId("default_app");
  const chosen = settings.default_app || "";
  const apps = installed.map(have => ({ value: have.name, textContent: have.title }));
  if (chosen && !installed.some(have => have.name === chosen))
    apps.push({ value: chosen, textContent: chosen });
  select.replaceChildren(make("option", { value: "", textContent: "None" }),
                         ...apps.map(app => make("option", app)));
  select.value = chosen;
}

function saveSettings(formId, read) {
  byId(formId).addEventListener("submit", async event => {
    event.preventDefault();
    if (await send(event.submitter, "/api/settings", post(read()))) {
      await loadSettings();
      loadStatus();
    }
  });
}

saveSettings("general", () => ({ device_name: byId("device_name").value,
                                 default_app: byId("default_app").value,
                                 menu_idle_min: Number(byId("menu_idle_min").value),
                                 power_saving: Number(byId("power_saving").value) }));
/* The time pane is saved whole: the zone, the time server, whether the network's time is taken,
 * and - when it is not - the date and time typed, which the device takes only then. */
const isNetworkTime = () => byId("time_auto").getAttribute("aria-pressed") === "true";
byId("time-zone").addEventListener("submit", async event => {
  event.preventDefault();
  const moment = new Date(byId("clock-value").value).getTime();
  const isSaved = await busy(event.submitter, async () => {
    await ask("/api/settings", post({ ...chosenZone(), ntp_server: byId("ntp_server").value,
                                      time_auto: isNetworkTime() }));
    if (!isNetworkTime() && !isNaN(moment)) await ask("/api/clock", post({ unix: Math.round(moment / 1000) }));
  });
  if (isSaved) {
    await loadSettings();
    loadStatus();
  }
});
saveSettings("display", () => ({
  refresh_full_every: Number(byId("refresh_full_every").value),
  refresh_min_data_s: Number(byId("refresh_min_data_s").value),
  refresh_keepalive_h: Number(byId("refresh_keepalive_h").value),
  dither: byId("dither").checked,
  font: Number(byId("font").value),
  refresh_fast: byId("refresh_fast").checked,
  header_clock: byId("header_clock").checked,
}));
saveSettings("language-form", () => ({ language: byId("language-choice").value }));

/* ---- the time zone: by a city of the device's list, or a POSIX string of one's own ---- */

const cityName = city => pageLanguage === "ru" ? city.ru : city.name;

/* The cities by the names the page shows, and "Custom" after them. */
byId("tz-city").replaceChildren(
  ...[...CITIES].sort((a, b) => cityName(a).localeCompare(cityName(b), pageLanguage))
    .map(city => make("option", { value: city.name, textContent: cityName(city) })),
  make("option", { value: "", textContent: "Custom" }));

const cityCalled = name => CITIES.find(city => city.name === name);

/* The browser's zone: its city by the zone's name, or by its rules, or the rules alone. */
function detectedZone() {
  const named = Intl.DateTimeFormat().resolvedOptions().timeZone;
  const city = CITIES.find(have => have.iana.includes(named));
  if (city) return { tz: city.zone, city: city.name };
  const tz = posixZone();
  return { tz, city: "" };
}

/* A zone shown: its city when it was chosen by one or a city has it, else as it is written. */
function showZone({ tz, city }) {
  const chosen = cityCalled(city);
  const found = chosen && chosen.zone === tz ? chosen : CITIES.find(have => have.zone === tz);
  byId("tz-city").value = found ? found.name : "";
  byId("tz").value = tz;
  byId("tz-own").hidden = Boolean(found);
}

/* What the form says: the zone and the city it was chosen by, none for one of one's own. */
function chosenZone() {
  const city = cityCalled(byId("tz-city").value);
  return city ? { tz: city.zone, tz_city: city.name } : { tz: byId("tz").value, tz_city: "" };
}

byId("tz-city").addEventListener("change", () => {
  const city = cityCalled(byId("tz-city").value);
  if (city) byId("tz").value = city.zone;
  byId("tz-own").hidden = Boolean(city);
});
byId("tz-detect").addEventListener("click", () => showZone(detectedZone()));

/* ---- the clock by hand: while the network's time is not taken ---- */

/* The browser's own wall time, as a field of date and time reads it. */
function localNow() {
  const now = new Date();
  return new Date(now.getTime() - now.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
}

/* The date and time are always there; taken from the network, they are the device's to keep and
 * cannot be typed. Typed, they are this computer's wall time, and the moment goes to the device. */
function drawClock() {
  byId("clock-value").disabled = isNetworkTime();
  if (!byId("clock-value").value || isNetworkTime()) byId("clock-value").value = localNow();
}

byId("time_auto").addEventListener("click", () => {
  byId("time_auto").setAttribute("aria-pressed", String(!isNetworkTime()));
  drawClock();
});

/* ---- the network: where the device is, and another to join ---- */

function drawNetworkNow() {
  const network = statusFacts().find(([name]) => name === "Network");
  byId("network-now").replaceChildren(...factList(network ? network[1] : []));
}

byId("wifi").addEventListener("submit", async event => {
  event.preventDefault();
  const network = { ssid: byId("ssid").value, password: byId("password").value };
  /* Setting the device up is one step: it learns where it is along with the network. */
  if (settings.tz === ZONE_NOT_SET) {
    const zone = detectedZone();
    network.tz = zone.tz;
    network.tz_city = zone.city;
  }
  toast(CONFIRM_ON_DEVICE);
  if (await send(event.submitter, "/api/wifi", post(network))) afterRestart();
});

/* ---- letters beyond ASCII: glyph packs ----
 * The device's fonts are ASCII; an alphabet is a pack the user adds. The official
 * catalog lists them, the browser fetches one and hands it to the device. Without a
 * pack its letters are spelled in Latin ones. */
async function loadGlyphs() {
  const rows = byId("glyph-rows");
  let have, offers;
  try {
    have = await ask("/api/glyphs");
    offers = await (await fetch(sources[0] + "/glyphs/index.json")).json();
  } catch (error) {
    rows.textContent = "Catalog unavailable.";
    return;
  }
  rows.replaceChildren(...offers.map(offer => {
    const row = make("div", { className: "offer" }), text = make("div");
    const isThere = have.packs.some(pack => pack.name === offer.name);
    text.append(make("b", { textContent: offer.title }),
                make("small", { textContent: offer.languages.split(", ").map(language => t(language)).join(", ") }));
    const button = make("button", { textContent: isThere ? "Remove" : "Install" });
    if (isThere) button.className = "quiet";
    button.addEventListener("click", () => busy(button, async () => {
      if (isThere) {
        await ask("/api/glyphs/remove?name=" + offer.name, { method: "POST" });
      } else {
        const pack = await (await fetch(sources[0] + "/glyphs/" + offer.file)).arrayBuffer();
        await ask("/api/glyphs", { method: "POST", body: pack });
      }
      await loadGlyphs();
    }));
    row.append(text, button);
    return row;
  }));
}
