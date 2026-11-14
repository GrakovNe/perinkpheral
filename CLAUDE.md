# perINKpheral

A desk e-paper companion on an ESP32-C6: a screen and four buttons, it lives on a shelf and shows "what you glance at". The architecture is Pebble's: an unchanging host firmware plus applications in wasm that are installed from a catalog without reflashing.

The name is written `perinkpheral` in code, the repository and the domain; `perINKpheral` in the README, the logo and headings. The pun: peripheral vision + peripheral device + ink.

This file is the one specification: vision, architecture, conventions, milestones and the current state. Next to it are only references: `AGENTS.md` - the bench, build and flash recipes, what must not be touched in the driver; `hardware/README.md` - everything found out about the panel, with the datasheet and links; `SECURITY.md` - who could take what from the device, what stands in the way and what does not. Where they disagree, this file wins.

**Everything in this project is written strictly in English**: code, comments, commits, documentation, this file, web pages, strings on the device. The one exception is the owner's (2026-11-14): the application guide has a Russian translation beside the English original (`docs/guide/guide.ru.txt`, built into `perinkpheral-app-guide.ru.pdf`); names are never translated in it (E-Ink, SDK, Watchdog, the calls and constants), and the English is the one that is right when the two differ. **Version 1 is ASCII only**: Cyrillic (fonts, strings, text from applications) is out of scope and comes back as a separate task in version 2 - do not design for it or build on it now.

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
- **The way out of a device that has stopped answering: buttons 1 and 4 (UP and BACK) held together for ten seconds restart it.** The press is an interrupt and the ten seconds are counted by a timer whose callback runs in an interrupt (`components/input/buttons.c`), so it does not wait for the host loop, the buttons' task or the scheduler; interrupts turned off for good are ended by the interrupt watchdog, as always. Nothing on the screen offers it - it is for when the screen cannot; Settings has Restart for when it can. Written and built; **not tried on the device**: the development tools press buttons above the pins, so only a hand can.
- From light sleep any of the four buttons wakes the device (`gpio_wakeup_enable`). Deep sleep - not used in version 1 - is another matter: its wake-up on the C6 is ext1 on GPIO0-7 only, so buttons 1-3 would wake the device and **button 4 (GPIO21) would not**.
- Flashing over the native USB Serial/JTAG (`/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS), `idf.py flash`. Entering the bootloader: hold BOOT while powering up or resetting.
- 3V3 gives up to ~700 mA for everything. On battery the 5V pin is empty.
- Over-discharge protection is entirely the battery's PCM. Charge level and power source are not shown on the screen - there is no data.
- Charge LED: blinks while charging, off when full. The CHRG signal is not routed to a GPIO.

## Stack
- ESP-IDF 5.5, C. No Arduino, ESPHome, LVGL, GxEPD2.
- Application runtime: **wasm3** v0.9.0 (interpreter, sandbox). Applications are built with wasi-sdk (found through `WASI_SDK_PATH`; verified with 34); the firmware build needs it too, since the image carries applications.
- Fonts: the square one (code, `font_square.c`) for the logo; for everything else one of two families, the user's choice (`font` in the settings; Font in Settings on the device, and on the page). **Departure Mono**, a monospaced pixel font and what a device starts with: one bitmap of it at its own 11 px from `tools/fontgen`, drawn at whole multiples - 22 px for the host, and 11, 22, 33, 44, 66 and 110 px for applications. **Jersey**, heavier, its letters of their own widths: four faces of it, each drawn for capitals of so many pixels - Jersey 10, 15, 20 and 25 - rasterized on their own grids and picked so that the capitals of every size are as tall as Departure Mono's (10, 15, 25, 30, 50, 80 px for its 8, 16, 24, 32, 48, 80), so what is laid out for one family holds for the other. Applications ask for a size by its id and get it in the family chosen. ASCII only in version 1.
- FS: LittleFS (partition `apps`, 640 KB). NVS twice: the host's settings in the partition `nvs`, the applications' `kv_*` in one of their own, `appkv` (56 KB), so that nothing an application keeps can crowd the settings.
- Network: `esp_http_client` (TLS through mbedTLS), `esp_http_server` for the web UI.
- The display driver is our own: init, full refresh (and Waveshare's fast one), partial refresh of a window, panel deep sleep. The bytes are clocked out by hand over GPIO, as a SPI master in mode 0 would, with **CS toggled per byte** - otherwise the refresh never finishes (see `AGENTS.md`). The SPI driver of ESP-IDF sent the same and spent 17 of every 21 microseconds getting ready for a byte; by hand a byte is 8.
- Buttons: `gpio_config` input without the internal pull-up (the pull-up is on the module), active LOW, debounce 20 ms. A press is reported as it happens (down, up) and as what it meant (short, or long at ≥600 ms).

## Memory budget (the hard limit of the C6)
- SRAM 512 KB, about 250-300 KB free after Wi-Fi + TLS. There is no PSRAM and there will be none.
- Flash 4 MB: 56 KB for the applications' stores (`appkv`) + two OTA slots of 0x170000 (~1.44 MB) + 320 KB of slots for the code of applications (`appcode`, six of 52 KB) + LittleFS 640 KB (manifests and files of applications) + 64 KB of glyph packs (`glyphs`, the last of the flash; taken from the files on 2026-11-13). A firmware larger than a slot means no OTA. The firmware is built for size (`-Os`, the interpreter too: for speed it was 7 % faster and 12.7 KB larger). Measured on 2026-11-13: with the development tools it is 1.31 MB, 11 % of the slot free; a build for release is 1.20 MB, 17 % (245 KB) free - it has no development tools (14 KB) and none of the words only a developer reads: a failed assertion says where by its address, an error is its number (`sdkconfig.release`, 70 KB). The page is carried packed with gzip, 17 KB of its 55, and sent so. What is large in it: the Wi-Fi libraries about 400 KB, TLS 190 KB, the firmware's own code 170 KB, the network stack 77 KB, the root certificates 69 KB, the interpreter 46 KB, mDNS 40 KB. The certificates are the next to go when the slot gets tight; if the slot gets tight, they can move into a file on LittleFS (`esp_crt_bundle_set`), which would also let them be renewed without a new firmware. The partition table is fixed - it cannot be changed over the air.
- A wasm application's arena is **exactly 64 KB** (one wasm page, there is nothing smaller), fixed for everyone. Build: `-Wl,--initial-memory=65536 -Wl,--max-memory=65536 -Wl,-z,stack-size=8192` (`tools/mkapp` does it); the interpreter is built with a limit of one page, so a module that asks for more does not load and none can grow.
- **One** wasm instance in memory at any moment; measured on the device, a loaded application costs 77 KB (the 64 KB page plus the interpreter) and all of it comes back when it leaves the screen. Free heap: 391 KB idle and 314 KB with an application on the screen before Wi-Fi was in the firmware; with Wi-Fi up as a setup access point, 236 KB idle - the radio costs about 155 KB, more than was budgeted, and its buffers have not been tuned yet. Delivering data to an application that is not on the scene = unload the scene → load the owner → deliver → bring the scene back (100-300 ms).
- **The heap is guarded** (`main/memory.c`). An application cannot take it - it has its own area - so what can run it short is the host's work for somebody, and each kind asks first: a request over the network needs 60 KB free and 17 KB in one piece, an answer of the web server's 24 and 8, a firmware image 32 and 8. Refused, the page says "Not enough memory." and an application gets `HOST_FAILED_NO_MEMORY` (a request waits six seconds for room before it is told). And when the heap is critically short all the same - under 12 KB, or an allocation failed - the host does not wait to fall over: at the first look it closes the page's connections, and after fifteen seconds of it restarts in an orderly way. `/api/status` counts refusals and failed allocations.
- **Where the 452 KB go** (measured 2026-11-12, plugged in, `weather` on the screen): 173 KB are the firmware's own - code that has to run from RAM, its data, the two frames; 112 KB are the application area, taken at start (128 KB since 2026-11-13, out of what was free); about 45 KB are the radio, the network stack and the tasks' stacks once Wi-Fi is up; **122 KB are free**, in one piece of 92 KB. A TLS session takes about 50 KB of that while it lasts: the least ever free was 73 KB. Looked at again on 2026-11-13: 120 KB free idle, 87 KB the least while starting, 82 KB after a poll over TLS, and 70 KB under the worst that could be arranged - two installs from the catalog, a poll over TLS and a dozen requests to the page at once, three times over; nothing was refused, no allocation failed, and all of it came back. The same day the application area grew by 16 KB for the calendar, so every one of these numbers is 16 KB less now: 104 KB free idle. Before the same day's change it was 76 KB free and 60 KB at the least: the radio's and the scheduler's code ran from RAM for speed this device has no use for (`CONFIG_ESP_WIFI_*IRAM_OPT`, `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH`), 44 KB of it. The radio's buffers were already at their least (4 static and 8 dynamic RX, 8 TX, no A-MPDU, a TCP window of two segments). IPv6 is off: nothing here speaks it, and it was 31 KB of the firmware.
- An HTTP response body goes to the application as a stream, in 1-2 KB chunks, while TLS is open; the host never holds it whole. Peak budget: TLS ~45 KB + one instance ~80 KB + the host.
- Two frames of 400×300×1 bit = 15 KB each: the back buffer the host draws into and the display service's copy of what the panel shows.

## Architecture

### Application
`/apps/<name>/app.wasm` + `/apps/<name>/manifest.json` on the LittleFS partition. The host reads `name`, `title`, `version`, `abi`, `fullscreen`, `system`, `icon`, `polls` (with `pick`), `hosts`, `settings`, `intents` and `companion`, checks each, and skips a key it does not know. A poll is an `http(s)` `url`, an `every` of 10 seconds to a day - a number, or `"{{key}}"` of a setting whose value is the pace as a person writes it ("15 min", "1 h"): the user's choice, taken by the host with no code in the application - and up to four `headers`; there are at most four polls, and the application knows each by its number. `{{key}}` in a URL or a header value is replaced by the value stored under that key (`text_template.c`). `name` is 1-13 of `a-z`, `0-9`, `-`, `_`: it is the folder, the key in the catalog and, as `a.<name>`, the NVS namespace of the application's store, and it must be the folder's name. `title` is at most 23 characters. **Texts a person reads are counted in characters** - a title, a setting's title, help and options - and may take twice that in bytes, which every letter of Europe's alphabets fits; the fields of the contract that hold such text (a notice's lines, a cell's name and value, the header's texts) are sized the same way, and the host cuts a text that is too long between characters, never inside one. The module is at most 48 KB (`MANIFEST_MODULE_MAX_BYTES`; `tools/mkapp` refuses a larger one) in a slot of 52 KB; what it has to fit when it runs is the application area (see Memory). The applications in `sdk/apps` are built and packed into the partition image with the firmware (`APP_BUNDLE_APPS`). The full manifest, as it will be:
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
  "intents": ["started"],
  "icon": [
    "...........",
    ".#########.",
    ".#.......#.",
    ".#..#....#.",
    ".#..###..#.",
    ".#..####.#.",
    ".#..###..#.",
    ".#..#....#.",
    ".#.......#.",
    ".#########.",
    "..........."
  ],
  "abi": 2
}
```
**An application may bring an icon**, in its manifest and optional: `"icon"` is 11 rows of 11 characters, `#` for ink and `.` for paper - the form the host's own icons have in the source, which a person can type, which follows the icon rules by construction and which scales. The device draws it beside the title in its menu, a cell two pixels; the web page draws it on the application's tile; a catalog's index carries it, so that an application has a face before it is installed.

**Settings are declared in the manifest, not by code**, the way Pebble did it with a companion page: nobody types a token with four buttons. The device's web page has an Applications section - what is installed, and a settings form for each, drawn from the manifest without running the application; the catalog will live in the same place. A field is a `text`, a `secret` (the page can write it and cannot read it back), a `number`, a `choice` from a list of `options`, a `toggle`, a `place`, a `url` or a `link`; it has a `key`, a `title` and may have a `help` line and a `default` - what the page shows while nothing is set; nothing is stored for it, so the application means the same when it finds nothing. At most 8 of them, a value at most 255 bytes. A `url` is the address of the user's own server - `http://192.168.1.10:8123`, a scheme and a host and maybe a port, nothing after - and the one setting a poll's URL may begin with and `hosts` may name: `"url": "{{server}}/api/states"`, `"hosts": ["{{server}}"]`. That is how an application for Plex or Home Assistant is written once for everybody's server. The requests of such an application go where the user said the server is and nowhere else, and a server changed to another forgets the secrets that were not given with it, so a token is not sent to an address it was not meant for. The `url` setting is the user's to write and the application's only to read: `kv_set` on it is refused, or an application could move its own server - and the token with it - wherever it liked. A value that goes into a header or stands as the server is looked at again when it is used, whoever stored it: one with a line break in it fails the request. A `link` is a whole address the user was given - a calendar's secret `https://host/path?key` - and is the whole of a poll's URL (`"url": "{{feed}}"`, `"hosts": ["{{feed}}"]`): the application's requests go to that host and nowhere else, and like the `url` it is the user's to write and the application's only to read; the page takes `webcal://` for `https://`. A `place` is asked for by its name: the page offers places as the name is typed (the browser asks Photon, a search of OpenStreetMap; the device does not) and fills the two `number` settings the place names as its `latitude` and `longitude`, which it does not show. The values are stored in the application's own store under the keys the manifest names, so the code reads a setting with `kv_get` by the same key, and the host substitutes it into URLs and headers (`{{key}}`). A change arrives as `app_on_settings()`; an application that is not on the screen is loaded for the call and unloaded after it, as for data.

**Strings are the developer's to translate, and the host's to put in place.** An application's `strings` folder holds a file a language - `en.json`, `ru.json`, each an object of the same keys; `tools/mkapp` makes `strings.h` of them (`STR_NO_EVENTS`, `app_string(id)`), builds English into the module and writes a table for every other language beside it (`strings.ru`, a file of the application's). When an instance is loaded the host copies the table of the device's language (`language` in the settings; General on the page) over the English one in the application's memory - when there is one, it is sound and it fits; else the application speaks English - so reading a string costs the application no code and no call. `text_fill` puts arguments into `{0}`, `{1}` of a pattern, in whatever order the language has them. The build fails on a language the system does not know (`sdk/languages.json`: the languages, and the glyph packs each needs), on a translation that lacks a key or has one too many, and on a letter the language's glyphs do not have. No plural forms. **What of the manifest a person reads** - the title, a setting's title and help, the labels of a choice's options - is translated in the same file under the key `manifest`, held by `mkapp` to the manifest itself and written beside it (`manifest.ru`); the host takes the title from it for the menu, and gives the page the settings in the device's language, an option keeping its value and gaining a label. **The host's own words** - what the buttons of its notices do, its questions, why an application has nothing to show, the names of days and months - are one table (`components/gfx/words.c`), English and Russian, in the firmware. Every application of the catalog has Russian. The system applications have Russian too: they have no files, so their tables are embedded in the firmware beside them (`launcher_system_file`). Still English: the page in the browser and the device's answers on it, and the names of the cities in Settings.

