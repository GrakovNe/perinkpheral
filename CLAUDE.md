# perINKpheral

A desk e-paper companion on an ESP32-C6: a screen and four buttons, it lives on a shelf and shows "what you glance at". The architecture is Pebble's: an unchanging host firmware plus applications in wasm that are installed from a catalog without reflashing.

The name is written `perinkpheral` in code, the repository and the domain; `perINKpheral` in the README, the logo and headings. The pun: peripheral vision + peripheral device + ink.

This file is the one specification: vision, architecture, conventions, milestones and the current state. Next to it are only references: `AGENTS.md` - the bench, build and flash recipes, what must not be touched in the driver; `hardware/README.md` - everything found out about the panel, with the datasheet and links. Where they disagree, this file wins.

**Everything in this project is written strictly in English**: code, comments, commits, documentation, this file, web pages, strings on the device. **Version 1 is ASCII only**: Cyrillic (fonts, strings, text from applications) is out of scope and comes back as a separate task in version 2 - do not design for it or build on it now.

## Vision and philosophy

**Vision.** A separate screen for the desk that is not a second monitor: it can be ignored until it is needed and read out of the corner of the eye - a line of the schedule, the current track. In one phrase: a TRMNL without the server and with buttons. The goal is an extensible operating system with its own applets on hardware anyone can repeat, and a real following among people who build things.

**Principles**, in the order they win arguments:
1. **Peripheral, not a monitor.** No backlight, nothing that moves, nothing that asks for attention. The device is silent until it is looked at, and what it shows is readable at a glance. This is why it is e-ink and why the refresh policy is conservative.
2. **Not a thin client.** The device does the work itself: it fetches the data, runs the application and draws the frame. No server prepares pictures for it, the vendor's or the user's own.
3. **The user's data stays the user's.** Showing a calendar must not require binding an account to a machine the user does not own. Requests go from the device straight to the source; there is no cloud of ours in between and there never will be. It is also simply faster than a trip around half the globe.
4. **It has buttons.** A companion is something you can answer, not a picture frame: applications react to presses, and the user is always one long press away from taking the screen back.
5. **Autonomous.** It runs from its battery for months and charges over USB-C; it does not need a cable, a hub or a computer next to it.
6. **Cheap to repeat.** The most common DIY parts, no custom PCB, a case simple enough to print in any plastic on any printer. If a choice makes the device harder to build at home, it needs a very good reason.
7. **The user decides what is on the screen.** Not a fixed set of dashboards: an SDK and applets that anyone can write and install without reflashing. The host stays small and unchanging so that everything interesting can live in applications.

What follows from this: no backend of our own, no accounts, no telemetry; a static catalog instead of a store service; TRMNL compatibility as one application, not as the platform; everything open under MIT.

## Hardware (fixed, assembled, verified)

### BOM
| Item | Part | Notes |
|---|---|---|
| MCU | Seeed Studio XIAO ESP32-C6 | ESP32-C6FH4 rev v0.2, RISC-V 160 MHz + LP core, 512 KB SRAM, 4 MB flash, **no PSRAM**; Wi-Fi 6 2.4 GHz, BLE 5; Li-Ion charger on board; ceramic antenna + U.FL |
| Display | Waveshare 4.2" e-Paper Module | 400×300, 1 bit, SPI; V2 panel (sticker), controller **SSD1683** - confirmed; module board Rev 2.2, jumper on 4-line SPI |
| Buttons | "Independent key switch module 4P", press output low | 4 tactile buttons, pull-ups and an LED per button on the board; outputs 1-4 are active low; **power LED removed** |
| Power | USB-C | charging + operation; VBUS on the 5V pin only with USB plugged in |
| Battery | Li-Ion 1S 3.7 V with a protection board (PCM), 4000 mAh | **connected**; on the BAT+/BAT- pads under the XIAO; the protection is mandatory - the board does not cut off over-discharge; the charge current is not documented by Seeed |

