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
- Application runtime: **wasm3** (interpreter, sandbox). Applications are built with wasi-sdk 25 (found through `WASI_SDK_PATH`).
- Fonts: the square one (code, `font_square.c`) for the logo; Tektur as bitmaps from `tools/fontgen` (20 px labels, 24 px text, 64 px digits) for everything else. ASCII only in version 1.
- FS: LittleFS (partition `apps`, 1 MB). NVS for the applications' `kv_*` and the host settings.
- Network: `esp_http_client` (TLS through mbedTLS), `esp_http_server` for the web UI.
- The display driver is our own, on top of `spi_master`: init, full refresh, partial refresh, panel deep sleep after every update. **SPI 2 MHz**, mode 0, CS toggled per byte - otherwise the refresh never finishes (see `AGENTS.md`).
- Buttons: `gpio_config` input without the internal pull-up (the pull-up is on the module), active LOW, debounce 20 ms, short press / long press (≥600 ms).

## Memory budget (the hard limit of the C6)
- SRAM 512 KB, about 250-300 KB free after Wi-Fi + TLS. There is no PSRAM and there will be none.
- Flash 4 MB: two OTA slots of 0x170000 (~1.44 MB) + LittleFS 1 MB (applications, fonts, gzipped web UI). A firmware larger than a slot means no OTA. The partition table is fixed - it cannot be changed over the air.
- A wasm application's arena is **exactly 64 KB** (one wasm page, there is nothing smaller), fixed for everyone. Build: `-Wl,--initial-memory=65536 -Wl,--max-memory=65536 -Wl,-z,stack-size=8192`; the host does not load a module with any other memory size.
- **One** wasm instance in memory at any moment. Delivering data to an application that is not on the scene = unload the scene → load the owner → deliver → bring the scene back (100-300 ms).
- An HTTP response body goes to the application as a stream, in 1-2 KB chunks, while TLS is open; the host never holds it whole. Peak budget: TLS ~45 KB + one instance ~80 KB + the host.
- Framebuffer 400×300×1 bit = 15 KB, a single one, owned by the host (`components/epd/display.c`).

## Architecture

### Application
`/apps/<name>/app.wasm` + `/apps/<name>/manifest.json`:
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
- **Switching** - long press on button 4 → the host menu (list of applications, selection with buttons 1/3, confirmation with 2). All other button events go to the application on the scene.
- **Preemption** - `request_focus()` from an application that received data in the background. Protection against a war for the screen: no more than one preemption per N seconds (30 by default), priority from the manifest on a conflict, a long press on button 4 always returns control to the user.

### Host API (wasm imports, a stable ABI after the first third-party application)
The only contract is `sdk/host_api.h`; applications and the host's import table are built from it. Summary:
```
gfx_clear()
gfx_text(x, y, font_id, str_ptr, len)
gfx_rect(x, y, w, h, fill)
gfx_line(x0, y0, x1, y1)
gfx_bitmap(x, y, w, h, bits_ptr)
gfx_text_width(font_id, str_ptr, len) -> px
gfx_font_height(font_id) -> px
gfx_flush(mode)            // 0 = partial, 1 = full; the host may downgrade/upgrade/refuse by its policy
timer_set(id, ms)          // scene only; fires app_on_timer(id)
http_request(req_id, method, url, headers, body) // scene only; the response -> app_on_data
request_focus()            // from app_on_data; the host decides
time_now() -> unix seconds // 0 until SNTP has synchronized
time_tz_offset() -> seconds
kv_get(key, buf, cap) / kv_set(key, buf, len)   // NVS, namespace = application name
log(level, str, len)
```
There are no pointer callbacks. An application exports functions with fixed names (the host skips the missing ones):
```
app_init()
app_on_focus() / app_on_blur()
app_on_button(button 1..4, event)   // 0 down, 1 up, 2 short, 3 long
app_on_timer(id)
app_on_data(req_id, ptr, len, flags)
```
`gfx_*` off the scene is a no-op with an error code. Time: SNTP on every Wi-Fi connection, the time zone is a host setting.

### E-paper policy
Implemented in `components/epd/refresh_policy.c` (pure, host-tested). **The policy's numbers are user settings** (host, web UI, NVS); today they are still in menuconfig, the defaults are Waveshare's recommendations for the 4.2" V2:
- data-driven updates (`app_on_data`, the clock) - no more often than once per **180 s**;
- reaction to a button - the user is looking at the device - **2 s** after the previous update;
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
  main/                entry point, settings (NVS), reset, screens (host screens), demo (bench)
  components/
    appconf/           the host settings struct, defaults, validate (pure)
    board/             what is not on the header: the antenna switch
    epd/               SSD1683 driver + framebuffer + partial/full policy
    gfx/               primitives, the square font, Tektur bitmaps (fonts/) (pure)
    input/             buttons, debounce, short/long press, event queue
    power/             plugged/battery/extreme profiles, light and deep sleep, wake-up sources (milestone 3)
    appsys/            wasm3 loader, arena, lifecycle, host API, focus (milestone 1)
    poller/            polls schedule, http_request queue, HTTP client, streamed app_on_data delivery (milestone 2)
    store/             catalog, install/remove in LittleFS, settings in NVS (milestone 4)
    web/               esp_http_server, API, static files (milestone 2/4)
  test/host/           host tests of the pure modules (tinytest)