**A companion page** is for what a form cannot do - signing the user in somewhere, choosing from what is there. `"companion"` in the manifest is its address, `https` only: a static page of the application's author on any hosting. The device's page opens it in a window from the application's settings (Set up), and the two talk in messages: the companion says it is ready, gets the application's settings and what is set (no secrets), and hands back values for keys the manifest declares; they go into the form, and the user saves them. Nothing of it runs on the device and nothing passes through a server of the project's. `sdk/companion/example.html` is a sample. Seen in a browser against the device's page; no application has one yet.

**JSON is picked apart by the host.** A poll or a request names the paths it wants (`current.temperature_2m`, `daily.weather_code[0]`); the host finds them in the stream and hands the application the values, so an application does not parse anything. One that wants the answer as it came asks for it raw and gets the pieces through `app_on_data`. The picker is one source, `sdk/lib/json_pick.c`, built into the host and into any application that wants it: an answer to a request of an application's own comes in pieces, and the application feeds them to a picker of its own. Ease of writing an application is worth the host's memory here.

**HTTPS** verifies the server against the full set of root certificates, the browsers' one, carried in the firmware (60-70 KB of flash, to be measured). If the firmware stops fitting into its slot, this is the first thing to think about again. A server in the home network with a certificate of its own making is not trusted; plain `http` is what works there today. **Decided (2026-11-12), to be built with the first application that needs it:** the user gives such a server's certificate as a setting of the application - pasted on the page - and it is trusted for that application's hosts only. Not from the application's package and not into a store shared by all: a certificate a server shows is not a server's for long, and one application must not be able to vouch for another's hosts.

### System applications and the system API
The host's own pages are applications too - written with the same SDK, run by the same interpreter - so that the host is built with what it gives to others. They are **system applications**: built from `firmware/system`, carried inside the firmware image (an update over the network brings them along, so they always speak the API of the host they run on), never installed and never removed, and outside the five places. Their names begin with `sys.`, which no installed application's name can. `Settings` and `About` are there today. `Settings` is the device's settings with four buttons and no keyboard: General (date and time - from the network or by hand, the time zone by a city from a list, power saving: Disabled, Always or Auto; the default application and how long the menu waits before giving it the screen), Apps (the installed ones, each can be removed), Display (the font - Departure Mono or Jersey - the three numbers of the refresh policy, Fast refresh and the clock in the header), Wi-Fi (the network, the address, forgetting it), Restart and Reset - each of the last two a question. Nothing is typed: a value is one of a few stepped through with UP and DOWN, a date is its parts stepped the same way with OK to the next and BACK to the one before, and what cannot be undone asks first. What needs text is set from the web page. `About` is what the device is, on two pages that OK turns between. The first is the mark, the version of the build (`v0.1.0-0adbfb4`: the version the project is at and the commit), a QR code of the project's address and, in the small font at the bottom, the author and the license; it is the splash too. The second is the device's state - its name, its network and address, the firmware and its slot, memory and uptime, all asked of the host with `sys_info`. They were two applications, `Device` and `About`, until 2026-11-13. 

What stays native is what has to work when nothing else does: the menu, the message of an application that crashed, the question about a storage that cannot be read and the question a request from the network waits on. The screen that says how to give the device a network is a system application too (`setup`, not in the menu): without it the device starts at the menu, and About has the address. The splash at power-up is not among them: it is About's picture, and without it the device simply starts.

