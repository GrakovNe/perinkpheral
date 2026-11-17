/* SPDX-License-Identifier: MIT
 * The device's log, a section of its own: what was written since the last look, a line a row -
 * when, how serious, who and what - narrowed to what a search names, followed while it is in
 * sight. After a restart the device's log starts anew, and so does the page's, under a line. */
"use strict";

const LOG_EVERY_MS = 2000, LOG_LINES_KEPT = 4000;
let logEnd = null, logTimer = 0, logReading = Promise.resolve(), logRest = "";
const logLines = []; /* { at, level, tag, text } or { restart: true } */

/* "I (2509707) httpd: what": the level, the milliseconds since the start, who, what. */
const LOG_LINE = /^([EWIDV]) \((\d+)\) ([^:]+?): ?(.*)$/;

function parseLogLine(line) {
  const found = line.match(LOG_LINE);
  return found ? { level: found[1], at: Number(found[2]), tag: found[3], text: found[4] }
               : { level: "", at: null, tag: "", text: line };
}

/* The time of day a line was written: the device started its uptime ago, as the status said. */
function logClock(at) {
  if (at === null) return "";
  if (!statusAt || status.uptime_s === undefined) return (at / 1000).toFixed(1);
  const when = new Date(statusAt - status.uptime_s * 1000 + at);
  return [when.getHours(), when.getMinutes(), when.getSeconds()].map(part => String(part).padStart(2, "0")).join(":");
}

const logSearched = () => byId("log-search").value.trim().toLowerCase();

function logMatches(line, wanted) {
  return !wanted || line.restart ||
         (line.tag + " " + line.text + " " + logClock(line.at)).toLowerCase().includes(wanted);
}

function logRow(line) {
  if (line.restart) return make("div", { className: "log-row restart", textContent: t("restart") });
  const row = make("div", { className: "log-row" + (line.level ? " " + line.level.toLowerCase() : "") });
  row.append(make("span", { className: "when", textContent: logClock(line.at) }),
             make("span", { className: "level", textContent: line.level }),
             make("span", { className: "tag", textContent: line.tag }),
             make("span", { className: "text", textContent: line.text }));
  return row;
}

function drawLogCount(shown) {
  const lines = logLines.filter(line => !line.restart).length;
  show("log-count", logSearched() ? t("{0} of {1}", shown, lines) : lines ? t("{0} lines", lines) : "");
}

/* The whole log anew: a search changed, or lines were let go of. */
function drawLog() {
  const view = byId("log-lines"), wanted = logSearched();
  const rows = logLines.filter(line => logMatches(line, wanted)).map(logRow);
  view.replaceChildren(...rows);
  if (!rows.length) view.append(make("p", { className: "note", textContent: logLines.length ? "Nothing found." : "Empty." }));
  drawLogCount(rows.filter(row => !row.classList.contains("restart")).length);
  view.scrollTop = view.scrollHeight;
}

function addLogLines(lines) {
  const view = byId("log-lines"), wanted = logSearched();
  const atEnd = view.scrollTop + view.clientHeight >= view.scrollHeight - 8;
  logLines.push(...lines);
  if (logLines.length > LOG_LINES_KEPT) {
    logLines.splice(0, logLines.length - LOG_LINES_KEPT);
    drawLog();
    return;
  }
  view.querySelector(":scope > .note")?.remove();
  view.append(...lines.filter(line => logMatches(line, wanted)).map(logRow));
  drawLogCount(view.querySelectorAll(".log-row:not(.restart)").length);
  /* A reader who scrolled up to read is left where they are. */
  if (atEnd) view.scrollTop = view.scrollHeight;
}

async function readLogNow() {
  const answer = await fetch("/api/log" + (logEnd === null ? "" : "?since=" + logEnd));
  if (!answer.ok) throw new Error("HTTP " + answer.status);
  const end = Number(answer.headers.get("X-Log-End"));
  const text = await answer.text();
  /* A position behind the one before: the device started anew, and its log with it. */
  if (logEnd !== null && end < logEnd) {
    logEnd = null;
    logRest = "";
    addLogLines([{ restart: true }]);
    return readLogNow();
  }
  logEnd = end;
  /* A line is shown once it is whole: the rest waits for the next look. */
  const parts = (logRest + text).split("\n");
  logRest = parts.pop();
  addLogLines(parts.filter(line => line.trim()).map(parseLogLine));
}

/* One read at a time: the one that follows and the one a press asks for would both add what
 * came since the same place. `fresh`: the whole log anew. */
function readLog(fresh) {
  logReading = logReading.catch(() => {}).then(() => {
    if (fresh) {
      logEnd = null;
      logRest = "";
      logLines.length = 0;
      drawLog();
    }
    return readLogNow();
  });
  return logReading;
}

async function followLog() {
  clearTimeout(logTimer);
  if (currentSection() !== "log" || document.hidden) return;
  try { await readLog(false); } catch (error) { /* away for a moment: asked again */ }
  if (isFollowing()) logTimer = setTimeout(followLog, LOG_EVERY_MS);
}

byId("load-log").addEventListener("click", event => busy(event.target, () => readLog(true)));
/* Follow is a button that stays down: pressed, in ink, while the log is followed. */
const isFollowing = () => byId("follow-log").getAttribute("aria-pressed") === "true";
byId("follow-log").addEventListener("click", () => {
  byId("follow-log").setAttribute("aria-pressed", String(!isFollowing()));
  followLog();
});
byId("clear-log").addEventListener("click", () => { logLines.length = 0; drawLog(); });
byId("log-search").addEventListener("input", drawLog);
byId("clear-search").addEventListener("click", () => { byId("log-search").value = ""; drawLog(); });
sectionShown.push(name => { if (name === "log") followLog(); });
document.addEventListener("visibilitychange", followLog);
drawLog();
