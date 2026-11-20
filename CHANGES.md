# Changes

Firmware release notes, newest first. Each successful push to `main` publishes a release.
CI uses the matching version section for its notes; otherwise it lists commits since
the previous release. Notes appear on GitHub, in the flasher and on the device's page.

## 0.1.65

- The setup access point names its page in the DHCP lease (option 114), for phones that
  read it.

## 0.1.64

- A long press of BACK opens the menu when the button is released. BACK can be held for
  any time before UP or DOWN switches applications; releasing it after a switch does nothing.
- In the menu BACK with UP or DOWN no longer opens an application.
- An application left with BACK and UP or DOWN is as it was when it is switched back to.
  A short press of BACK still opens the menu.
- The header shows a time set by hand, or a changed time zone, at once.

## 0.1.63

- Hold BACK and press UP or DOWN to open the previous or next application in the menu's
  order, without the menu. System pages are skipped.

## 0.1.62

- Redrawn icons: wind, mask and pollen (a flower), at every size, with one line weight.

## 0.1.61

- One session for where the user is: `session_set` / `session_get` keep up to 1024 bytes,
  allocated when first set. `scene_state_set` / `scene_state_get` are removed; ISS and Life
  use the session and need this firmware. Update them from the catalog after updating.
- `contract check` refuses a call that is not recorded in the ABI snapshot;
  `screen_set_inverted` is now recorded.
- "Check for updates" asks the Perinkpheral organization's repository.

## 0.1.57

- Offer English and Russian from the firmware's language list; the device page
  uses that list directly. Reject unsupported language settings without losing
  saved Wi-Fi when upgrading from a version that accepted them.

## 0.1.54

- Applications can invert their whole screen, including the header and bottom margin.

## 0.1.52

- Use Perinkpheral OS consistently in the SDK guide, architecture documentation
  and developer tools.

## 0.1.51

- Let applications read HTTP response headers for authentication with local services.
  Existing applications remain compatible.

## 0.1.50

- Queue individual app updates and Update all together, with immediate progress
  notices and a result for each app. Repeated clicks do not duplicate an install,
  and one failed update does not stop the remaining apps.
- Leaving the page drops updates that have not started; returning from the
  browser cache refreshes the list without replaying an installation.
- Describe perINKpheral as a specialized operating system for e-paper devices.

## 0.1.49

- Do not offer older stable app versions from a stale catalog as updates.
- Label the last restart cause as Reset reason.

## 0.1.48

- Show Update all whenever at least one app update is available.
- Raise the ESP locating stops by 0.5 mm and refresh the enclosure print files.

## 0.1.47

- Show installed app capacity as a thin bar below the heading.

## 0.1.46

- Fix the app auto-update switch reporting "Invalid value."
- Place the logging switch to the right of the log level.

## 0.1.45

- New app installations open on the device. Updates keep the current screen.
  Manual updates report each app and its installed version.
- Optional daily app updates run on the device without an open browser.
- App catalogs display authors. Perinkpheral is reserved for the official source.
- Network settings can scan nearby Wi-Fi networks. Recovery now serves a captive portal.
- Logging has its own settings tab, with an enable switch and a saved severity level.
- Device-page layout, language choices, file selection and dependency credits are corrected.
- Device Settings groups appearance options, wraps lists, and confirms restart and reset.
  About links to the project portal.

- Hide the OTA partition slot from the device page and About screen. The last-fault
  field shows only its cause.

- Limit Wi-Fi transmit power to 15 dBm for the next stability experiment. This is
  a mitigation under test, not a confirmed fix for the observed watchdog resets.
- Interrupted application updates stay hidden until their files are consistent.
  Storage failures preserve cached modules and settings awaiting migration.
- Concurrent list polls finish every item. Temporary memory pressure no longer
  causes a delayed restart after the heap has recovered.
- Wi-Fi accepts full-length network names and 64-character hexadecimal keys.
- Application builds refuse source folders as output; catalogs reject duplicate
  application names. IDE-generated headers are isolated by application path.
- SDK tools can publish an application catalog to the developer's own public
  GitHub repository with `publish <folder> --repo owner/repository`.
- Releases include enclosure files and the exact firmware symbols for crash reports.

## 0.1.38

- Normal firmware updates keep a single preparation screen until the confirmation
  question, without flashing access-point or device-page instructions in between.
- Manual recovery and failed startup show recovery immediately. Saved Wi-Fi gets
  time to connect before access-point instructions appear. A stalled update exposes
  connection help after 30 seconds.
- This screen fix changes both main and recovery. Existing devices need a USB
  recovery update; an ordinary OTA updates main only and cannot replace recovery.

## 0.1.35

