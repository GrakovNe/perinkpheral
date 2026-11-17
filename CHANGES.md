# Changes

What each release of the firmware brings, newest first. Every push to `main` is a release; the
section of its version, when written, is the release's notes on GitHub, in the web flasher and on
the device's page - without one, the notes are the commits since the release before.

## 0.1.14

- The device's page shows what a release brings when it offers the update.
- Every push is built: the firmware for release and for debugging, the host tests, the
  applications in the simulator against pictures that were looked at and kept, and the size of
  the image against the last build of `main`.
- Every push to `main` is a release, its version raised by itself.
- The web flasher writes any release, for release or for debugging.
- The tools are all Java: the project has no Python of its own. `tools/perinkpheral console`
  reads the device's console over USB.

## 0.1.13 - 2026-11-17

- The host's settings are a store by key. Settings on the device and the device's page write the
  same keys by the same rules, and any application with system access may too
  (`sys_setting_get`, `sys_setting_set`, `sys_setting_range` replace `sys_number_*` and
  `sys_text_set`).
- An application cannot remove itself.
- The mark is six cells by six; the icons of no signal, thunder and play are drawn anew, and info,
  gauge and battery are rounder and plainer.
- A notice's icon stands by what is drawn of it, in the middle of the text.
- The device's page: an update the firmware is too old for says so on the application's tile;
  on a phone the tabs of Settings are one row.
- An install the firmware is too old for is answered 409.

## 0.1.12 - 2026-11-16

- Moving a row of the menu: UP, OK and DOWN along the bottom, no words; a long BACK puts the row
  back. An application removed forgets its place in the menu.

## 0.1.9 to 0.1.11 - 2026-11-16

- The menu's order is yours: a long press of OK takes a row, UP and DOWN move it, OK keeps the
  order, BACK puts it back. Settings and About move too, and the page shows its tiles in the same
  order.

## 0.1.8 - 2026-11-16

- An application says the oldest firmware it runs on, worked out from the calls it makes; the
  device does not start or install one made for a newer firmware.
- Jersey has Cyrillic, every letter at every size.
- The icons are drawn by hand at eleven cells; text is centred by its ink.
- The device's page has Control: the screen and its buttons.

## 0.1.7 - 2026-11-16

- The device's page in the portal's manner: Departure Mono, the mark, tiles with icons, the screen
  in a frame of wood, one column on a phone.

## 0.1.6 - 2026-11-16

- What every application wrote for itself is in the SDK's library: dates, paces, places, the
  short weekday.