The page with the hardware, assembly and case renders is `hardware/bom/index.html`; the case model is `hardware/step/perinkpheral.step`; the renders are in `hardware/render/` (not in git).

### Wiring
```
Display → XIAO
  VCC  → 3V3
  GND  → GND
  DIN  → D10 (GPIO18, MOSI)
  CLK  → D8  (GPIO19, SCK)
  CS   → D7  (GPIO17)
  DC   → D5  (GPIO23)
  RST  → D4  (GPIO22)
  BUSY → D6  (GPIO16)

Button module → XIAO
  V → 3V3
  G → GND
  1 → D0 (GPIO0)
  2 → D1 (GPIO1)
  3 → D2 (GPIO2)
  4 → D3 (GPIO21)

Battery → XIAO
  BAT+ → BAT+ pad (bottom, next to "D5")
  BAT- → BAT- pad (bottom, next to "D8")

Free: D9 (GPIO20, MISO - the display does not need it).
```
The wiring is fixed and done - do not change it. Battery voltage and USB presence are **not measured and not detected** - decided, do not propose dividers or ADC. The power profile is a host setting.

### Board specifics worth knowing
- Not on the header and not to be touched: GPIO9 (BOOT, strapping), GPIO15 (user LED), GPIO12/13 (USB D-/D+), GPIO3 and GPIO14 (the antenna switch - driven only by `components/board`).
- Antenna: at start GPIO3 → LOW (enables the RF switch), GPIO14 → LOW (ceramic) or HIGH (external U.FL). Without this Wi-Fi may work poorly. Done by `components/board`, the choice is `APP_ANTENNA_EXTERNAL` in menuconfig.
- GPIO16/GPIO17 are the chip's UART0 TX/RX, here they are the display's BUSY and CS. The console is on USB-JTAG only. The ROM bootloader still prints its boot log into GPIO16 at every reset (tens of ms) - the panel survives it.
- GPIO0-2 are the LP group: the buttons are on them, the display stays off them.
- Wake-up from deep sleep on the C6 is ext1 on GPIO0-7 only: buttons 1-3 wake the device, **button 4 (GPIO21) does not**. From light sleep any of the four wakes it (`gpio_wakeup_enable`).
- Flashing over the native USB Serial/JTAG (`/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS), `idf.py flash`. Entering the bootloader: hold BOOT while powering up or resetting.
- 3V3 gives up to ~700 mA for everything. On battery the 5V pin is empty.
- Over-discharge protection is entirely the battery's PCM. Charge level and power source are not shown on the screen - there is no data.
- Charge LED: blinks while charging, off when full. The CHRG signal is not routed to a GPIO.

## Stack
- ESP-IDF 5.5, C. No Arduino, ESPHome, LVGL, GxEPD2.
- Application runtime: **wasm3** v0.9.0 (interpreter, sandbox). Applications are built with wasi-sdk (found through `WASI_SDK_PATH`; verified with 34); the firmware build needs it too, since the image carries applications.
- Fonts: the square one (code, `font_square.c`) for the logo; Departure Mono, a monospaced pixel font, as bitmaps from `tools/fontgen` (22 px) for everything else. ASCII only in version 1.
- FS: LittleFS (partition `apps`, 1 MB). NVS for the applications' `kv_*` and the host settings.
- Network: `esp_http_client` (TLS through mbedTLS), `esp_http_server` for the web UI.
- The display driver is our own, on top of `spi_master`: init, full refresh, partial refresh, panel deep sleep after every update. **SPI 2 MHz**, mode 0, CS toggled per byte - otherwise the refresh never finishes (see `AGENTS.md`).
- Buttons: `gpio_config` input without the internal pull-up (the pull-up is on the module), active LOW, debounce 20 ms, short press / long press (≥600 ms).

## Memory budget (the hard limit of the C6)
- SRAM 512 KB, about 250-300 KB free after Wi-Fi + TLS. There is no PSRAM and there will be none.
- Flash 4 MB: two OTA slots of 0x170000 (~1.44 MB) + LittleFS 1 MB (applications, fonts, gzipped web UI). A firmware larger than a slot means no OTA. The partition table is fixed - it cannot be changed over the air.
- A wasm application's arena is **exactly 64 KB** (one wasm page, there is nothing smaller), fixed for everyone. Build: `-Wl,--initial-memory=65536 -Wl,--max-memory=65536 -Wl,-z,stack-size=8192` (`tools/mkapp` does it); the interpreter is built with a limit of one page, so a module that asks for more does not load and none can grow.
- **One** wasm instance in memory at any moment; measured on the device, a loaded application costs 77 KB (the 64 KB page plus the interpreter) and all of it comes back when it leaves the screen. Free heap without the network: 391 KB idle, 314 KB with an application on the screen. Delivering data to an application that is not on the scene = unload the scene → load the owner → deliver → bring the scene back (100-300 ms).
- An HTTP response body goes to the application as a stream, in 1-2 KB chunks, while TLS is open; the host never holds it whole. Peak budget: TLS ~45 KB + one instance ~80 KB + the host.
- Two frames of 400×300×1 bit = 15 KB each: the back buffer the host draws into and the display service's copy of what the panel shows.

## Architecture

### Application
`/apps/<name>/app.wasm` + `/apps/<name>/manifest.json` on the LittleFS partition. The host reads `name`, `title`, `version`, `abi` and `fullscreen` today and skips what it does not know yet, so a manifest with `polls` and `hosts` already loads. `name` is 1-15 of `a-z`, `0-9`, `-`, `_`: it is the folder, the key in the catalog and the NVS namespace of the settings, and it must be the folder's name. `title` is at most 23 bytes. The module is at most 48 KB. The applications in `sdk/apps` are built and packed into the partition image with the firmware (`APP_BUNDLE_APPS`). The full manifest, as it will be:
```json
{
  "name": "plex",
  "version": "0.1.0",
  "title": "Plex Now Playing",
  "polls": [
    { "id": "now", "url": "http://192.168.1.10:32400/status/sessions",
      "headers": { "Accept": "application/json", "X-Plex-Token": "{{token}}" },
      "every": 15 }
  ],
  "hosts": ["192.168.1.10"],
  "fullscreen": false,
  "settings": [
    { "key": "token", "type": "secret", "title": "Plex token" }
  ],
  "abi": 1
}
```
User settings (`settings`) are edited in the web UI, stored in NVS, substituted by the host into URLs/headers (`{{key}}`) and available to the application through `kv_get`.

### Execution model
- **Scene** - one application, it owns the screen and the buttons.
- **Polling** - the host itself fetches every `polls` entry of every installed application at its `every`. This is the background mechanism ("pushes").
- **Raw requests** - the application on the scene may itself call `http_request` (method, URL, headers, body) - for OAuth, calendars, actions on a button. Requests go into the host's queue, one at a time; only hosts from the manifest's `hosts` are allowed. The response arrives through the same `app_on_data`.
- **Delivery** - the host calls `app_on_data(req_id, ptr, len, flags)` in chunks (flags: FIRST, LAST, ERROR; the HTTP status is in the first chunk) on the owning application, **whether it is on the scene or not**:
  - on the scene - the call is made right away;
  - not on the scene - unload the scene (`app_on_blur`) → load the owner → deliver → if it called `request_focus()` it stays as the scene, otherwise unload it and bring the previous scene back (`app_on_focus`).
- **State** - every application must restore itself from `kv_*`: the host unloads it on every scene change and background delivery. The framebuffer is not touched during the swap (the screen is not redrawn); the host remembers the scene's timers and restarts them after the return.
- **Buttons** - the layout is the same everywhere in the host: 1 up, 2 ok, 3 down, 4 back. Applications are encouraged to keep it.
- **Switching** - long press on button 4 → the host menu (list of applications, selection with buttons 1/3, confirmation with 2, button 4 goes back to where the menu was opened from). All other button events go to the application on the scene.
- **Preemption** - `request_focus()` from an application that received data in the background. Protection against a war for the screen: no more than one preemption per N seconds (30 by default), priority from the manifest on a conflict, a long press on button 4 always returns control to the user.

### Host kernel
The host is layers, and each knows only the ones under it:

| Layer | Where | What it does |
|---|---|---|
| Drivers | `components/epd` (`epd.c`), `components/input`, `components/board` | the panel, the buttons, the antenna switch |
| Display service | `components/epd` (`display.c`, `refresh_policy.c`) | keeps a copy of the frame the panel shows; given a new frame it leaves the panel alone if nothing changed, refreshes it, or says "too soon" |
| Graphics | `components/gfx` | draws on a surface: the whole frame or a viewport into it with its own origin and clipping |
| Window manager | `components/ui` (`wm.c`) | which scene has the screen, how the user moves between scenes, how a frame is put together from the header and the scene's picture |
| Scenes | `main/scene_*.c`, later the wasm application | tie a model to a view and decide what the buttons do, all behind one interface (`scene.h`): enter, leave, button, draw |
| Views | `main/screens.c` | a struct in, pixels out; no hardware, rendered by `tools/preview` |
| Services | `main/sysinfo.c`, `main/status.c`, settings; later network, time, polling | the data, with no idea that a screen exists |

- **Two frames.** The back buffer (`host.c`) is where frames are put together; the display service holds the front copy of what the panel shows. Drawing never touches the panel, an unchanged frame costs no refresh, and a scene cannot spoil the screen halfway.
- **Layers are areas of one frame**, not buffers of their own: the header and the content do not overlap. A scene that is not full-screen gets a viewport under the header and cannot draw outside it.
- **A scene never refreshes anything.** It reports that its picture changed and draws when asked; the window manager passes on why the frame changed (a switch, a press, data), and the display service decides what the panel does.
- **One event queue** (`events.c`). A press, later a timer or a response from the network, is posted from its own task and taken off by the host loop one at a time: nothing in the host runs concurrently with anything else.
- **The menu is a scene** like any other, not a layer over an application: opening it takes the screen away from whatever had it.
- **Development console** (`APP_DEV_CONSOLE`, on by default, off for a release): characters typed into the USB console press buttons and print the frame the panel shows; `tools/devshot` turns that into a PNG. This is how the device is driven and looked at without hands on it.

### Header
The host draws a header above its own screens and above every application that does not ask for the full screen:
- On the left the Wi-Fi icon in one of two states - connected, or not connected (the icon with a cross). It shows the outcome of the last connection attempt, which is what matters in the profiles where Wi-Fi is off between polls.
- On the right the date (`Wed 11 Nov`), blank while the time is unknown.
- Nothing else: no application title, no network name, no clock - the date changes once a day, so the header never asks for a refresh of its own. There is no footer.
- An application chooses in its manifest: `"fullscreen": false` (the default) leaves it 400×257 under the header, with its coordinates starting there, and it cannot draw over the header; `"fullscreen": true` gives it the whole 400×300. A long press on button 4 opens the menu in either mode.
- The network name and the address are on the device page of the menu, not in the header.

### Host API (wasm imports, a stable ABI after the first third-party application)
The only contract is `sdk/host_api.h`; applications and the host's import table are built from it, and `tools/mkapp` refuses a module that imports anything else. It freezes as ABI 1 at the end of milestone 2. What exists today:
```
gfx_width() / gfx_height()             // the surface: 400x300 full-screen, 400x257 under the header
gfx_text(x, y, font_id, str_ptr, len)  // y is the top of the line; returns the width drawn
gfx_text_width(font_id, str_ptr, len) -> px
gfx_font_height(font_id) -> px
gfx_rect(x, y, w, h, fill)
gfx_line(x0, y0, x1, y1)
gfx_bitmap(x, y, w, h, bits_ptr)       // a set bit is ink
request_redraw()                       // the picture is out of date
timer_set(id, ms)                      // one shot, 0 cancels; fires app_on_timer(id)
time_now() -> unix seconds             // 0 while the time is unknown
time_uptime_ms() -> ms since start
log(level, str, len)
```
Still to come with milestone 2: `http_request`, `request_focus`, `time_tz_offset`, `kv_get` / `kv_set` (NVS, namespace = application name).

There are no pointer callbacks. An application exports functions with fixed names (the host skips the missing ones):
```
app_init()
app_on_focus() / app_on_blur()
app_on_button(button 1..4, press)   // 2 short, 3 long; 0 down and 1 up are reserved
app_on_timer(id)
app_on_draw()                       // paint the whole picture from the state
app_on_data(req_id, ptr, len, flags)   // milestone 2
```
- **An application draws only inside `app_on_draw()`.** Handlers change the state and call `request_redraw()`; the host calls `app_on_draw()` when it needs the frame, with a white surface, and the `gfx_*` calls answer with an error anywhere else. The host puts the frame together, compares it with what the panel shows and decides whether and how the panel is refreshed - so there is no `gfx_flush`, no partial-or-full choice in the application's hands, and an application cannot leave a half-drawn screen.
- **The module is not trusted.** One that imports anything outside the host API is refused before it runs. Every pointer and length it passes is checked against its 64 KB before the host touches it, and coordinates and sizes beyond a few thousand pixels are refused, so that no call can cost more than the surface is large. A handler that traps stops the application, and the screen says so.
- **No C library.** Applications are linked with `-nostdlib` against `sdk/lib`, a few hundred bytes of string building and the `memcpy` family; `printf` alone would take a quarter of the 64 KB. `hello` is 878 bytes.
- Time: SNTP on every Wi-Fi connection, the time zone is a host setting.

### E-paper policy
Implemented in `components/epd/refresh_policy.c` (pure, host-tested). **The policy's numbers are user settings** (host, web UI, NVS); today they are still in menuconfig, the defaults start from Waveshare's recommendations for the 4.2" V2:
- data-driven updates (`app_on_data`, a timer, the clock) - no more often than once per **60 s**, counted from the start of a refresh, so that a clock can show minutes. Waveshare's own advice is 180 s and the panel is rated for a million refreshes; a user who wants the panel to last longer raises the number;
- reaction to a button - the user is looking at the device - **at once**, no pause by default; the panel's own partial refresh (about a second) is the only limit;
- full refresh - after **5** partial ones;
- at least one update per **24 hours** (the host's keep-alive task).

Not configurable:
- Full refresh when the application on the scene changes; the first update after start is always full.
- After every update the panel goes to deep sleep (mode 1, RAM is kept), otherwise it degrades irreversibly.
- No animations and no real-time progress bars.
- 1 bit per pixel. Fonts are bitmap, pre-converted (`tools/fontgen`), ASCII; the readable minimum is ~10 px.

### Web UI (on the device, `esp_http_server`)
- `GET /` - static files (gzip in LittleFS, no frameworks, target ≤150 KB).
- `GET /api/apps` - installed ones; `POST /api/apps/install {url}` - download `.wasm` + `manifest.json` from the catalog; `DELETE /api/apps/<name>`.
- `GET/PUT /api/apps/<name>/settings` - by the `settings` schema from the manifest.
- `GET /api/catalog` - proxies the catalog's static JSON (the catalog URL is a host setting).
- `GET /api/log` - the log ring buffer.
- `POST /api/focus/<name>` - bring onto the scene.
- `POST /api/ota` - upload a firmware image into the spare OTA slot and reboot into it; the bootloader rolls back if the new image does not confirm itself.
- Host settings (power profile, e-paper policy, time zone, catalog URL) are edited here too.

The catalog is static files (GitHub Pages/Releases): `catalog.json` + `.wasm`/`manifest.json` by URL, no server.

### Power and profiles
The battery is connected. There is no USB detector and there will be none, so the profile is the host setting `power_profile: plugged | battery`, `plugged` by default; "extreme power saving" is a separate setting on top of `battery`.
- **plugged** - Wi-Fi always on, the web UI available, any `every`, polls on schedule.
- **battery** - **light sleep** between polls: memory, application state and the framebuffer are kept; wake-up by timer and by any of the four buttons (`gpio_wakeup_enable`). Wi-Fi comes up for the poll and goes down; the web UI is available for 2 minutes after a button press. The minimum `every` is 300 s, anything smaller is rounded up. The board's current in light sleep has not been measured - measure it before any promises about battery life.
- **extreme power saving** - off by default: **deep sleep** between polls. Every wake-up is a cold start: the application is loaded anew and lives only from `kv_*`, timers do not survive the sleep, the first draw after wake-up is a full refresh. Only buttons 1-3 (GPIO0-2) wake the device; button 4 (GPIO21) does not wake it from deep sleep - the menu is available after waking with any other. BSSID/channel are kept in RTC memory for a fast reconnect.
- Estimate for deep sleep, 4000 mAh and 85 % usable capacity (wake-up ~3 s at ~85 mA, ~50 µA asleep): a poll every 15 min ≈ 1 year, 5 min ≈ 4.5 months, 1 min ≈ 4 weeks. There are no numbers for light sleep until it is measured. Wi-Fi always on while on battery is hours, not days.

### TRMNL compatibility - as an application, not as a platform
TRMNL is a thin client: once per `refresh_rate` the device does `GET /api/display`, receives JSON with an `image_url`, downloads a PNG/JPEG/BMP and draws it; the protocol has no interactivity (one button, its action is set by the server through `special_function`); 4.2" is not in the list of models. So TRMNL is not adopted as a whole. Instead there is the `trmnl` application:
- `polls`: `GET {{server}}/api/display` with the headers `ID` (MAC), `Access-Token`, `FW-Version`, `Width: 400`, `Height: 300`, `Battery-Voltage`, `RSSI`; `every` is taken from the response's `refresh_rate`. We do not measure the battery voltage - `Battery-Voltage` carries a constant.
- `app_on_data`: parses `image_url`/`filename` (cache by filename - do not download the same thing again), downloads the picture (a 1-bit 400×300 BMP preferably; PNG only if a decoder fits into the arena), draws it with `gfx_bitmap`.
- Works with any BYOS server (Terminus, byos_fastapi and others) - they read `Width`/`Height` from the headers and render for 400×300. The TRMNL cloud is not a goal (BYOD license, 800×480).
- Gives access to the TRMNL plugin ecosystem with no changes in the host.

## Repository layout
```
firmware/              the ESP-IDF project (build from this folder)
  main/                entry point, host loop, event queue, scenes, views (screens), services, settings (NVS)
  components/
    appconf/           the host settings struct, defaults, validate (pure)
    board/             what is not on the header: the antenna switch
    epd/               SSD1683 driver + display service + partial/full policy
    gfx/               primitives, the square font, Departure Mono bitmaps (fonts/) (pure)
    input/             buttons, debounce, short/long press
    ui/                scene interface and the window manager (pure)
    power/             plugged/battery/extreme profiles, light and deep sleep, wake-up sources (milestone 4)
    appsys/            one wasm application in memory, the host API as it sees it, the application as a scene, manifests and the store of installed applications (pure)
    wasm3/             the interpreter, v0.9.0, vendored unmodified (MIT)
    poller/            polls schedule, http_request queue, HTTP client, streamed app_on_data delivery (milestone 2)
    store/             catalog, install/remove in LittleFS, settings in NVS (milestone 5)
    web/               esp_http_server, API, OTA upload, static files (milestone 2/5)
  test/host/           host tests of the pure modules (tinytest)
sdk/
  host_api.h           the only contract; applications and the host's import table are built from it
  lib/                 what applications link instead of a C library
  apps/
    hello/             sample: text + reaction to a button
    clock/             second sample, to check switching
    plex/              the first real one: now playing + play/pause/next
    trmnl/             TRMNL BYOS client: polls /api/display, parses JSON, downloads the picture, draws with gfx_bitmap
tools/
  fontgen/             TTF → bitmap (ASCII)
  preview/             renders the host's views to PNG on the development machine - look before flashing
  devshot/             presses buttons on a real device over USB and saves the frame its panel shows
  mkapp/               builds app.wasm (wasi-sdk) and checks its imports and memory
  catalog/             generates catalog.json (milestone 5)
assets/
  fonts/               DepartureMono-Regular.otf (OFL), its license
hardware/              everything about the physical device
  README.md            panel, datasheet, bench notes
  bom/                 the "hardware and assembly" page (links into render/ and step/)
  step/                the STEP model of the case
  render/              case renders from Blender (not in git)
```
`power`, `poller`, `store`, `web`, the `plex` and `trmnl` applications and `tools/catalog` do not exist yet.

## Milestones
1. **Applications without the network.** The host loads `hello.wasm` from LittleFS; the application draws text and reacts to a button; `clock.wasm` is switched to through a minimal host menu (long press 4). A check of running on battery without USB. Host API of the milestone: `gfx_clear`, `gfx_text`, `gfx_rect`, `gfx_bitmap`, `gfx_text_width`, `gfx_flush`, `timer_set`, `time_now`, `log`; exports `app_init`, `app_on_button`, `app_on_timer`, `app_on_focus`, `app_on_blur`. Start with `sdk/host_api.h` and `components/appsys`.
2. **Drivers and the SDK.** Everything the host offers an application, finished and frozen as ABI 1, each piece with its side of `host_api.h`:
   - **Wi-Fi** - provisioning through the web UI in AP mode on first start, reconnects, SNTP. **OTA comes right after it**: the firmware is uploaded to the device over the network (`POST /api/ota`, the image goes into the spare slot). An image that is not a perinkpheral build is refused before it is written; a new image is on probation until the network and the web server are up, and is rolled back if they do not come up, so that from this point on nobody needs the cable to flash.
   - **Screen** - the whole `gfx_*` set with fonts by id, `gfx_line`, `gfx_font_height`, the flush policy as seen by an application.
   - **Buttons** - down/up events next to short/long.
   - **Host services** - `kv_*`, `time_now`/`time_tz_offset`, timers with ids, `log` and the log ring buffer, `poller`, `http_request`, streamed `app_on_data`, `request_focus` and the scene swap on background delivery.
   - **SDK** - `tools/mkapp`, the samples, and the first real application, `plex`, as the proof that the API is enough.
3. **Host screens.** The operating system's own interface, designed and drawn: the menu and application list, the first-start and Wi-Fi setup screens, status and error screens, what the device shows while it updates or has nothing to show.
4. **Power.** The `battery` profile on light sleep (wake-up by timer and button, rounding of `every`, switching profiles by the setting), measured current and real battery life numbers, then "extreme power saving" on deep sleep.
5. **Web UI and catalog.** Install from `catalog.json`, application and host settings (including the e-paper policy), the log, firmware updates pulled from releases (`esp_https_ota`).

## State
Done and verified on hardware: the display driver (full refresh 2.19 s of BUSY, partial 0.41 s), buttons with short and long presses, the refresh policy, drawing and fonts, the host kernel (display service with two frames, window manager, scenes, one event queue) with built-in scenes - the menu, a device page and the splash - and wasm applications installed on LittleFS: the menu lists what is there, `hello` counts presses and `clock` redraws on a timer once a minute, host tests of the pure modules.

Open:
- An application that loops forever in a handler hangs the host: wasm3 runs it to completion on the host task. It needs a budget of instructions or a watchdog that stops the application, not the device.
- The host task has 8 KB of stack and applications run on it; `hello` leaves 6.2 KB. Watch it (`m` in the development console) as applications grow.
- The e-paper policy numbers are in menuconfig; move them into the host settings (`appconf` + NVS).
- There are no down/up button events yet, only short/long.
- `esp_ota_mark_app_valid_cancel_rollback()` is called right after the hardware is up; with OTA it moves into the probation check, so that an image without a working network rolls back.
- The version on the splash is hard-coded as `ver 1.234.5` - a prototype string, not the build's version.
- `hardware/render` is a symlink to `render/` in the repository root while Blender is still writing there by an absolute path; when the renders are done, replace the link with the folder itself.

Milestone 1 is done on USB power; what is left of it needs hands: a check on battery without USB. Next - milestone 2, starting with Wi-Fi and OTA.

## Conventions
- Everything is written strictly in English, with a plain hyphen `-`, never an en or em dash.
- Nothing in the repository depends on one developer's machine: no home-directory paths, no fixed serial port names, no OS-specific steps in code, tools or documentation.
- SPDX header in every source file, C11. Logic that does not need hardware is pure and host-tested; `settings.c` is the only owner of the host's NVS.
- When debugging hardware: the vendor's reference code and the datasheet first, hypotheses after. Do not suggest "check the wires".

## Branding
- **The logo is the word `perINKpheraL` set in the device's square font: lower case `per` and `phera`, capital `INK` and a capital `L` at the end, all of one height in the same squares, with no plate, frames or dots.** The mix of capitals and lower case inside one word is the mark itself. On the panel - black on white, scale 5 (27 px squares), 357 px.
- The square font: every glyph is a square on a grid of 3 strokes × 2 gaps (5+6+5+6+5 = 27 px), no ascenders or descenders, diagonals allowed. Implementation: `firmware/components/gfx/font_square.c`.
- Under the mark on the splash: `ver 1.234.5` (a prototype string, hard-coded), `(c) 2026 Max Grakov`, `MIT License`.
- **The square font is for the word `perINKpheraL` only.** All other text on the device (splash, menu, host applications) is set in **Departure Mono** (SIL OFL 1.1, https://github.com/rektdeckard/departure-mono), a monospaced pixel font. It is rasterized to bitmaps by `tools/fontgen` only at multiples of 11 px, where it lands on its own pixel grid: 22 for all text and 66 for large digits. It was chosen on the panel against Jersey 20, Jersey 25 and Terminus. **Jersey 25** (SIL OFL 1.1, heavier, proportional) comes later as an optional second font the user can pick in the host settings; it is not in the firmware yet. The file is `assets/fonts/DepartureMono-Regular.otf` with `OFL-DepartureMono.txt`. Outline fonts rasterized to 1 bit (Tektur was the first try) come out thin and ragged at this pixel density - use fonts drawn for the pixel grid. Fonts are OFL/CC0 only.
- Palette: black #141413 and paper #E9E7E1 (on the device - the panel's native grey-white). Every host screen is black on white. No halftones, dithering or gradients.
- Do not propose: a plate or frame around `INK`, dithered letters, white on black, decorations on the splash, other typefaces.

## Do not
- Do not pull in Arduino, ESPHome, LVGL, GxEPD2, Home Assistant.
- Do not give applications sockets: the network is only `polls` and `http_request` in the host's queue, by the `hosts` list.
- Do not build a declarative JSON layout into the core (it can be one of the applications).
- Do not keep more than one wasm instance in memory or more than one TLS connection.
- Do not use pointer callbacks in the ABI - named exports only.
- Do not change `host_api.h` signatures without an ABI version in the manifest.
- Do not design for PSRAM: there is none.
- Do not measure the battery voltage and do not detect USB - decided.
- Do not "optimize" the panel driver: CS per byte, one SPI device, bounded BUSY waits.