- Independent recovery replaces the second full firmware slot. Main firmware now has
  2 MiB, an additional 576 KiB. Settings, applications and their storage keep their
  existing partition addresses and sizes.
- Existing devices require a one-time USB migration: choose Keep data in the web
  flasher. Ordinary OTA cannot change the bootloader or partition table. Press BACK
  after migration to start the installed firmware.
- Network updates enter recovery and ask for OK on the device. Recovery uses the same
  notices, icons, buttons and saved language as the main interface. It reads settings
  and installed glyphs without modifying their partitions.
- A failed or interrupted update can be replaced from recovery. There is no automatic
  return to a previous full firmware image. Hold BACK for three seconds during reset
  to enter recovery; its page is also available on the perink-recovery Wi-Fi network.
- The browser and CLI wait for the intended image to complete its startup checks.
  CLI upload connections are renewed across the recovery reboot.
- Large installed-app lists are streamed within the heap budget. Catalog downloads
  check TLS memory before starting, and malformed glyph bitmap lengths are rejected
  before alignment arithmetic.

## 0.1.34

- Applications can keep up to 1024 bytes of temporary scene state in RAM through
  `scene_state_set` / `scene_state_get`. This preserves game boards across background
  deliveries without flash writes; the existing 128-byte session remains independent.
- A settings change discards unfinished manifest poll responses from the previous
  settings and releases their background instance. Explicit HTTP requests retain
  their terminal callbacks.
- The gas icon uses clean geometric nodes and bonds at all three sizes.

## 0.1.30

- ABI 3 is the first frozen application contract. ABI 2 packages are refused with
  "Update the app." Reinstall the rebuilt catalog applications; their patch versions rise.
  Firmware and ABI versions remain independent; future firmware preserves ABI 3.
- Queued destinations are authorized again before sending, from the same application KV
  view that fills their headers. A settings change cannot send new credentials to a stale
  destination. Disallowed queued requests fail instead of moving to another server.
- System access exposes only the explicit settings surface of the web page. Internal and
  unknown keys return `HOST_ERR_NOT_FOUND`; `sdk/host/sys.h` documents every public key.
- Install/update notifications remain pending after a load failure and are consumed before
  entering a loaded callback, including one that traps. Package identity includes both
  module and manifest, independently of version-string ordering.
- Headers specify binary layouts, pointer lifetimes, buffer/error rules, streamed failures
  and rendering precedence. Calendar accepts its opening marker across arbitrary pieces.
- Tools check actual Wasm function types and an append-only recorded contract, and host
  tests run frozen Probe, Hello and Settings packages, translations and saved Hello state.
  The ABI 3 firmware floor is 0.1.30. The build takes the maximum of this floor,
  later introduction stamps and the manifest's explicit minimum. Fixture hashes are
  checked separately in `sdk/abi/3/fixtures.txt`; `sdk/abi/3.txt` records only the contract.
- Status exposes `supported_abis`. Install, Update and Update all use the same ABI and
  firmware compatibility check; catalog metadata comes from the packaged manifest.
- CI builds both guide PDFs and the glyph packs and attaches them to each release.
  It checks committed generated files, PDF text, guide limits, Russian translation
  completeness and the API route list.

## 0.1.26

- A setting may be `"hidden": true` in its manifest: written through the device API,
  left off the device page's form. HA Companion's server and token are, set by the Home
  Assistant integration.

## 0.1.25

- The Catalog shows the installed applications too: Installed, or Update when a source has
  another version. Installed apps has Update all when there is more than one update.
- Time: date and time follow synchronization while Network time is enabled; otherwise
  the fields are editable. The same form includes the time zone, time server and Save.
- "Factory reset" in Russian is "Сбросить к заводским настройкам".

## 0.1.23

- `answer_info(response, capacity)` copies a bounded prefix and returns the bytes written.
- `poll_set_interval` rejects values outside 10..86400 seconds; 0 forgets the code override
  and restores the manifest or setting. Storage failures are reported; an unchanged pace
  keeps its deadline. Code overrides remain in effect across restarts and settings saves.
- `sys_app_remove` queues one removal until the handler returns and refuses a second with
  `HOST_ERR_NO_SPACE`; success means queued, not completed.
- `time_from_local(parts, unix_time)` returns a status separately from the moment. Skipped
  local times move forward by the clock-change gap; repeated times use the earlier occurrence.
- `gfx_draw_icon` takes `(x, y, icon, cell)`, like the other drawing calls.
- `HOST_ALARM_REPEATING_MAX` names the device's 24 shared repeating-alarm slots. One
  application may use several; none are reserved per application.
- `screen_request` remains void: decided after delivery, refused within 30 seconds of a press,
  over a question, or within 30 seconds of the last grant. Refused requests are not retried.

## 0.1.21