sdk/
  host_api.h           the only contract; applications and the host's import table are built from it
  apps/
    hello/             sample: text + reaction to a button
    clock/             second sample, to check switching
    plex/              the first real one: now playing + play/pause/next
    trmnl/             TRMNL BYOS client: polls /api/display, parses JSON, downloads the picture, draws with gfx_bitmap
tools/
  fontgen/             TTF → bitmap (ASCII)
  preview/             renders the host's screens to PNG on the development machine - look before flashing
  mkapp/               builds .wasm (wasi-sdk/clang) + validates the manifest (milestone 1)
  catalog/             generates catalog.json (milestone 4)
assets/
  fonts/               Tektur-VF.ttf (OFL), its license
hardware/              everything about the physical device
  README.md            panel, datasheet, bench notes
  bom/                 the "hardware and assembly" page (links into render/ and step/)
  step/                the STEP model of the case
  render/              case renders from Blender (not in git)
```
`power`, `appsys`, `poller`, `store`, `web`, `sdk/apps/*`, `tools/mkapp`, `tools/catalog` do not exist yet.

## Milestones
1. **No network, no store.** The host loads `hello.wasm` from LittleFS; the application draws text and reacts to a button; `clock.wasm` is switched to through the menu (long press 4). A check of running on battery without USB. Host API of the milestone: `gfx_clear`, `gfx_text`, `gfx_rect`, `gfx_bitmap`, `gfx_text_width`, `gfx_flush`, `timer_set`, `time_now`, `log`; exports `app_init`, `app_on_button`, `app_on_timer`, `app_on_focus`, `app_on_blur`. Start with `sdk/host_api.h` and `components/appsys`.
2. **Network and delivery.** Wi-Fi (provisioning through the web UI in AP mode on first start), SNTP, `poller`, `http_request`, streamed `app_on_data`, `request_focus`, scene swap on background delivery. The `plex` application. The `plugged` profile.
3. **The battery profile.** Light sleep between polls, wake-up by timer and button, current measurement, rounding of `every`, switching profiles by the setting. Then "extreme power saving" on deep sleep.
4. **Web UI and catalog.** Install from `catalog.json`, application and host settings (including the e-paper policy), the log, host OTA (`esp_https_ota`).

## State
Done and verified on hardware: the display driver (full refresh 2.19 s of BUSY, partial 0.41 s), buttons with short and long presses, the refresh policy, drawing and fonts, the splash and the bench demo (a short press → counters, a long press on button 4 → the splash, a stub for the host menu), host tests of the pure modules.

Open:
- `sdk/host_api.h` is an old draft (a timer without an id, `host_log`, button events 0/1, `on_data` with the whole body). Rewrite it to the Host API of this file before the first line of `appsys`.
- wasm3 is not wired in as a component: it is not in Espressif's registry, take it from github.com/wasm3/wasm3 (`platforms/embedded/esp32-idf`).
- The e-paper policy numbers are in menuconfig; move them into the host settings (`appconf` + NVS).
- There are no down/up button events yet, only short/long.
- `esp_ota_mark_app_valid_cancel_rollback()` is called right after the hardware is up; once there is OTA, move it to where the next update could arrive.
- The version on the splash is hard-coded as `ver 1.234.5` - a prototype string, not the build's version.
- `hardware/render` is a symlink to `render/` in the repository root while Blender is still writing there by an absolute path; when the renders are done, replace the link with the folder itself.

Next step - milestone 1: `host_api.h` → wasm3 → `components/appsys` → `tools/mkapp` + `sdk/apps/hello` → the host menu and `clock` (the splash and `demo.c` become host screens) → a check on battery without USB.

## Conventions
- Everything is written strictly in English, with a plain hyphen `-`, never an en or em dash.
- Nothing in the repository depends on one developer's machine: no home-directory paths, no fixed serial port names, no OS-specific steps in code, tools or documentation.
- SPDX header in every source file, C11. Logic that does not need hardware is pure and host-tested; `settings.c` is the only owner of the host's NVS.
- When debugging hardware: the vendor's reference code and the datasheet first, hypotheses after. Do not suggest "check the wires".

## Branding
- **The logo is the word `perINKpheraL` set in the device's square font: lower case `per` and `phera`, capital `INK` and a capital `L` at the end, all of one height in the same squares, with no plate, frames or dots.** The mix of capitals and lower case inside one word is the mark itself. On the panel - black on white, scale 5 (27 px squares), 357 px.
- The square font: every glyph is a square on a grid of 3 strokes × 2 gaps (5+6+5+6+5 = 27 px), no ascenders or descenders, diagonals allowed. Implementation: `firmware/components/gfx/font_square.c`.
- Under the mark on the splash: `ver 1.234.5` (a prototype string, hard-coded), `(c) 2026 Max Grakov`, `MIT License`.
- **The square font is for the word `perINKpheraL` only.** All other text on the device (splash, menu, host applications) is set in **Tektur** (SIL OFL 1.1, https://github.com/hyvyys/Tektur, variable: `wght` 400-900, `wdth` 75-100), the Regular instance (wght 400, wdth 100), rasterized to bitmaps by `tools/fontgen`. The file is `assets/fonts/Tektur-VF.ttf` with `OFL-Tektur.txt`. Fonts are OFL/CC0 only.
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