They are written with the **system API**, the `sys_*` part of `sdk/host_api.h` (`sys_info`: the version, the name, the network and its state, the address, the slot, free memory, the time zone; `sys_number_get` / `sys_number_set` and `sys_text_set`: the host's settings; `sys_time_set`; `sys_app_title` / `sys_app_remove`; `sys_app_default` / `sys_app_set_default`; `sys_do`: restart, forget the network, factory reset; `sys_logo`). It is not fenced off: the device is its owner's, and any application may have it. It has to ask - `"system": true` in its manifest - and the page says "<title> requests system access." before it installs one. A module that imports a `sys_*` call without having asked is refused by `tools/mkapp` and by the host.

### Execution model
- **Scene** - one application, it owns the screen and the buttons.
- **Polling** - the host itself fetches every `polls` entry of every installed application at its `every`. This is the background mechanism ("pushes").
- **Raw requests** - an application may itself call `http_request` (method, URL, headers, body) - for OAuth, calendars, actions on a button. Requests go into the host's queue, one at a time; only hosts from the manifest's `hosts` are allowed. The response arrives through the same `app_on_data`.
- **Redirects** - a `GET` that is answered with one is asked again where it says, up to three times. A request of the application's own stays inside its `hosts`: sent elsewhere, it fails with `HOST_FAILED_NOT_ALLOWED`. The headers an application gave do not travel to another host; `http` and `https` of one name are one host, and so is a port written out when it is the scheme's own. A request with a body is not sent twice: its redirect is the answer.
- **Delivery** - the host calls `app_on_data(request, ptr, len, flags, http_status)` in chunks (flags: FIRST, LAST, FAILED; the HTTP status comes with the first chunk) on the owning application, **whether it is on the scene or not**:
  - on the scene - the call is made right away;
  - not on the scene - unload the scene (`app_on_blur`) → load the owner → deliver → if it called `request_focus()` it stays as the scene, otherwise unload it and bring the previous scene back (`app_on_focus`).
- **State** - every application must restore itself from `kv_*`: the host unloads it on every scene change and background delivery. The framebuffer is not touched during the swap (the screen is not redrawn); an application's timers (`timer_set`) stop when it leaves the screen and are set again by it in `app_on_focus`.
- **Buttons** - the layout is the same everywhere in the host: 1 up, 2 ok, 3 down, 4 back. Applications are encouraged to keep it.
- **Moving about is a stack** (`components/ui/wm.c`, pure). The menu is under everything and is what shows when the stack is empty. A long press on BACK is the beginning again: the stack is emptied and the menu shows, and BACK in the menu does nothing - unless the user chose a default application, which BACK in the menu then opens. Whatever takes the screen goes on top - an application opened from the menu, a question the host asks, an application whose alarm came - and the one under it leaves and waits; when the top is done, what was under it enters again. BACK is one step down. Opening from the menu starts anew; a scene is in the stack once - opened again, it moves to the top from where it waited; six deep, the oldest forgotten. Only the top is loaded - there is room for one application - so a scene that waits enters again from what it stored. Nobody keeps a "where to return" of their own: the host's question and its work notice are entries like any other. **BACK is the way out and needs no code**: a short press of BACK leaves an application that does not export `app_on_back()` - to the menu, when it was opened there. One with pages of its own exports `app_on_back()`, gets the press there instead of in `app_on_button`, goes a page back and calls `request_exit()` from its first. An application counts on this: `request_exit()` is one step down, and one that took the screen with `request_focus()` returns the user to what they were looking at.
- **Switching** - long press on button 4 → the host menu (list of applications, selection with buttons 1/3, confirmation with 2, button 4 goes back to where the menu was opened from). A row of the menu is a title and nothing else; the selected row is inverted, white on a black bar. All other button events go to the application on the scene.
- **Preemption** - `request_focus()` from an application that received data in the background. Protection against a war for the screen: no more than one preemption per N seconds (30 by default), priority from the manifest on a conflict, a long press on button 4 always returns control to the user.

### Host kernel
The host is layers, and each knows only the ones under it:

| Layer | Where | What it does |
|---|---|---|
| Drivers | `components/epd` (`epd.c`), `components/input`, `components/board` | the panel, the buttons, the antenna switch |
| Display service | `components/epd` (`display.c`, `refresh_policy.c`) | keeps a copy of the frame the panel shows; given a new frame it leaves the panel alone if nothing changed, refreshes it, or says "too soon" |
| Graphics | `components/gfx` | draws on a surface: the whole frame or a viewport into it with its own origin and clipping |
| Window manager | `components/ui` (`wm.c`) | which scene has the screen, how the user moves between scenes, how a frame is put together from the header and the scene's picture |
| Scenes | `main/scene_*.c`, `components/appsys/app_scene.c` | tie a model to a view and decide what the buttons do, all behind one interface (`scene.h`): enter, leave, button, draw |
| Views | `main/screens.c` | a struct in, pixels out; no hardware, rendered by `tools/preview` |
| Services | `main/sysinfo.c`, `main/status.c`, settings, `components/net` (Wi-Fi, the clock); later polling | the data, with no idea that a screen exists |

- **A partial refresh sends only what changed.** The display service finds the box outside which the new frame is the frame on the panel (`frame_box.c`, pure) and the driver sends that window - Waveshare's own `PartialDisplay`, the same commands and waveform. The window is taken together with the one of the refresh before, so that the result does not depend on what the controller does with its two frames afterwards. After a partial refresh the panel is left out of deep sleep for 1.5 s (the `panel` task puts it there), and one that comes in that time needs no reset. A step in the menu is 0.46 s, was 1.0 s; the whole frame 0.53 s: 0.41 s of BUSY and the bytes. Seen in the log only: the picture on the panel itself has to be looked at by a person.
- **A list longer than the screen has a scroll bar** at its right edge (`components/ui/scrollbar.c`, pure): a thin line and a thumb on it - as much of the line as the rows shown are of the list, as far down as the first of them. The selected row is shorter by the bar's room. The host's menu has it, and the lists of Settings draw the same. **A row's label and value never meet** (`list.c`): too wide together, a value of up to half the row - a time, a state - stays whole and the label is cut with `...`; a longer value has what the label leaves and never less than the half.
- **Two frames.** The back buffer (`host.c`) is where frames are put together; the display service holds the front copy of what the panel shows. Drawing never touches the panel, an unchanged frame costs no refresh, and a scene cannot spoil the screen halfway.
- **Layers are areas of one frame**, not buffers of their own: the header and the content do not overlap. A scene that is not full-screen gets a viewport under the header and cannot draw outside it.
- **A scene never refreshes anything.** It reports that its picture changed and draws when asked; the window manager passes on why the frame changed (a switch, a press, data), and the display service decides what the panel does.
- **One module keeps every deadline** (`components/deadlines`): the scene's timers, the applications' alarms and polls, the panel's sleep and its daily refresh, the while after a press, Wi-Fi's retries, the day turning in the header, the menu left idle, the device stopping. The table is pure (`deadline_table.c`); around it are one timer set for the nearest and the clock as the scale. A deadline that has come is news in the event queue, answered by the host loop; one that could not be told stays in the table.
- **One event queue** (`events.c`). A press, later a timer or a response from the network, is posted from its own task and taken off by the host loop one at a time: nothing in the host runs concurrently with anything else.
- **The menu is a scene** like any other, not a layer over an application: opening it takes the screen away from whatever had it, which waits under it in the stack.
- **Development tools** (`APP_DEV_CONSOLE`, on by default, off for a release): the device is driven and looked at without hands on it. Over the network, `/api/dev` gives the frame the panel shows, takes presses, opens an application and takes a new build of one; `tools/devtool` is the command line for it, and `devtool.py run <app dir>` builds an application, uploads it and restarts the device into it in about ten seconds, with no reflashing. `devtool.py serve <app dir>` is the same for a person in a browser tab beside any editor - the live screen, the four buttons (and the keyboard's arrows, Enter and Backspace), the log as it is written and a Run button; `devtool.py new <name>` makes an application to start from, and `log --follow` prints the log as it grows (`/api/log?since=<where the last answer ended>`, the end in `X-Log-End`). `tools/mkapp` says what a build came to against what the device has: the module, the code and what running it takes of the interpreter's 64 KB, the data and the stack in the 64 KB page, the files. Over the cable, characters typed into the USB console do the same for a device with no network (`tools/devshot`). Whatever touches the frame or the scenes is handed to the host loop through the event queue.

### Host screens: notices
What the host itself has to say takes the whole screen, with no header, and is one picture drawn after the owner's sketches (`notice_draw` in `components/ui/notice.c`, pure): two lines of large text on the left, a large icon from the host's set on the right, and along the bottom either what the buttons do or three strokes that stand for work going on. No bar that moves. **The buttons are named the same way everywhere** (`kit_actions`): a button's name white on black - what OK does and then OK from the left edge, BACK and then what it does from the right: `Format OK        BACK Skip`. A button that is the only one stands in the middle. The icon stands a little in from the right edge.
- **A question** (`scene_confirm.c`): "Update firmware?" with the update icon, "Factory reset?" with the warning, "Join <network>?" with the signal, "Install system app?" with the gear. OK does it, BACK cancels; the menu and a minute of silence cancel too. A press is not an answer until the question is on the panel.
- **An error** (`scene_storage.c`): "Storage error" with the warning; OK formats, BACK goes on without.
- **Work going on** (`scene_working.c`): "Updating in progress" from the first byte of a firmware image to the restart into it, or until the upload is given up. The buttons do nothing.
- **A notification** (`scene_notify.c`, `notify` in the contract): what an application has to tell wherever the user has gone - the timer's "Time is up", the calendar's meeting that begins. The application hands the host the notice from any handler, an alarm's above all, and is done: the host shows it under the header over whatever has the screen, keeps it until OK or BACK dismisses it, and gives the screen back. Up to four wait their turn; a restart forgets them. Phone notifications will come this way.
- **An application that is not running** (`app_scene.c`): its title, "crashed" or "cannot start", why in a line of ordinary text under them, the warning; BACK closes, and so does any press.
- **The questions Settings asks** - remove an application, forget the network, factory reset - are the same notice, drawn by the host for the application (`ui_notice`), under its header: OK does it, BACK cancels.
- **The kit** (`components/ui/kit.c`, `list.c`, `notice.c`, all pure, rendered by `tools/preview`) is what every screen is put together from, the host's and an application's alike - the design system. Primitives: text in four styles (text, title, caption, value), the title of a page with its line, an icon in three sizes (22, 44, 99 px), a button with what it does, the row of buttons along the bottom. Elements: a bar (how much of how much; it changes with the data, it does not move), a chart (a series of numbers as a line, an area, bars or steps), the frame with arrows around what UP and DOWN change. the cell - a name, a picture and a value in a box of any size, which is what a dashboard is made of. Screens: the list (rows with values, options with a check, facts nobody chooses from), the notice (a question, an error, work going on, a message such as "Not set up"), the picker of one value from a row, the date and time set a part at a time. An application asks for them with `ui_*`; **what the buttons do on those screens is `sdk/lib/app_ui.h`** (`app_list_button`, `app_picker_button`, `app_date_button`, each answering changed, done or cancelled), so an application writes only what the result is for. Nobody has to use any of it: `gfx_*` draws anything. Settings is written with it, and `kit` in the catalog shows every part on a page of its own.
- **Any application may ask for the host's look**: `ui_notice` is this notice, and `ui_list_row` a row of the host's list - the bar behind the chosen row, a value or an icon at the right edge, the scroll bar (`components/ui/list.c`, pure). The menu and the lists of Settings are drawn by the same code, so an application that uses it looks the part and stays so when the look changes.

### Power-up
A partition of applications that cannot be read is never formatted behind the user's back: after the splash the device says "Storage error. Format storage?" with Cancel and Format (`scene_storage.c`, native - it has to work when nothing else does). Cancel leaves the device running with no installed applications; Format erases the partition and restarts. What the applications stored is in NVS and stays.

The splash is drawn once and the device moves on by itself after a few seconds (a press does it at once). It lingers once: set aside for an application that takes an answer off the screen, it moves on as soon as it is back - a device with several applications has no six quiet seconds after a start, and stayed on the splash until a press (found by the owner, 2026-11-13):
- **no network stored** - to the setup screen: the name of its access point and the address to open;
- **otherwise** - to the default application when the user chose one, else back to the application that was on the screen last (its name is kept in NVS), or to the menu when there was none.

A device on a shelf has to come back from a restart showing what it showed, without a press.

**The default application** (`default_app` in the settings, by name; General in Settings on the device, and on the page) is what the device shows by itself: after a restart, by BACK in the menu, and when the menu has been left alone for `menu_idle_min` minutes (1 to 60, 5 by default; `DEADLINE_MENU_IDLE`, set anew by every press). Only the menu gives way: an application that took the screen - by an alarm, say - has something to tell and stays until the user closes it. A default that is not installed is as none.

### Wi-Fi
- **No network stored** - the device puts up an open access point named after itself (`Perinkpheral-XXXX`) and waits to be set up at `192.168.4.1`. The setup screen shows a QR code that joins a phone to that access point, with the network name and the address in words under it. A QR code cannot both join a network and open a page, so the access point is a captive portal: its name server (`captive_dns.c`) answers every name with the device, the web server redirects whatever it does not know to its page, and the phone opens the page by itself. The name server answers only while the access point is up. Not yet tried with a phone.
- **A network stored** - it connects; a lost or failed connection is tried again after 2, 4, 8 ... at most 30 seconds.
- **Two minutes without success** - rescue: the setup access point comes up as well and the device keeps trying once a minute, so a wrong password never needs the cable. Connected again, the access point goes away.
- The rules are `components/net/wifi_plan.c`, pure and host-tested; `wifi.c` only carries them out. The Wi-Fi library keeps nothing in flash: the credentials are the host's settings.
- The device answers to its name: `perinkpheral-xxxx.local` (mDNS, the `espressif/mdns` component), and announces its page as `_http._tcp`. It costs about 8 KB of memory and 42 KB of the firmware.
- Time comes from SNTP whenever there is a network, in the user's time zone (a POSIX TZ string in the settings). Everything a person reads is local time: the header, the status page, the applications through `time_zone_offset()`. Until the first answer the device does not know the time: the header shows no date and `time_now()` is 0.

### Header
The host draws a header above its own screens and above every application that does not ask for the full screen:
- On the left the Wi-Fi icon in one of two states - connected, or not connected (the same icon struck through). It shows the outcome of the last connection attempt, which is what matters in the profiles where Wi-Fi is off between polls.
- On the right the date (`Wed 11 Nov`), blank while the time is unknown.
- Nothing else of the host's own: no application title, no network name - the date changes once a day, so the header never asks for a refresh of its own. There is no footer. **The time instead of the date** is a setting (`header_clock`, off by default; Display in Settings on the device, and on the page): the header then turns with every minute, above every application.
- **An application may put its own text into both**: beside the icon on the left, where the host has nothing, and on the right in place of the date (`header_text`). It gives a line and nothing more - the font and the size are the host's. A text that does not fit is cut with `...`, and the two never meet: when together they are too wide each is sure of half the width, and what one does not need of its half goes to the other (`wm_header_share`, `gfx_text_fit`). A side the application gave nothing for is the host's; a text of one space leaves it empty. The texts are forgotten when the application leaves the screen. `weather` shows its city and the date, each behind a setting of its own.
- **An application may keep an icon and a line in the header while it is away** (`header_status`): something the user set going and wants in sight - the timer's minutes left and its hourglass. The header reads **icon text ... text icon**: on the left the icon stands in place of the host's Wi-Fi one with the line after it, on the right the line stands in place of the date with the icon after it. They are there above every screen with a header, whoever is on it. A side is the application's that set it last, until it gives it back, is removed, or the device restarts; a side kept this way comes before what the application on the screen gave for it. A recurring alarm (`alarm_set_every`) is what keeps the line right. `timer` keeps the right side.
- An application chooses in its manifest: `"fullscreen": false` (the default) leaves it 400×260 under the header, with its coordinates starting there, and it cannot draw over the header; `"fullscreen": true` gives it the whole 400×300. A long press on button 4 opens the menu in either mode.
- The network name and the address are on the second page of About, not in the header.

### Host API (wasm imports)
The only contract is `sdk/host_api.h`; applications and the host's import table are built from it, and `tools/mkapp` refuses a module that imports anything else. It is not frozen and, until the first release, not versioned: the contract is simply changed, with no backward compatibility, and the applications are rebuilt - the ones in `sdk/apps` with the firmware, the ones in perinkpheral-apps by `build.sh` there. `HOST_API_VERSION` and the manifest's `abi` exist for later and stay at 2 until then. What exists today:
```
gfx_width() / gfx_height()             // the surface: 400x300 full-screen, 400x260 under the header
gfx_text(x, y, font_id, str_ptr, len)  // y is the top of the line; returns the width drawn
                                       // fonts: 0 text 22 px, 1 medium 33, 2 large 44, 3 display 66, 4 huge 110, 5 small 11; all of ASCII in each
gfx_text_width(font_id, str_ptr, len) -> px
                                       // font_id | HOST_FONT_TABULAR: every digit takes the room of the widest one,
                                       // so a time that changes does not shift - for a typeface whose digits differ
gfx_text_fit(font_id, str_ptr, len, max_width, out_ptr, cap) -> len   // the text cut with "..." to fit the width
gfx_font_height(font_id) -> px          // the whole line
gfx_font_ascent(font_id) -> px          // from the top of the line to the baseline
gfx_font_cap_height(font_id) -> px      // capitals and digits above the baseline
gfx_color(0..16)                       // how dark the calls below draw: 0 paper, 16 ink, between them a grey by dithering
gfx_rect(x, y, w, h, fill)
gfx_line(x0, y0, x1, y1)
gfx_bitmap(x, y, w, h, bits_ptr)       // a set bit takes the colour, a clear one leaves what is there
gfx_qr(x, y, cell, str_ptr, len)       // a QR code of the text, up to 57 modules a side; returns the modules, cell 0 only measures
gfx_bitmap_file(x, y, w, h, name, name_len, offset)  // the same picture read by the host from the application's file
gfx_picture(x, y, picture_ptr, cell)   // a picture of the application's own: a byte of width, a byte of height, a bit a cell;
                                       // a cell so many pixels. tools/mkpicture makes them of PNGs and of rows of '#' and '.'
gfx_icon(icon, x, y, cell)             // one of the host's icons, 11 cells square, a cell so many pixels; in the current colour
request_redraw(flags)                  // the picture is out of date; flags: SOON - show it now, CLEAN - a full refresh,
                                       // NO_BLINK - now, and never as a full refresh: one picture of a row
ui_list_visible(list_ptr) / ui_list_first(list_ptr)  // the host's list - count, selected, top, height, first: rows in sight, the first of them
ui_list_row(list_ptr, index, label, value, icon)     // one row of it, with the scroll bar; a title may stand over a list and buttons under it
ui_notice(struct_ptr)                  // the host's notice on the whole surface: two lines, an icon, what OK and BACK do
ui_text(x, y, style, str, len, max_width) / ui_title(str, len)   // text in the host's styles; the title of a page
ui_text_lines(x, y, style, str, len, width, max_lines)           // the same over several lines, broken at spaces, the last cut
ui_text_width(style, str, len) / ui_text_height(style)           // to centre and to stack; HOST_UI_MARGIN is the host's 16 px
ui_icon(x, y, icon, size)              // a host's icon at 22, 44 or 99 px
ui_button(x, y, name, action, mirrored) / ui_actions(ok, back)   // a button and what it does; the row along the bottom
ui_bar(x, y, w, value, max) / ui_frame(x, y, w, h, arrows)       // how much of how much; the frame around what is set
ui_cell(struct_ptr, picture_ptr) / ui_cell_fit(...)   // a name, a picture and a value in a box of any size, the value as large
                                       // as fits; cells that stand together take the size the longest value can have
ui_chart(struct_ptr, values_ptr)       // a series of integers as a line, an area, bars or steps, scaled to a box;
                                       // up to 128 values, a gap for a missing one, captions of the least and the greatest
ui_value(x, y, font, value, unit) / ui_value_width(...)   // a value and its unit after it on the baseline: "18 km/h"
ui_reason(status)                      // why there is nothing to show, as every application says it: Loading,
                                       // Not set up, No network, Access denied, No answer; 0 for an answer that came
ui_picker(struct_ptr) / ui_date(struct_ptr)   // one of a row of values; a time, a date or both, a part framed
request_exit()                         // done: one step down the stack, as BACK does in the host's pages
header_text(side, str_ptr, len)        // its own line in the host's header, left or right; len 0 gives the side back
request_focus()                        // from app_on_data off the screen: worth showing now; the host decides
notify(notice_ptr)                     // something to tell the user wherever they are: the host shows the notice over
                                       // whatever has the screen until it is dismissed; the application is not loaded for it
timer_set(id, ms)                      // one shot, 0 cancels; fires app_on_timer(id)
kv_get(key, key_len, buf, cap) -> len  // the application's own store
kv_set(key, key_len, buf, len)         // len 0 removes the key; 1 KB a value, 4 KB an application
http_request(tag, method, url, headers, body)   // only to the manifest's "hosts"; the answer comes under the tag
                                       // GET POST PUT DELETE PATCH HEAD, and PROPFIND REPORT for calendar servers
data_response(struct_ptr)              // inside app_on_data / app_on_values: the status, Content-Length and -Type,
                                       // ETag, Last-Modified, Location, Link, Retry-After of the answer in hand
poll_now(n)                            // fetch poll n of the manifest now, not when it is due
poll_set_interval(n, seconds)          // another pace for it, kept through a restart
alarm_set(id, seconds)                 // app_on_alarm(id) once, on the screen or off it; 0 takes it back; four an application
alarm_set_at(id, unix_time)            // the same at a moment of the clock, and following the clock when it is set
alarm_set_every(id, seconds, first_in) // the same again and again, from a minute to a day; the first in `first_in`
header_status(side, icon, str_ptr, len) // an icon and a line that stay on a side of the header while the application is away
file_size(name, name_len) -> bytes     // a file from the application's package (its `files` folder)
file_read(name, name_len, offset, buf, cap) -> bytes read
file_write(name, name_len, offset, buf, len)  // inside the file or at its end; len 0 at 0 removes it
time_now() -> unix seconds             // 0 while the time is unknown
time_zone_offset() -> seconds          // add to time_now() for the user's local time
time_local(struct_ptr)                 // the local time taken apart: year, month, day, weekday, hour, minute, second
time_text(unix_time, style, out, cap)  // a moment in words: "15:04", "Fri 13 Nov", "Friday", "13 November",
                                       // and as near as it is: "15:04", "Tmrw 15:04", "Sat 15:04", "21 Nov"
text_fill(pattern, len, args, count, out, cap)   // "{0}" and "{1}" of a pattern replaced by numbers and texts
time_uptime_ms() -> ms since start
time_boot_id() -> a number that is another after every restart, never 0: kept beside a stored uptime
log(level, str, len)                   // one line of at most 200 characters; what is not printable ASCII is logged as "?"
```
**The host's icons** (`components/gfx/gfx_icons.c`, `HOST_ICON_*`): signal and no signal, the four arrows, check, cross, plus, minus, trash, gear, clock, hourglass, bell, info, warning, play, pause, update - twenty, each drawn as text in the source on eleven cells square, in the manner of the font. The header, the host's questions and Settings draw theirs from the same set. `tools/preview` renders the sheet.

Everything agreed for the contract on 2026-11-11 is written. The last of it, `gfx_bitmap_file`, has run under the host tests only: no application draws a picture from a file yet.

Done since and seen on the device: requests of an application's own (`http_request`: one at a time for the whole device, two waiting for the whole device, only to the manifest's `hosts`, the answer in pieces through `app_on_data` under a number of 16 or more), files (`file_read`, `file_write`, 64 KB an application), the alarm (`timer` in the catalog: a countdown that takes the screen back from another application when it ends), `poll_now`. The alarm is kept in memory: a restart forgets it.

**Few applications, large ones**, the way Pebble did it: at most **6** installed, each with a module of up to 48 KB (in its slot of 52 KB in `appcode`), 64 KB of files and 4 KB of `kv_*`. **The files are a promise to all six at once**, an update included, and so they are counted as they lie on the device - each file rounded up to a block of 4 KB (`MANIFEST_FILE_BLOCK_BYTES`), sixteen blocks an application: by `mkapp`, by `file_write` and by an install from a catalog, which did not look at the sum before. The partition is 160 blocks; six applications full of files are 6 x (16 + a folder's 2 + the manifest's 1) and the root's 2, 116; one more installed over its old version is 16 and its module's 12 on the way to its slot and 3, 31; 13 blocks are the file system's to work in. Measured on the device: five applications with small files take 16 blocks, `badapple` with its 60 KB another 17. `/api/status` has `storage_used` and `storage_total`. It was five applications of 120 KB until 2026-11-13, which the partition could not have held. Whoever wants more rotates them - one day from a companion application on a phone.

**Put off: BLE.** The chip has BLE 5 and no classic Bluetooth. As a host service it could be media keys - the device pairs as a keyboard and an application sends play, pause, next, volume to whatever it is paired with, with no API of the player. What plays now can be read that way only from an iPhone (Apple Media Service); Android and computers give it over classic Bluetooth, which is not here. It is not in the plan of milestone 2: the stack is estimated, not measured, at 200-300 KB of flash and 60-90 KB of memory, next to a firmware slot with about 300 KB left and a heap that TLS still has to fit into. Measure before deciding. **Put off again by the owner (2026-11-13):** wanted - the host owning the radio, pairing and bonds, an application saying what it wants to be - but the problem is memory: about 150 KB are free in the firmware's slot, less than the stack is expected to take, and the heap has little to spare beside TLS. It comes back when room has been made and the cost measured.

**How soon a redraw is shown** follows from where it was asked: from `app_on_button` at once, from `app_on_timer` and `app_on_data` by the e-paper policy. Flags on `request_redraw`: `HOST_REDRAW_SOON` - show it now although nobody pressed anything; `HOST_REDRAW_CLEAN` - a full refresh, for a new picture; `HOST_REDRAW_NO_BLINK` - now, and not as the full refresh the host makes after every few partial ones: for pictures in a row. The traces then pile up until the first refresh that does not ask for this, or until the application leaves. None has a budget: the device is made by developers for developers, and one who wants to spend the battery and the panel may. A row of pictures is shown as fast as the panel takes them; any other request has a floor of one refresh a second.

What a handler may count on: one handler runs at a time and has 3 seconds; it returns nothing and leaves requests (`request_redraw`, `request_exit`, `request_focus`) that take effect when it returns; presses are not lost, timers of one id collapse into one, and the pieces of one answer arrive in order with nothing between them.

There are no pointer callbacks. An application exports functions with fixed names (the host skips the missing ones):
```
app_init()
app_on_focus() / app_on_blur()
app_on_terminate()                  // the instance is about to be unloaded, whatever the reason: save here
app_on_back()                       // a short press of BACK, for an application with pages of its own; without it BACK leaves
app_on_button(event)                // BTN_UP | BTN_OK | BTN_DOWN | BTN_BACK with _KEYDOWN, _KEYUP as it happens
                                    // and _PRESSED, _LONG_PRESSED as what it meant: BTN_OK_PRESSED
app_on_timer(id)
app_on_draw()                       // paint the whole picture from the state
app_on_data(request, ptr, len, flags, http_status)   // a piece of the answer to poll number `request`
app_data_buffer() -> ptr            // where the host puts that piece: 1 KB, sdk/lib has it ready
```
- **An application draws only inside `app_on_draw()`.** Handlers change the state and call `request_redraw()`; the host calls `app_on_draw()` when it needs the frame, with a white surface, and the `gfx_*` calls answer with an error anywhere else. The host puts the frame together, compares it with what the panel shows and decides whether and how the panel is refreshed - so there is no `gfx_flush`, no partial-or-full choice in the application's hands, and an application cannot leave a half-drawn screen.
- **Mistakes the build refuses** (`tools/mkapp`, 2026-11-14): a function named like a handler that is none (`app_on_buton`), a call whose answer has nowhere to arrive (`timer_set` without `app_on_timer`, `alarm_set*` without `app_on_alarm`, `http_request` without `app_on_data`), a poll or intents of the manifest without their handler, drawing without `app_on_draw`, a string or a picture the code never names. A module that fails a check is removed.
- **Two more say themselves in the log** of a running application: a handler that took over 400 ms (`slow handler: on_draw took 520 ms`, once an instance) and more than 12 writes of the store in a minute (every one is a write of the flash).
- **Two mistakes say themselves in the log**, once each: a drawing call outside `app_on_draw()` ("draws nothing: change the state and call request_redraw()"), and a redraw that waits for the refresh policy ("a redraw waits 37 s") - a picture that does not come is otherwise taken for a hang.
- **The module is not trusted.** One that imports anything outside the host API is refused before it runs. Every pointer and length it passes is checked against its 64 KB before the host touches it, and coordinates and sizes beyond a few thousand pixels are refused, so that no call can cost more than the surface is large. A handler has 3 seconds: one that is still running then - a loop without an end - is stopped, and recursion is stopped before it runs through the host's stack. A handler that traps or is stopped ends the application, and the screen says so.
- **Data arrives as values or in pieces.** `main/poller.c` fetches every poll of every installed application when it is due, one request at a time, over HTTP and HTTPS. A poll with `"pick"` in the manifest is read to the end by the host, the connection is closed, and the application gets the values in one `app_on_values()` call, reading them with `data_value()` (`sdk/lib/app_values.h` turns them into numbers and text). Any other answer goes to the application as it comes: the host reads up to 1 KB, copies it into the buffer the application named with `app_data_buffer()` and calls `app_on_data()` - flags say first and last, the HTTP status comes with the first piece, and a request that failed is one call with `HOST_DATA_FAILED` and no data, the status saying why: no network, no answer, a setting not set, a host not allowed, a timeout, a certificate the device does not trust, an answer over 256 KB (4 MB for one handed on in pieces: a calendar of some years is a megabyte or two), no memory. Either way an application that is not on the screen is loaded for the delivery and unloaded after it (`app_delivery.c`). Seen on the device: `weather` asking Open-Meteo over HTTPS, on the screen and off it; a request takes about 6 seconds, most of it the TLS handshake, and the free heap goes down to about 80 KB while it lasts. The pieces path has run only under the host tests.
- **State lives in the store.** An application is unloaded whenever it leaves the screen, so what must survive goes through `kv_get` / `kv_set`: up to 1 KB per key and 4 KB for all the values of one application, keys of up to 15 characters, in the `appkv` partition. The namespace is `a.<name>` and an application's name cannot hold a dot, so no application can reach the host's settings or another application's values. `hello` reads its count when it starts and saves it in `app_on_blur()`.
- **Text is bytes, and what is not ASCII is carried, not drawn.** The host hands on the UTF-8 of an answer as it came - a value picked from JSON is cut between characters, never inside one - and an application may store it and send it back. The fonts have ASCII only: every other character is drawn as one `?`, and `gfx_text_fit` cuts between characters. What is beyond ASCII is drawn by the host, the same in every application, down a ladder: the letter's own glyph from a **glyph pack** when one is there (`gfx_pack_add`; `tools/fontgen/mkpack.py` makes `latin`, `cyrillic` and `greek` - 32, 18.7 and 1.4 KB, 14 of the partition's 16 sectors: Departure Mono's letters as it has them; Jersey's Latin as it has it, 191 of the 232; Jersey's Cyrillic - every letter of the pack: Russian, Ukrainian, Belarusian, Serbian, Macedonian, Bulgarian, Kazakh - made by `tools/fontgen/jersey_cyrillic.py` from the Latin glyphs of the same face, mirrored, cut and joined, with stems and bars of the face's weight, so one description serves every face. Jersey 20, the face of the largest size alone, is left out for room, and Jersey has no Greek: there the ladder goes on to the spelling); else its spelling in ASCII by a table (`spelling.h`, made by `mkspelling.py`: "Moskva", "Strasse", "Athina", a typist's dashes and quotes); else `?`. The degree sign (`\xC2\xB0`) is drawn in any font as a small square with a hole at the top of the capitals. On the device the packs live in a partition of their own (`glyphs`, 64 KB, mapped: `main/glyphs.c`), each in whole sectors, several side by side; a pack is written with its first bytes last, so one cut short by a loss of power is no pack. The user adds and removes them on the page (Settings, Language: the browser fetches a pack from the official catalog's `glyphs` folder and hands it to the device - `GET` / `POST /api/glyphs`, `POST /api/glyphs/remove?name=`), and the picture on the panel changes at once. Seen on the device: Russian, German and Greek in the calendar with the packs, spelled without. The Kazakh schwa, which the font lacks, is its Latin one's shape. Not built: choosing languages rather than packs (it comes with the localization of applications).
- **An answer may simply stop.** An application that was loaded off the screen to take an answer in pieces gives way the moment the user presses a button; the pieces still to come are dropped, it is told nothing, and the poll is fetched again in a minute. An application keeps what it shows until an answer has come whole.
- **No floating point.** The interpreter is built without it - the chip has no FPU, and soft float in the host was 34 KB - and without reference types, extended constants and compact imports; a module still has its one table of functions and calls through pointers. `tools/mkapp` refuses an application whose code has a float operation and says which; one that got onto the device anyway stops when it reaches it. Numbers are integers - tenths, hundredths - and `sdk/lib/app_values.h` reads them that way.
- **No C library.** Applications are linked with `-nostdlib` against `sdk/lib`, a few hundred bytes of string building and the `memcpy` family, and `app_alloc.h` - an arena over what the page has left after the stack and the static data (`app_alloc`, and `app_alloc_mark` / `app_alloc_release` to give back to a mark; `mkapp` says how much is left); `printf` alone would take a quarter of the 64 KB. `hello` is 878 bytes.
- Time: SNTP on every Wi-Fi connection, the time zone is a host setting.

