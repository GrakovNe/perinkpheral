// SPDX-License-Identifier: MIT
// Exercise the page's real settings loader with firmware-owned language choices.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

module.exports = async function () {
  const source = fs.readFileSync(path.join(__dirname, "../../main/web/js/settings.js"), "utf8");
  const loader = source.slice(source.indexOf("async function loadSettings()"), source.indexOf("function drawDefaultApp()"));
  const elements = new Map();
  const byId = id => {
    if (!elements.has(id)) elements.set(id, {
      options: [], setAttribute() {}, append(option) { this.options.push(option); },
      replaceChildren(...options) { this.options = options; },
    });
    return elements.get(id);
  };
  const response = { language: "ru", languages: [
    { code: "en", name: "English" }, { code: "ru", name: "Russian" },
  ] };
  const context = vm.createContext({
    settings: {}, ask: async () => response, byId, make: (_tag, values) => values,
    ZONE_NOT_SET: "unset", setLanguage() {}, showZone() {}, showWifiNetworks() {},
    drawClock() {}, drawDefaultApp() {}, drawStatus() {}, drawTiles() {}, t: text => text,
  });
  vm.runInContext(loader, context);
  await context.loadSettings();
  assert.deepEqual(byId("language-choice").options.map(option => option.value), ["en", "ru"]);
  assert.equal(byId("language-choice").value, "ru");
  // A later firmware can supply another choice without changing the browser code.
  response.language = "xx";
  response.languages = [{ code: "xx", name: "Firmware choice" }];
  await context.loadSettings();
  assert.equal(byId("language-choice").options.length, 1);
  assert.equal(byId("language-choice").options[0].textContent, "Firmware choice");
  assert.equal(byId("language-choice").value, "xx");
};