- An application keeps where the user was in it - the page, the row, a value half set - when
  another application's data or a question of the device takes its place for a moment, and
  comes back to it. Settings, Calendar and HA do; it is gone when the application is left,
  opened anew from the menu, or the device restarts.
- A question is answered only by an OK pressed once it is on the screen: a press meant for the
  screen before it, or made while the panel was still drawing, no longer answers it.
- An application in the background no longer takes the screen within 30 seconds of a press, or
  over a question. After a long BACK, the notifications that wait show at the next press.
- Lists stand still while the selection moves inside them; a long UP or DOWN moves a page. In
  the date and time a long press steps far: minutes by ten, days by a week.
- Settings: power saving and the menu's timeout are chosen with Save and Cancel; the time zone's
  city and the default application from a list; Buttons says what holding a button does. The
  page's title in the header is no longer covered by another application's status.
- For developers: the contract is written by subject (`sdk/host/`), with `host_api.h` including
  it all; `session_set` / `session_get` and `app_session_save` / `app_session_load`;
  `HOST_REDRAW_QUESTION`; `app_draw_cells` draws cells in one size; `app_state_save` says
  whether it stored; a repeating alarm for every application the device holds.

## 0.1.20

- Fixes found by a review of the whole project. An application with system access that
  removes the one waiting under it no longer brings the device down; the removal takes effect
  once its handler returns.
- Data: incomplete JSON responses are rejected. Slow responses time out even while
  bytes continue arriving; lists that cannot fit return a memory error. `poll_run` is
  held to once a second, and a long list of one application lets the others' polls in.
- An alarm of another application in the middle of an answer taken in pieces ends it as a
  press does, and the poll is asked again soon.
- Installing: what a failed try left is cleared first; an application that writes files is
  held to its 32 KB; a server left at its default counts when the address is changed.
- Settings: Apps lists the applications in the order of their titles in the device's
  language; the date and time are set in one step.
- The device's page: Control's buttons no longer report an error; the log knows a restart
  and keeps its clock right; names of the device and the network stay as written; on a
  phone the facts and the file chooser fit the width; the build date and faults are said in
  the page's language.
- The development tools' Run opens the application even when another is the default.

## 0.1.19

- The time zone is chosen by a city on the device's page too, from the list the device has;
  "Custom" takes a POSIX string. Detect finds the browser's city by its zone's name.
- Firmware is a section of its own: what is installed, OTA with a newer release's notes on the
  screen, and Custom firmware. Device keeps what the device is, and restart and reset.
- New defaults: fast refresh on, the clock in the header on, and no full refresh but when the
  screen changes hands. A device keeps what was saved on it.
- An application may say what it does: `description` in its manifest, a line of up to 95
  characters, translated as its title is. The catalog shows it with the title in the page's
  language, and so does what an application is; every official application has one, in
  English and Russian.
- Installed apps and the Catalog are two sections: the catalog has a search over it, what the
  sources have in columns, and the sources' gear beside its name.
- The status says whether the firmware has the development tools: the page no longer asks a
  release for its screen, and its log no longer says "not found" for it.
- Custom firmware is one line: the file, and Install at its end.
- The log is a section of its own: a window as tall as the screen, a line a row with its time
  of day, a search, warnings and errors marked.
- The cities are in the device's language, on the device and on the page, sorted by the names
  shown; the city chosen is one setting, so both show the same one of those that share a zone.

## 0.1.18

- The device's page speaks the device's language - English or Russian - in the terms the device
  uses itself; the status's time is in it too.
- The page's firmware is two parts, OTA and Custom firmware. While an update is under way a
  screen over the page counts the time and says what is going on, finds the device when it is
  back and reads the page anew.
- About is Credits, as on the portal: the project, what the firmware is made with, the
  typefaces.
- The confirmation of a new firmware is logged by its number, not "UNKNOWN ERROR".

## 0.1.17

- The device's page answers while the device waits for OK on its screen, and while it installs
  an application or fetches a firmware: those requests wait on a task of their own. Another
  one meanwhile is answered "Device busy." at once.
- A server that sends its answer a byte at a time no longer keeps the device's one connection:
  values are read within a minute, an answer in pieces within five, or the application is told
  of a timeout.
- The device restarts after ten minutes of continuous memory refusals for requests.

## 0.1.16

- The device's page no longer stops answering after a while. The web server was promised
  seven connections of the device's ten sockets, where its own three, the setup name server's
  and a poll's left five: a browser that kept a few open left none for the next, and the
  oldest was never let go. The device has twelve sockets now and the page seven, the oldest
  closed for the next.

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

- Perinkpheral OS's settings are a store by key. Settings on the device and the device's page write the
  same keys by the same rules, and any application with system access may too
  (`sys_setting_get`, `sys_setting_set`, `sys_setting_range`).
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
