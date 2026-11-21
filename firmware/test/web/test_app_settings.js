// SPDX-License-Identifier: MIT
// An application's settings form: a value saved empty opens empty, a setting with nothing stored shows its default.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const core = read("core.js"), apps = read("apps.js");
const between = (text, from, to) => text.slice(text.indexOf(from), text.indexOf(to, text.indexOf(from)));

/* An element as far as the form's code asks of one. */
function element(tag) {
  const node = {
    tag, children: [], dataset: {}, listeners: {}, className: "", value: "",
    append(...nodes) { node.children.push(...nodes.filter(child => typeof child !== "string")); },
    prepend(...nodes) { node.children.unshift(...nodes.filter(child => typeof child !== "string")); },
    replaceChildren(...nodes) { node.children = nodes; },
    addEventListener(event, handler) { node.listeners[event] = handler; },
    querySelectorAll(selector) {
      const found = [], wanted = selector.split(",").map(part => part.trim());
      const matches = child => wanted.some(part => part.startsWith(".")
        ? child.className.split(" ").includes(part.slice(1)) : child.tag === part);
      const walk = parent => parent.children.forEach(child => { if (matches(child)) found.push(child); walk(child); });
      walk(node);
      return found;
    },
  };
  return node;
}

const context = vm.createContext({
  document: { createElement: tag => element(tag) }, MANIFEST_SETTING_VALUE_MAX: 255, MANIFEST_LIST_ITEM_MAX: 63,
});
vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
vm.runInContext(between(core, "function editableList(", "\n}\n") + "\n}\n", context);
vm.runInContext(between(apps, "function placeField(", "let appInDialog"), context);

const app = {
  settings: [
    { key: "city", type: "text", default: "Paris" },
    { key: "units", type: "choice", options: ["metric", "imperial"], default: "metric" },
    { key: "feeds", type: "list", default: ["one", "two"] },
    { key: "server", type: "url", default: "http://192.168.1.10:8123" },
    { key: "label", type: "text", default: "Home" },
    { key: "tags", type: "list", default: ["a", "b"] },
  ],
  /* What GET /api/apps gives: the first three saved empty, the rest never saved. */
  values: { city: "", units: "", feeds: "" },
};
const field = key => context.settingField(app, app.settings.find(setting => setting.key === key))[1];
const items = box => [...box.values()]; /* an array of this realm, to compare */

assert.equal(field("city").value, "", "a text saved empty stays empty");
assert.equal(field("units").value, "", "a choice saved empty is no choice");
assert.deepEqual(items(field("feeds")), [], "a list saved empty has no items");
assert.equal(field("server").value, "http://192.168.1.10:8123", "nothing stored: the default");
assert.equal(field("label").value, "Home");
assert.deepEqual(items(field("tags")), ["a", "b"]);

/* A value stored is shown as it is. */
app.values.label = "Office";
assert.equal(field("label").value, "Office");

/* A setting named as an object's own property is still a setting. */
const odd = { settings: [{ key: "constructor", type: "text", default: "Set" }], values: {} };
assert.equal(context.settingField(odd, odd.settings[0])[1].value, "Set");
console.log("app settings: ok");