### E-paper policy
Implemented in `components/epd/refresh_policy.c` (pure, host-tested). **The policy's numbers are user settings**: fields of the host's settings with a range each, stored in NVS and handed to the display service at start. The form for them in the web UI is still to come. The defaults start from Waveshare's recommendations for the 4.2" V2:
- data-driven updates (`app_on_data`, a timer, the clock) - no more often than once per **60 s**, counted from the start of the last such refresh, so that a clock can show minutes: what the user did in between - opened the application, pressed a button - does not put the next one off, and one that comes a second early is in time. Waveshare's own advice is 180 s and the panel is rated for a million refreshes; a user who wants the panel to last longer raises the number;
- reaction to a button - the user is looking at the device - **at once**, no pause by default; the panel's own partial refresh (about half a second) is the only limit;
- full refresh - after **5** partial ones;
- at least one update per **24 hours** (the host's keep-alive task).

Not configurable:
- Full refresh when the application on the scene changes; the first update after start is always full.
- The panel goes to deep sleep (mode 1, RAM is kept) after every full refresh and 1.5 s after the last partial one, otherwise it degrades irreversibly. Its voltages are off after every refresh either way: the controller does that itself. The chip does not go into light sleep while the panel is awake, and the panel's lines keep their levels when it does (`gpio_sleep_sel_dis`).
- **Fast refresh** is a setting (`refresh_fast`, off by default, on the page): Waveshare's `Init_Fast`, its 1.5 s waveform. A full refresh is 2.2 s with it and 2.7 s without, were 3.1 s. Not for the cold.
- No animations and no real-time progress bars in the host's own screens. An application that wants a row of pictures has `HOST_REDRAW_NO_BLINK`; the panel itself takes half a second for a partial refresh of the whole frame (0.41 s of it BUSY), and the application its time to draw, so that is three pictures in two seconds at best. `sdk/apps/badapple` is the test: 200x150 unpacked and drawn in about 0.1 s a frame by the interpreter.
- 1 bit per pixel. Fonts are bitmap, pre-converted (`tools/fontgen`), ASCII; the readable minimum is ~10 px.

### Web UI (on the device, `esp_http_server`)
What exists:
- `GET /` - the page. One HTML file, no frameworks, in the device's own black and paper: a menu on the left, the section on the right. **Apps** - a tile for every application on the device, with its icon, its version and Remove (and Update when a catalog has another version); the last tile is a plus that opens what the catalogs have and the device does not, and is disabled when the six places are taken; a gear on a tile opens the application's settings and the button beside it what the application is - its manifest in words: where it connects, how often, what it keeps, whether it starts with the device and has system access (`#apps:weather:info`); the gear of the section the list of sources: the official one first and fixed, up to three more added and removed by the row. **Device** - what the device is, the firmware (the latest release asked of GitHub by the browser, an image to install), the log, restart and reset. **Settings** - in tabs: the name with the default application and the menu's idle timeout, the time zone and the time server, the display, the network. **About** - the author, the licenses, what it is made with. `#apps:weather` and `#apps:catalogs` open the dialogs by a link.
- `GET` / `POST /api/settings` - the host's own settings but the network: the name, the time server, the time zone, and how the panel is treated - seconds between refreshes nobody asked for, hours before a refresh is forced, the full refresh after so many quick ones (0: always full, 255: never, the way e-readers offer it), and whether greys are dithered. What can take effect at once does.
- `POST /api/restart`.
- `GET /api/status` - name, firmware, slot, whether the image is on probation, Wi-Fi phase, network, address, free memory, and why the device last started (`reset_reason`: 3 a restart asked for, 4 a fault, 5 the interrupt watchdog). After a fault `last_panic` is its record, kept in RTC memory through the restart (`main/crash_note.c`): how many starts ago, the reason, the task, the program counter and the code addresses found on the stack - `riscv32-esp-elf-addr2line -pfiaC -e build/perinkpheral.elf <addresses>` names the place.
- `GET /api/log` - the last 4 KB of the device's log, as text: what the console would have shown, kept in memory for when there is no cable.
- `POST /api/wifi` `{"ssid", "password"}` - validated like every setting, stored, and the device restarts to join.
- `GET /api/apps` - the installed applications, each with its settings as the manifest declares them and their values (a secret only as set or not); `POST /api/apps/settings?app=<name>` `{"key": "value"}` - checked against the manifest, stored in the application's store, the application told (`app_on_settings()`) and its polls fetched again at once; `POST /api/apps/remove?app=<name>` - its files; the list of applications is read anew, with no restart; `GET /api/apps/removed` - the names of removed applications whose settings are kept: the page offers those first when adding one, marked "settings kept". The page's Applications section is a form for each.
- `POST /api/time` `{"tz"}` - the time zone, a POSIX TZ string; it takes effect at once, with no restart. The page works the string out from the browser's own zone and offers it; a device that was never told its zone gets it along with the Wi-Fi network, so setting the device up is one step.
- `POST /api/reset` - factory reset: the settings and the applications' stores are erased on the way up, and the device comes back with its setup access point. The page has a button for it.
- `/api/dev/...` - the development tools, only in a build with `APP_DEV_CONSOLE`: `GET screen` (a 1-bit BMP of what the panel shows), `POST button`, `POST open`, `POST app` (upload a file of an application), `POST run` (restart into an application), `POST flash` (a firmware image, with nobody asked), `POST crash?kind=wdt|abort` (a deliberate fault, to test the record and the way back).
- `POST /api/ota/release` `{"url", "size"}` - the device fetches the image of a release itself, from the project's releases on GitHub and nowhere else, through the redirect they are behind, and installs it as an upload. The page's "Check for updates" asks GitHub for the latest release, compares its tag (`v0.1.0`) with the firmware's version and offers the update. A release is built with `APP_VERSION=0.1.0` and carries `perinkpheral.bin`.
- `POST /api/ota`, `/api/ota/release`, `/api/reset` and `/api/wifi` wait for Confirm on the device's own screen, a minute at most (see Open).
- `POST /api/ota` - a firmware image as the request body (`curl --data-binary @build/perinkpheral.bin http://<address>/api/ota`): written into the spare slot, and the device restarts into it.

**Firmware over the network.** The upload is refused before a byte is written unless it is an image of this project (the image and description magic numbers and the project name) built for the partition table the device has. The new image starts on probation: it is confirmed only once the way the next update would arrive is open again - the web server is up and the device is on its network, or, with no network stored, its setup access point has stayed up for 20 seconds. An image that is not there within 3 minutes is rolled back by the bootloader. The receiving (`ota_stream.c`) and the verdict (`ota_health.c`) are pure and host-tested; `main/ota.c` writes the slot.

### Catalogs
Applications are installed the way a Debian system installs packages: the device knows a few **sources**, each the address of a folder of static files, and reads the index in it; nothing but the index tells it what is there. No server, no git on the device, no building: any hosting that serves files over HTTPS will do, and HTTPS it has to be: a source that is not `https://` is refused.
- `tools/catalog/mkcatalog.py <folder> --out <catalog>` builds every application of a folder and writes `catalog.json` beside them: for each its name, title, version, `abi`, and its files with their sizes and SHA-256.
- The official applications are a repository of their own, https://github.com/GrakovNe/perinkpheral-apps - `clock`, `weather`, `timer`, `stopwatch`, `ha`, `calendar`, `badapple`, `kit` - and its `catalog` folder is always the first source of a device: it is not stored and cannot be removed. The user adds up to three more. What the firmware's image carries is `hello` alone: the SDK's sample, there for a developer to read and for a new device to have something to open. It is not in the catalog - once removed, it comes back only with a flash over the cable. This repository keeps the SDK, the tools and the sample.
- **Six places.** A device holds at most 6 applications; a catalog may hold five hundred. The page installs and removes in a click, and **an application that is removed keeps what it stored** - its settings among it - for when it comes back (`&forget=1` on the removal erases that too), until the stores' partition is full: then what removed applications left is the first to go.
- The page passes the index on from the source as it comes (`GET /api/catalog?source=<n>`): the device does not read it. Installing (`POST /api/apps/install`) downloads the files the index lists, takes each only if its size and SHA-256 are what the index says, reads the manifest with the parser that runs applications - one made for another `abi` is refused with those words. The files are put in place as one step (`app_store_commit_all`: a mark before the first, taken away after the last), and a start that finds the mark finishes it, so a loss of power does not leave a new manifest beside an old module. Nothing restarts: the host reads the list of applications anew (`EVENT_APPS_CHANGED` - the scenes are made again, the poller's schedule too, and an application that was on the screen and is gone gives it to the menu). `GET` / `POST /api/sources` are the list.
- There are no signatures: what vouches for an application is HTTPS to the source the user chose. The application is in the sandbox all the same.

### Power and profiles
The battery is connected. There is no detector of the power source in version 1, so the profile is a host setting, `power_saving`: 0 never (`plugged`, the default), 1 always (`battery`), 2 auto. It is in Settings on the device (General, Power saving: Disabled, Always, Auto) and on the page. **Auto** saves whenever no computer is on the USB cable, which is the one thing the board can tell (`usb_serial_jtag_is_connected`: the host's packets, looked for every 5 s): plugged into a computer the device is as when plugged, otherwise as on its battery - and a charger looks like the battery. It becomes "on battery" in the version of the hardware that can tell (a divider on D9: version 2).
- **plugged** - Wi-Fi always on, the web UI available, any `every`, polls on schedule.
- **battery** - **light sleep** between polls: memory, application state and the framebuffer are kept; wake-up by timer and by any of the four buttons (`gpio_wakeup_enable`). Wi-Fi comes up for the poll and goes down; the web UI is available for 2 minutes after a button press, after power-up and after the profile is changed. The minimum `every` is 300 s, anything smaller is rounded up. The board's current in light sleep has not been measured - measure it before any promises about battery life.
  - How it is built (`main/power.c`): the chip sleeps by itself whenever no task runs (ESP-IDF power management with tickless idle; `light_sleep_enable` follows the setting at run time). The radio is counted: the poller holds it while something waits for the network and gives it back after, a press holds it for two minutes, and with nobody holding it `wifi_set_radio(false)` stops Wi-Fi without the plan taking that for a lost network. A network that does not answer in 20 s is given up until the next poll's least distance. The buttons task waits for a level instead of sampling every 5 ms, or the chip would never sleep. The header's icon says how the last attempt went.
  - With a computer on the other end of the USB cable the chip does not sleep (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`), so the console and flashing work on the bench - and so sleep itself has only been seen to be allowed, not seen: the radio's schedule was watched from the network, the current was not measured.
- **Extreme power saving - deep sleep between polls - was built, seen working on the device and taken out on 2026-11-12, put off to version 2** (the owner's decision: too much to get right for now). Every wake-up was a cold start, only buttons 1-3 woke the device, and what had to outlive the sleep lived in RTC memory; the last commit that has it is `4aec49d`. What it showed: the device slept two minutes after the last press, woke itself for a poll and by a button, and a poll over HTTPS kept it awake about 8 s.
- Estimates, none of them measured (4000 mAh, 85 % usable). Light sleep keeps the chip and its memory on: with a guessed 0.5-2 mA between polls it is 2-4 months at a poll every 15 min and 1-2 months at 5 min, and less while mDNS wakes the chip ten times a second (see Open). Wi-Fi always on while on battery is days at best. Deep sleep, for when it returns: about a year at 15 min with a 3 s wake-up, about six months with the 8 s a poll over HTTPS takes.

### TRMNL compatibility - as an application, not as a platform
TRMNL is a thin client: once per `refresh_rate` the device does `GET /api/display`, receives JSON with an `image_url`, downloads a PNG/JPEG/BMP and draws it; the protocol has no interactivity (one button, its action is set by the server through `special_function`); 4.2" is not in the list of models. So TRMNL is not adopted as a whole. Instead there is the `trmnl` application:
- `polls`: `GET {{server}}/api/display` with the headers `ID` (MAC), `Access-Token`, `FW-Version`, `Width: 400`, `Height: 300`, `Battery-Voltage`, `RSSI`; `every` is taken from the response's `refresh_rate`. We do not measure the battery voltage - `Battery-Voltage` carries a constant.
- `app_on_data`: parses `image_url`/`filename` (cache by filename - do not download the same thing again), downloads the picture (a 1-bit 400×300 BMP preferably; PNG only if a decoder fits into the arena), draws it with `gfx_bitmap`.
- Works with any BYOS server (Terminus, byos_fastapi and others) - they read `Width`/`Height` from the headers and render for 400×300. The TRMNL cloud is not a goal (BYOD license, 800×480).
- Gives access to the TRMNL plugin ecosystem with no changes in the host.

## Decided, being built

An architecture review on 2026-11-12 found the structure behind most of the open items; these are the owner's decisions on it. Until a line here is built the code is as the sections above describe it, and where the two disagree this section is where the code is going.

**Who may change what.** *(Built. The poller keeps its own copy of the list of applications and takes a new one, in the host loop, after the list was read anew; nothing of it is held while applications run their last handlers.)*
- The host loop owns the host's state: the settings, the list of installed applications, the scenes. Every other task - the web server, the poller, Wi-Fi - reads a **published snapshot** and changes nothing.
- A snapshot carries a version: a reader notes it, copies, and copies again if it changed meanwhile. No lock on reading, anywhere; a reader may see what was true a moment ago.
- A change is a **command through the event queue**: the handler posts it and waits for the host loop's answer, as the development tools do. A page waits up to the length of a refresh; that is fine.
- Posting into the queue may be refused when it is full, and says so: the caller decides (a command answers "Device busy.", a deadline stays due, a press is lost).
- `settings.c` holds the settings and the one function that checks, stores and applies them.
- An application is installed in two steps: the files are fetched into a staging place from the web server's task, and put in place by the host loop together with reading the list anew.

**How an application is addressed.** *(Built: ids, the schedule and the alarms by id, several alarms. Not yet: intents.)*
- Inside the host an application is its **id: 32 bits of the SHA-256 of its module and its manifest**. A new version is another application to everything that was waiting for the old one; the same version installed again is the same. System applications have ids the same way. The name stays for people and for the disk: the folder, the catalog, the page, the store, the application to return to after a restart.
- The schedule of polls, the requests and the alarms are kept **by id**. Removing an application removes its own; nothing else touches them.
- Several alarms an application: `alarm_set(id, seconds)`, `alarm_set_at(id, unix_time)`, `app_on_alarm(id)`.
- **Intents by subscription**: an application names in its manifest (`"intents"`) the kinds of news it wants, and gets them through `app_on_intent(type)`, loaded for it if it is not. Three kinds: the time changed (`time_changed`), the network is there again (`network_up`), and the device was powered up or restarted (`started`). **`started` is how an application runs without being opened**: a restart forgets alarms, a poll's own pace and requests under way, and an application that asked is loaded off the screen after it and sets them going again from its store - a calendar puts its alarm back, a messenger makes its first request. Polls need none of this: they come from the manifest.  A kind the host does not know fails the build in `tools/mkapp`.
- `app_on_terminate()` is called before an instance is unloaded, whatever the reason - it left the screen, a delivery in the background is done, the device restarts or sleeps, the application is removed. `app_on_blur()` keeps its narrow meaning and comes first. State is saved in `app_on_terminate`.

**Time.** *(Built: `components/deadlines`. What is left outside is the minute after an update in which the new firmware proves itself, and the buttons' debounce.)*
- **One module owns every deadline** of the host and does nothing else: the polls, the alarms, the timers of the application on the screen, the panel's daily refresh, the while after a press, Wi-Fi's retries. Set a deadline, take it back, ask for the nearest; one that has come is an event in the queue.
- **The scale is absolute time.** The RTC always has one - the user's, the network's or 1970 - and runs through sleep. Behind the API everything is a moment: "in 20 minutes" is turned into one when it is set.
- When the clock is changed - by the network or by hand - deadlines that were set as "in so long" move with it, and those set as "at such a time" stay.

**Stopping.**
- A restart, a factory reset and going to sleep are one sequence in the host loop: `app_on_terminate` for the loaded application, the panel's refresh to its end, the poller and the radio stopped, what the chip has to know when it wakes written down, and only then the restart or the sleep. No waiting on the off chance: every step is known to be done.

**Memory.**
- *(Built: `components/wasm3/m3_area.c`, 128 KB.)* RAM is two areas. The **application area** is one block taken at start: the application's page of 64 KB and the interpreter's working memory, of one fixed size for every application, reset as a whole when the application is unloaded. The **host's heap** is the rest, and no application ever touches it. What the host has is then the same whichever application is on the screen. Measured on the device (2026-11-12): over its page an application takes about 11 KB that the interpreter needs for any, and about two bytes for every byte of its module, or four for every byte of the module's code alone - the sample 12 KB, the weather 26 KB, Settings (11 KB of code) 35 KB. The area is the page and **64 KB** (48 KB until 2026-11-13: the calendar, 12 KB of code, was measured at 53 KB with all of it run, and the owner gave applications the room); that, and not the size of a module's file, is what an application has to fit, and one of 13 KB of code that all runs is about as large as there can be - `tools/mkapp` says so before the device does. Applications are built for size (`-Oz`, link-time optimization) and the interpreter keeps compiled code in pages of 512 bytes: with pages of 4 KB every application took 6-8 KB more. With the area taken the host has 82 KB free at the menu, 68 KB with an installed application on the screen (its module is still a copy in the heap) and 22 KB of that left while a TLS session is open. A developer declares nothing.
- *(Built: `main/app_code.c`.)* An application's **code is executed from flash** and never copied into RAM, as the system applications' already is: a partition of six slots of 52 KB (`appcode`, 320 KB), one an installed application, mapped read-only. A slot is whole sectors and as small as holds the largest module; they were five of 64 KB until 2026-11-13, and the change needed no new partition table. A module arrives as a file, as before - from the catalog, from the development tools, in the firmware's image - and the host loop moves it into a slot the next time it reads the list of applications. The host writes a slot when an application is installed and erases it when one is removed; the slot is 64 KB, a module at most 48. The manifest, the files and the store stay where they are.
- This changes the partition table, which cannot be changed over the air: the one device there is gets flashed over USB, once. *(Built.)* An image made for another partition table is refused by the update before a byte is written: every image carries, right after its description, a number made of `partitions.csv` (`image_layout.c`, `ota_layout_t`), and the device takes only its own. An image from before that is refused the same way.
- The poller looks at the largest free block before a TLS session, not at the sum.

**The contract.**
- `http_request(tag, ...)`: the application chooses the tag, a number it has a name for, and the answer comes with it. Tags are an application's own; two applications with the same one never meet.
- `net_is_online()` goes. An application asks for data; "there is no network" is one of the answers, and a request that failed says why: no network, no answer, a host the manifest does not allow.
- The header's icon says whether the last use of the network worked.
- The system API gives applications by id, not by their place in a list.
- `"system": true` stays one consent for all of it.
- *(Built.)* `main/poller.c` is three: which polls are wanted and at what pace (`poll_schedule.c`, pure - when each is due is a deadline, the alarms are deadlines outright), the one that talks to the network (`poller_net.c`), and what ties them to the host (`poller.c`: the task, the radio, the requests waiting). The task sleeps until it is woken and has no clock of its own. Calling the host loop and waiting for it is `events_call_patiently()`.

Looked at and left: the settings as one table instead of a field in five places (it is only typing, once applying them is one function); memory an application declares in its manifest (a developer cannot know the number - the area is the same for all); blocks that move and a heap that is compacted, as the systems of the nineties had (the host is C over ESP-IDF, and one application at a time with an area that is reset gives the same for nothing).

## Repository layout
```
firmware/              the ESP-IDF project (build from this folder)
  main/                entry point, host loop, event queue, scenes, views (screens), services, settings (NVS), the web server and its page, OTA
  components/
    appconf/           the host settings struct, defaults, validate (pure)
    board/             what is not on the header: the antenna switch
    deadlines/         every deadline of the host: the table (pure) and the one timer behind it
    epd/               SSD1683 driver + display service + partial/full policy
    gfx/               primitives, the square font, Departure Mono bitmaps (fonts/) (pure)
    input/             buttons, debounce, short/long press
    ui/                scene interface and the window manager (pure)
    power/             plugged/battery/extreme profiles, light and deep sleep, wake-up sources (milestone 4)
    appsys/            one wasm application in memory, the host API as it sees it, the application as a scene, manifests and the store of installed applications (pure)
    wasm3/             the interpreter, v0.9.0 (MIT), with one local change described in its README
; the setup access point's name server
    qr/                QR codes: Project Nayuki's generator v1.8.0 (MIT), and the text that joins a phone to a network (pure)
    poller/            polls schedule, http_request queue, HTTP client, streamed app_on_data delivery (milestone 2)
    store/             catalog, install/remove in LittleFS, settings in NVS (milestone 5)
    web/               the rest of the web UI: applications, catalog, settings (milestone 5)
  test/host/           host tests of the pure modules (tinytest)
sdk/
  host_api.h           the only contract; applications and the host's import table are built from it
  cmake/               applications as a CMake project for an IDE: the code understood, build, run and the page as targets
  lib/                 what applications link instead of a C library; json_pick, which the host builds too; app_ui, what the buttons do on the kit's screens;
                       app_draw, app_state: text by a terminated string, a versioned struct in the store, settings read by key -
                       what every application wrote for itself until 2026-11-14
  companion/           a sample of a companion page
  apps/
    hello/             the sample: text, a reaction to a button, a value that survives. The one
                       application built into the firmware's image
tools/
  fontgen/             TTF → bitmap (ASCII); mkpack.py - the glyph packs of letters beyond ASCII;
                       mkspelling.py - the tables by which what has no glyph is spelled in ASCII
  preview/             renders the host's views to PNG on the development machine - look before flashing
  devtool/             the device over the network: screenshots, presses, the log, upload and run an application, flash
  devshot/             the same over USB for a device with no network: presses and the frame its panel shows
  mkapp/               builds app.wasm (wasi-sdk) and checks its imports and memory
  mkpicture/           drawings - PNG, PBM, rows of '#' and '.' - into the pictures an application carries in its
                       code; mkapp runs it over an application's `pictures` folder and the code includes pictures.h
  mkvideo/             a video into a 1-bit stream small enough to be a file of an application
  catalog/             builds a folder of applications into a catalog a device installs from
  strings/             the translations of applications as one CSV for a person to correct, and back
assets/
  fonts/               DepartureMono-Regular.otf (OFL), its license
  mark/                the project's mark, two corners of an empty frame: SVG and PNG, bare and on paper
docs/
  guide/               the application platform's datasheet and development guide: guide.html, build.py (HTML to PDF
                       with a Chromium browser), the PDF
hardware/              everything about the physical device
  README.md            panel, datasheet, bench notes
  bom/                 the "hardware and assembly" page (links into render/ and step/)
  step/                the STEP model of the case
  render/              case renders from Blender (not in git)
```
`firmware/system` holds the system applications: `settings`, `about` and `setup`. `power`, `store`, `web` and the `plex` and `trmnl` applications do not exist yet; the poller lives in `main/poller.c` and `main/poller_net.c`, not in a component of its own.

## Milestones
1. **Applications without the network.** The host loads `hello.wasm` from LittleFS; the application draws text and reacts to a button; `clock.wasm` is switched to through a minimal host menu (long press 4). A check of running on battery without USB. Host API of the milestone: `gfx_clear`, `gfx_text`, `gfx_rect`, `gfx_bitmap`, `gfx_text_width`, `gfx_flush`, `timer_set`, `time_now`, `log`; exports `app_init`, `app_on_button`, `app_on_timer`, `app_on_focus`, `app_on_blur`. Start with `sdk/host_api.h` and `components/appsys`.
2. **Drivers and the SDK.** Done when a user can write an application by the rules of the day, make a network request with the host's tools and come out of the background when the answer arrives; use the SDK for the buttons and for state; and set the application up through the web. The contract is **not frozen** at the end of it.
   - **Done**: Wi-Fi with setup and rescue, SNTP and the time zone, OTA over the network, fonts by size, down/up next to short/long, `kv_*`, timers, the log, `tools/mkapp`, the development tools.
   - **Drawing and time**: colour, local time taken apart, system icons, the redraw flags.
   - **The network for applications**: the `poller`, `http_request`, poll control, HTTPS with the full set of root certificates in the firmware, JSON picked apart by the host, delivery to an application that is not on the screen and `request_focus`.
   - **Settings through the web**: an Applications section on the device's page - what is installed, and a form for each from its manifest; `app_on_settings()`.
   - **Files and the alarm.**
   - **The first real application is `weather`** (Open-Meteo: no key, no account), with icons drawn for it; `plex` comes after.
3. **Host screens.** The operating system's own interface, designed and drawn: the menu and application list, the first-start and Wi-Fi setup screens, status and error screens, what the device shows while it updates or has nothing to show.
4. **Power.** The `battery` profile on light sleep (wake-up by timer and button, rounding of `every`, switching profiles by the setting) - built, the radio's schedule seen on the device; "extreme power saving" on deep sleep - built, seen working, taken out and put off to version 2; measured current and real battery life numbers - not yet.
5. **Web UI and catalog.** Install from `catalog.json`, application and host settings (including the e-paper policy), the log, firmware updates pulled from releases (`esp_https_ota`).

## State
Done and verified on hardware: the display driver (full refresh 2.19 s of BUSY, partial 0.41 s), buttons with short and long presses, the refresh policy, drawing and fonts, the host kernel (display service with two frames, window manager, scenes, one event queue) with built-in scenes - the menu, a device page, the splash and the setup screen - and wasm applications installed on LittleFS: the menu lists what is there, `hello` counts presses and `clock` shows the time, the day of the week and the date and redraws once a minute, host tests of the pure modules.

Verified on the owner's network: joining it, SNTP and the date in the header, local time by the stored time zone, the web page, a firmware upload with its probation (both slots, several times), returning to the last application after a restart, the factory reset and setting the device up again after it (the owner did it with the QR code and reported it working; which phones open the page by themselves was not written down), and the development tools over the network.

Open:
- **A refresh that meets a TLS handshake is slow** - seen at 3 s instead of 0.45, and once at 4.7 s instead of 2.2 at start. The poller's task runs above the host loop; that the priority is the cause is a guess until it is tried. Put off by the owner (2026-11-12), not dropped. One cause of refreshes lost outright while the flash was busy is found and fixed: the panel's reset pulse came out long (see `hardware/README.md`).
- **mDNS and the battery.** Its timer wakes the chip ten times a second for as long as it runs, radio or no radio (`CONFIG_MDNS_TIMER_PERIOD_MS`), which the `battery` profile cannot afford. Built (2026-11-12): the responder runs only while somebody may be looking for the device - always while plugged, and for the two minutes after a press while saving (`power_on_reachable`, `announce_name` in `main.c`); between polls it is not there. Not yet seen on the device, and whether the chip then sleeps is still a current nobody measured. To think about again: a device on its battery is found by its name only after a press.
- **What the network may do without a hand on the device.** Built (2026-11-12, the owner's choice of scope): what cannot be taken back - a firmware update (`/api/ota`, `/api/ota/release`), a factory reset, a change of the Wi-Fi network, the first setup included - is asked about on the device's own screen (`scene_confirm.c`, native, a notice of the host's - see Host screens), and the request waits for the answer, a minute at most; unanswered is "Not confirmed." and a second request meanwhile "Device busy.". No password, no window of time. Still open to anybody on the network, by that choice: installing and removing applications, settings, restart. The development tools ask nobody and have a firmware upload of their own (`/api/dev/flash`): they are not in a build for release. Requests made by pages of other sites through a browser are refused already (`Origin`, `Host`), reading as well as changing: only the page itself is given to anybody.
- **The interrupt watchdog restarts, found (2026-11-14).** Three in two days; the panic record named the place: ESP-IDF 5.5.5, `temp_sensor_get_raw_value()` of `esp_hw_support`, which the radio calls when it turns its front end off (`phy_close_rf`, modem sleep). It waits `200 - (now - then)` microseconds with interrupts off, and when the timer reads earlier than `then` the wait is the low 32 bits of a negative number - up to 71 minutes. `main/delay_guard.c` wraps `esp_rom_delay_us` and makes a busy wait of over a second a millisecond; `/api/status` counts them as `delays_cut`. Why the timer reads earlier is not known - the wrap removes the consequence, not the cause. To report to Espressif.
- The down and up events of real buttons have not been seen on the device.
- A firmware upload replaces the firmware and not the applications: after a change of the host API the ones on the device do not start and say "Update the app." until they are installed again from the catalog - `devtool.py flash --apps` does both for the sample. This is a matter of the time before the first release only: after it the API is versioned and stays compatible.
- An application uploaded with the development tools is still started by restarting the device (`devtool.py run`); installing and removing through the page or Settings do not restart it.
- The time zone the page works out from a browser is a rule of fixed dates; a zone that changes its clocks otherwise (Morocco) comes out approximate.
- `hardware/render` is a symlink to `render/` in the repository root while Blender is still writing there by an absolute path; when the renders are done, replace the link with the folder itself.

The five milestones of the operating system are done but for what is listed above as open or put off. Next: applications for people - Home Assistant first, then a calendar by WebCal, then Plex with the certificate of the user's own - and, once two of them have tried the contract on servers that are not ours, the first release, with which the host API is versioned and stays compatible. The guide for application developers is `docs/guide` - a datasheet, a guide and a reference in one PDF, built from `guide.html` by `build.py` - and is preliminary until the SDK is fixed: a change of `sdk/host_api.h` is a change of it. Its translation is `guide.ru.txt` - every piece of text and its Russian under it - which `units.py` puts into the page (`build.py --language ru`); `units.py missing ru` lists what a change of the guide left untranslated.

## Conventions
- Everything is written strictly in English, with a plain hyphen `-`, never an en or em dash.
- **Interface text is short and standard**: on the panel, on the web page, in the answers of the API. The words every interface uses - Save, Update, Remove, Install, Factory reset, "Invalid value.", "Storage error." - and not a sentence of our own making ("Read them again", "refreshes nobody asked for"). No paragraphs of help under a field: a label that needs explaining is the wrong label. Simple, exact, competent.
- Nothing in the repository depends on one developer's machine: no home-directory paths, no fixed serial port names, no OS-specific steps in code, tools or documentation.
- JSON documents - manifests, the page's requests and answers - are read and written by `components/appsys/json_tree.c`, the project's own: a tree, integers only, no floating point (cJSON and what it pulled of the C library were 9.7 KB of the image). Answers that arrive in pieces are `sdk/lib/json_pick.c`.
- SPDX header in every source file, C11. Logic that does not need hardware is pure and host-tested; `settings.c` is the only owner of the host's NVS.
- When debugging hardware: the vendor's reference code and the datasheet first, hypotheses after. Do not suggest "check the wires".

## Branding
- **The logo is the word `perINKpheraL` set in the device's square font: lower case `per` and `phera`, capital `INK` and a capital `L` at the end, all of one height in the same squares, with no plate, frames or dots.** The mix of capitals and lower case inside one word is the mark itself. On the panel - black on white, scale 5 (27 px squares), 357 px.
- **The mark is two corners that frame an empty square**: the top right one and the bottom left one, a stroke each, set apart on the diagonal - the frame of a screen with nothing asking for attention in it. The owner found it on 2026-11-13 in the host's own `update` icon (`gfx_icons.c`, eleven cells: the corners are six cells a side and stand three cells apart) and named it the icon and the logo of the whole project. The word `perINKpheraL` stays the wordmark; the mark is what stands where a word does not fit - a favicon, an avatar, an icon of an application. It is drawn in `assets/mark` (SVG and PNG, bare and on paper) from the icon's own cells, and stands as the icon of the device's page in a browser, at the head of the three repositories' READMEs and as the icon of the Home Assistant integration. **It stays the `update` icon too** (the owner: "a superb decision that the icon is right there"): the question about a new firmware is asked under the project's own mark.
- The square font: every glyph is a square on a grid of 3 strokes × 2 gaps (5+6+5+6+5 = 27 px), no ascenders or descenders, diagonals allowed. Implementation: `firmware/components/gfx/font_square.c`.
- The splash at power-up is the About application's picture: under the mark the version of the build, a QR code of the project's address, and `(c) 2026 Max Grakov` and `MIT License` in the small font. When the application cannot be shown there is no splash: the device goes straight to where the splash would have led.
- **The square font is for the word `perINKpheraL` only.** All other text on the device (splash, menu, host applications) is set in **Departure Mono** (SIL OFL 1.1, https://github.com/rektdeckard/departure-mono), a monospaced pixel font. It is rasterized by `tools/fontgen` once, at 11 px, where it lands on its own pixel grid; every size on the device is that bitmap with each dot drawn 1, 2, 3, 4, 6 or 10 pixels large, so a new size costs no flash. It was chosen on the panel against Jersey 20, Jersey 25 and Terminus. **Jersey** (SIL OFL 1.1, https://github.com/scfried/soft-type-jersey; heavier, proportional) is the second family the user can pick. The number in a Jersey face's name is the height of its capitals in pixels on its own grid, so Jersey 25 is the medium size and Jersey 15 the size of the host's text; the files are `assets/fonts/Jersey{10,15,20,25}-Regular.ttf` with `OFL-Jersey.txt`. The file is `assets/fonts/DepartureMono-Regular.otf` with `OFL-DepartureMono.txt`. Outline fonts rasterized to 1 bit (Tektur was the first try) come out thin and ragged at this pixel density - use fonts drawn for the pixel grid. Fonts are OFL/CC0 only.
- Palette: black #141413 and paper #E9E7E1 (on the device - the panel's native grey-white). Every host screen is black on white. No halftones, dithering or gradients.
- Do not propose: a plate or frame around `INK`, dithered letters, white on black, decorations on the splash, other typefaces.

## Do not
- Do not pull in Arduino, ESPHome, LVGL, GxEPD2, Home Assistant.
- Do not give applications sockets: the network is only `polls` and `http_request` in the host's queue, by the `hosts` list.
- Do not build a declarative JSON layout into the core (it can be one of the applications).
- Do not keep more than one wasm instance in memory or more than one TLS connection.
- Do not use pointer callbacks in the ABI - named exports only.
- Do not version the host API before the first release: change `host_api.h` freely and rebuild the applications. After a release a changed signature will mean a new `abi`.
- Do not design for PSRAM: there is none.
- Do not measure the battery voltage and do not detect USB - decided.
- Do not "optimize" the panel driver past Waveshare's own: its commands, its waveforms and its waits as they are, CS per byte, bounded BUSY waits. No waveforms of our own (`0x32`).
