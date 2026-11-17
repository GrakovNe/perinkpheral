// SPDX-License-Identifier: MIT
// The public default is prefilled; a private custom link stays write-only.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../../main/web/js/apps.js'), 'utf8');
const start = source.indexOf('function settingField(app, setting)');
const end = source.indexOf('\nlet appInDialog', start);
const context = {
  MANIFEST_SETTING_VALUE_MAX: 255,
  make(tag, properties) {
    return Object.assign({ tag, dataset: {}, handlers: {},
      addEventListener(name, callback) { this.handlers[name] = callback; },
      append() {}, prepend() {} }, properties);
  }
};
vm.createContext(context);
vm.runInContext(source.slice(start, end), context);
const setting = { key: 'page1', type: 'link', title: 'Page 1', default: 'https://example.com/status.json' };
let parts = context.settingField({ values: { page1: setting.default } }, setting);
assert.equal(parts[1].value, setting.default);
assert.equal(parts[1].dataset.changed, '1');
parts[2].handlers.click();
assert.equal(parts[1].value, '');
assert.equal(parts[1].dataset.changed, '1');
parts = context.settingField({ values: { page1: true } }, setting);
assert.equal(parts[1].value, undefined); // no private address in the form
assert.equal(parts[1].dataset.changed, '0');
parts[2].handlers.click();
assert.equal(parts[1].value, '');
assert.equal(parts[1].dataset.changed, '1');
parts = context.settingField({ values: { page1: false } }, setting);
assert.equal(parts[1].dataset.changed, '0');
assert.notEqual(parts[1].value, setting.default); // explicit empty never resurrects a default
console.log('Default links: public preset, private override and removal passed');
