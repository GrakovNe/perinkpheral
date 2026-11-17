/* SPDX-License-Identifier: MIT
 * What every part of the page uses: talking to the device, saying what happened, asking
 * before what cannot be undone, and the sections a link can lead to. */
"use strict";

const SLOTS = 24, SOURCES_MAX = 4, ZONE_NOT_SET = "UTC0";
const byId = id => document.getElementById(id);
const show = (id, text) => { byId(id).textContent = text; };
const make = (tag, properties) => Object.assign(document.createElement(tag), properties || {});

/* ---- pictures: the device's own icons, eleven cells a side ---- */

const GEAR = ["....###....", ".##.###.##.", ".#########.", "..##...##..", "###.....###",
              "###.....###", "###.....###", "..##...##..", ".#########.", ".##.###.##.",
              "....###...."];

const INFO = ["...#####...", "..#.....#..", ".#...#...#.", "#.........#", "#....#....#",
              "#....#....#", "#....#....#", ".#...#...#.", "..#.....#..", "...#####...",
              "..........."];


/* An icon as the manifest writes it - rows of '#' and '.' - drawn cell by cell. */
function pixels(rows, cell) {
  const NS = "http://www.w3.org/2000/svg", size = rows.length * cell;
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("class", "pixels");
  svg.setAttribute("width", size);
  svg.setAttribute("height", size);
  svg.setAttribute("viewBox", "0 0 " + size + " " + size);
  rows.forEach((row, y) => {
    /* A run of ink in a row is one rectangle. */
    for (const run of row.matchAll(/#+/g)) {
      const rect = document.createElementNS(NS, "rect");
      rect.setAttribute("x", run.index * cell);
      rect.setAttribute("y", y * cell);
      rect.setAttribute("width", run[0].length * cell);
      rect.setAttribute("height", cell);
      svg.append(rect);
    }
  });
  return svg;
}

function iconButton(rows, title, onClick) {
  const button = make("button", { className: "icon", title, type: "button" });
  button.append(pixels(rows, 2));
  button.addEventListener("click", onClick);
  return button;
}

/* ---- talking to the device ---- */

async function ask(url, options) {
  const answer = await fetch(url, options);
  const json = await answer.json();
  if (!answer.ok) throw new Error(json.error || "HTTP " + answer.status);
  return json;
}

const post = body => ({ method: "POST", body: JSON.stringify(body) });

/* ---- what happened: on the button that did it, and in the corner ---- */

/* A message in the corner. It goes by itself unless it is told to stay;
 * a click sends it away at once. Returns the way to take it down. */
function toast(text, kind, stays) {
  const box = make("div", { className: "toast" + (kind ? " " + kind : ""), textContent: text });
  const close = () => box.remove();
  box.addEventListener("click", close);
  /* An open dialog covers the page: the message has to be in it to be seen. */
  (document.querySelector("dialog[open]") || document.body).append(byId("toasts"));
  byId("toasts").append(box);
  if (!stays) setTimeout(close, kind === "error" ? 6000 : 3500);
  return close;
}

/* How a button reads while its work is under way and right after it. */
const WORDS = { Save: ["Saving", "Saved"], Install: ["Installing", "Installed"],
                Remove: ["Removing", "Removed"], Update: ["Updating", "Updated"],
                Connect: ["Connecting", "Connected"], Restart: ["Restarting", "Restarting"],
                "Factory reset": ["Resetting", "Reset"], Refresh: ["Loading", "Refresh"],
                Set: ["Setting", "Set"], "Check for updates": ["Checking", "Check for updates"] };

/* Runs the work with the button showing it: dots while it goes on, a tick
 * when it is done, and the button as it was a moment later. What went
 * wrong goes to the corner. Returns whether it went well. */
async function busy(button, work) {
  const label = button.textContent;
  /* The label may be in the page's language; the words are English, and put into it where they stand. */
  const words = WORDS[Object.keys(WORDS).find(word => label.startsWith(word) || label.startsWith(t(word)))] ||
                [label, label];
  /* Its width stays: the text changes under the user's eyes, the layout does not. */
  button.style.minWidth = button.offsetWidth + "px";
  button.disabled = true;
  button.classList.add("busy");
  button.textContent = words[0];
  let done = true;
  try {
    await work();
  } catch (error) {
    toast(error.message === "Failed to fetch" ? "Device unreachable." : error.message, "error");
    done = false;
  }
  button.classList.remove("busy");
  if (done && words[1] !== label) {
    button.classList.add("done");
    button.textContent = words[1];
    await new Promise(resolve => setTimeout(resolve, 1400));
    button.classList.remove("done");
  }
  button.textContent = label;
  button.disabled = false;
  return done;
}

/* The device's "Saved." is what the tick on the button says already; anything more
 * than that - "Restart to apply." - is worth a line in the corner. */
const send = (button, url, options) => busy(button, async () => {
  const answer = await ask(url, options);
  const isPlain = !answer.ok || answer.ok === "Saved." || answer.ok.endsWith("Restarting.");
  if (!isPlain) toast(answer.ok);
});

/* What cannot be taken back is asked about on the device's own screen. */
const CONFIRM_ON_DEVICE = "Confirm on the device.";

/* A question before something that cannot be taken back. */
/* `notes`, when given, stand under the question as they were written: what a release brings. */
function confirmed(text, action, notes) {
  show("confirm-text", text);
  show("confirm-yes", action);
  show("confirm-notes", notes || "");
  byId("confirm-notes").hidden = !notes;
  const dialog = byId("confirm-dialog");
  dialog.returnValue = "no";
  dialog.showModal();
  return new Promise(resolve => dialog.addEventListener(
    "close", () => resolve(dialog.returnValue === "yes"), { once: true }));
}

/* After a restart the device is gone for a moment; then the page reads everything anew. */
let restarting = null;

function afterRestart() {
  if (restarting) return;
  restarting = toast("Restarting", "busy", true);
  setTimeout(async function retry() {
    try {
      await loadAll();
      restarting();
      restarting = null;
    } catch (error) {
      setTimeout(retry, 2000);
    }
  }, 9000);
}

/* ---- sections ----
 * "#settings" is a section, "#settings:language" a tab of it; "#apps:weather" is the section
 * with the settings of an application open in it, "#apps:weather:info" with what it is, and
 * "#apps:catalogs" with the list of catalogs: a link can lead straight to any of them. */

const sectionShown = [];  /* what a section does when it comes into view: (name) => void */

function currentSection() {
  const wanted = (location.hash.slice(1) || "apps").split(":")[0];
  return byId(wanted) && byId(wanted).tagName === "SECTION" ? wanted : "apps";
}

function showSection() {
  const inside = (location.hash.slice(1) || "apps").split(":").slice(1).join(":");
  const name = currentSection();
  if (name === "settings") showPane(byId(inside) && byId(inside).classList.contains("pane") ? inside : "general");
  else openInside(inside);
  for (const section of document.querySelectorAll("section"))
    section.classList.toggle("current", section.id === name);
  for (const link of document.querySelectorAll("nav a:not(.brand)")) {
    link.classList.toggle("current", link.hash === "#" + name);
    /* On a phone the menu is a line that scrolls: the section open is brought into it. */
    if (link.hash === "#" + name)
      document.fonts.ready.then(() => requestAnimationFrame(() => {
        const row = link.parentElement.getBoundingClientRect(), at = link.getBoundingClientRect();
        if (at.right > row.right) link.parentElement.scrollLeft += at.right - row.right;
        else if (at.left < row.left) link.parentElement.scrollLeft -= row.left - at.left;
      }));
  }
  for (const shown of sectionShown) shown(name);
  /* The browser scrolls to what the link names; a section starts at its top. */
  scrollTo(0, 0);
}

let wantedInside = null;

function openInside(what) {
  wantedInside = what || null;
  if (!wantedInside) return;
  if (wantedInside === "catalogs") {
    byId("sources-dialog").showModal();
    wantedInside = null;
    return;
  }
  /* An application's settings can open once the device has said what it holds. */
  const [name, page] = wantedInside.split(":");
  const app = installed.find(have => have.name === name);
  if (app && page === "info") {
    openAppInfo(app);
    wantedInside = null;
  } else if (app && app.settings.length) {
    openAppSettings(app);
    wantedInside = null;
  }
}

function showPane(name) {
  for (const pane of document.querySelectorAll(".pane"))
    pane.classList.toggle("current", pane.id === name);
  for (const tab of byId("tabs").children) {
    tab.classList.toggle("current", tab.dataset.pane === name);
    /* On a phone the tabs are one row that scrolls: the chosen one in sight,
     * once the section it is in is laid out in its own letters. */
    if (tab.dataset.pane === name)
      document.fonts.ready.then(() => requestAnimationFrame(() => {
        const row = tab.parentElement.getBoundingClientRect(), at = tab.getBoundingClientRect();
        if (at.right > row.right) tab.parentElement.scrollLeft += at.right - row.right;
        else if (at.left < row.left) tab.parentElement.scrollLeft -= row.left - at.left;
      }));
  }
}

for (const tab of byId("tabs").children)
  tab.addEventListener("click", () => { location.hash = "#settings:" + tab.dataset.pane; });

for (const close of document.querySelectorAll("[data-close]"))
  close.addEventListener("click", () => close.closest("dialog").close());

/* A list the user edits a line at a time: a field an item, each with a button that takes it
 * away, and one at the end that adds a line while there is room for one. A first line that is
 * fixed - the official catalog - is shown and kept. box.values() is what is in the others. */
function editableList(box, { values, most = Infinity, fixedFirst = false, input = {} }) {
  const row = (value, isFixed) => {
    const line = make("div");
    const field = make("input", Object.assign({ value, spellcheck: false, autocomplete: "off" }, input));
    field.disabled = isFixed;
    line.append(field);
    if (!isFixed) {
      const remove = make("button", { type: "button", className: "quiet", textContent: "−",
                                      title: "Remove" });
      remove.addEventListener("click", () => { line.remove(); drawAdd(); });
      line.append(remove);
    }
    return line;
  };
  const drawAdd = () => {
    for (const old of box.querySelectorAll(".add")) old.remove();
    if (box.querySelectorAll("input").length >= most) return;
    const add = make("button", { type: "button", className: "quiet add", textContent: "+", title: "Add" });
    add.addEventListener("click", () => {
      const line = row("", false);
      add.before(line);
      line.querySelector("input").focus();
      drawAdd();
    });
    box.append(add);
  };
  box.setValues = list => {
    box.replaceChildren(...list.map((value, index) => row(value, fixedFirst && index === 0)));
    drawAdd();
  };
  box.values = () => [...box.querySelectorAll("input:not(:disabled)")].map(field => field.value.trim())
    .filter(value => value);
  box.setValues(values);
  return box;
}
