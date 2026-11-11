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
- Fonts: the square one (code, `font_square.c`) for the logo; Departure Mono, a monospaced pixel font, for everything else: one bitmap of it at its own 11 px from `tools/fontgen`, drawn at whole multiples - 22 px for the host, and 11, 22, 33, 44, 66 and 110 px for applications. ASCII only in version 1.
- FS: LittleFS (partition `apps`, 1 MB). NVS for the applications' `kv_*` and the host settings.
- Network: `esp_http_client` (TLS through mbedTLS), `esp_http_server` for the web UI.
- The display driver is our own, on top of `spi_master`: init, full refresh, partial refresh, panel deep sleep after every update. **SPI 2 MHz**, mode 0, CS toggled per byte - otherwise the refresh never finishes (see `AGENTS.md`).
- Buttons: `gpio_config` input without the internal pull-up (the pull-up is on the module), active LOW, debounce 20 ms. A press is reported as it happens (down, up) and as what it meant (short, or long at ≥600 ms).

## Memory budget (the hard limit of the C6)
- SRAM 512 KB, about 250-300 KB free after Wi-Fi + TLS. There is no PSRAM and there will be none.
- Flash 4 MB: two OTA slots of 0x170000 (~1.44 MB) + 320 KB of slots for the code of applications (`appcode`) + LittleFS 704 KB (manifests and files of applications). A firmware larger than a slot means no OTA. The firmware is built for size (`-Os`, the interpreter too: for speed it was 7 % faster and 12.7 KB larger) and is 1.22 MB with TLS and the full set of root certificates in it: 18 % of the slot is free. The certificates are 69 KB of that; if the slot gets tight, they can move into a file on LittleFS (`esp_crt_bundle_set`), which would also let them be renewed without a new firmware. The partition table is fixed - it cannot be changed over the air.
- A wasm application's arena is **exactly 64 KB** (one wasm page, there is nothing smaller), fixed for everyone. Build: `-Wl,--initial-memory=65536 -Wl,--max-memory=65536 -Wl,-z,stack-size=8192` (`tools/mkapp` does it); the interpreter is built with a limit of one page, so a module that asks for more does not load and none can grow.
- **One** wasm instance in memory at any moment; measured on the device, a loaded application costs 77 KB (the 64 KB page plus the interpreter) and all of it comes back when it leaves the screen. Free heap: 391 KB idle and 314 KB with an application on the screen before Wi-Fi was in the firmware; with Wi-Fi up as a setup access point, 236 KB idle - the radio costs about 155 KB, more than was budgeted, and its buffers have not been tuned yet. Delivering data to an application that is not on the scene = unload the scene → load the owner → deliver → bring the scene back (100-300 ms).
- An HTTP response body goes to the application as a stream, in 1-2 KB chunks, while TLS is open; the host never holds it whole. Peak budget: TLS ~45 KB + one instance ~80 KB + the host.
- Two frames of 400×300×1 bit = 15 KB each: the back buffer the host draws into and the display service's copy of what the panel shows.

## Architecture

### Application
`/apps/<name>/app.wasm` + `/apps/<name>/manifest.json` on the LittleFS partition. The host reads `name`, `title`, `version`, `abi`, `fullscreen` and `polls` today and skips what it does not know yet, so a manifest with `hosts` and `settings` already loads. A poll is an `http(s)` `url`, an `every` of 10 seconds to a day and up to four `headers`; there are at most four polls, and the application knows each by its number. `{{key}}` in a URL or a header value is replaced by the value stored under that key (`text_template.c`). `name` is 1-13 of `a-z`, `0-9`, `-`, `_`: it is the folder, the key in the catalog and, as `a.<name>`, the NVS namespace of the application's store, and it must be the folder's name. `title` is at most 23 bytes. The module is at most 48 KB. The applications in `sdk/apps` are built and packed into the partition image with the firmware (`APP_BUNDLE_APPS`). The full manifest, as it will be:
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
  "abi": 2
}
```
**An application may bring an icon**, in its manifest and optional: `"icon"` is 11 rows of 11 characters, `#` for ink and `.` for paper - the form the host's own icons have in the source, which a person can type, which follows the icon rules by construction and which scales. The device draws it beside the title in its menu, a cell two pixels; the web page draws it on the application's tile; a catalog's index carries it, so that an application has a face before it is installed.

**Settings are declared in the manifest, not by code**, the way Pebble did it with a companion page: nobody types a token with four buttons. The device's web page has an Applications section - what is installed, and a settings form for each, drawn from the manifest without running the application; the catalog will live in the same place. A field is a `text`, a `secret` (the page can write it and cannot read it back), a `number`, a `choice` from a list of `options`, a `toggle` or a `place`; it has a `key`, a `title` and may have a `help` line and a `default` - what the page shows while nothing is set; nothing is stored for it, so the application means the same when it finds nothing. At most 8 of them. A `place` is asked for by its name: the page offers places as the name is typed (the browser asks Photon, a search of OpenStreetMap; the device does not) and fills the two `number` settings the place names as its `latitude` and `longitude`, which it does not show. The values are stored in the application's own store under the keys the manifest names, so the code reads a setting with `kv_get` by the same key, and the host substitutes it into URLs and headers (`{{key}}`). A change arrives as `app_on_settings()`; an application that is not on the screen is loaded for the call and unloaded after it, as for data.

**JSON is picked apart by the host.** A poll or a request names the paths it wants (`current.temperature_2m`, `daily.weather_code[0]`); the host finds them in the stream and hands the application the values, so an application does not parse anything. One that wants the answer as it came asks for it raw and gets the pieces through `app_on_data`. Ease of writing an application is worth the host's memory here.

**HTTPS** verifies the server against the full set of root certificates, the browsers' one, carried in the firmware (60-70 KB of flash, to be measured). If the firmware stops fitting into its slot, this is the first thing to think about again. A server in the home network with a certificate of its own making is not trusted; plain `http` is what works there until somebody needs more.

### System applications and the system API
The host's own pages are applications too - written with the same SDK, run by the same interpreter - so that the host is built with what it gives to others. They are **system applications**: built from `firmware/system`, carried inside the firmware image (an update over the network brings them along, so they always speak the API of the host they run on), never installed and never removed, and outside the five places. Their names begin with `sys.`, which no installed application's name can. `Settings`, `Device` and `About` are there today. `Settings` is the device's settings with four buttons and no keyboard: General (date and time - from the network or by hand, the time zone by a city from a list, power saving, which has one value yet), Apps (the installed ones, each can be removed), Display (the three numbers of the refresh policy), Wi-Fi (the network, the address, forgetting it) and Reset. Nothing is typed: a value is one of a few stepped through with UP and DOWN, a date is its parts stepped the same way with OK to the next and BACK to the one before, and what cannot be undone asks first. What needs text is set from the web page. `Device` is the device's name, its network and address, the firmware and its slot, memory and uptime, all asked of the host with `sys_info`. `About` is the mark, the version of the build (`v0.1.0-0adbfb4`: the version the project is at and the commit), a QR code of the project's address and, in the small font at the bottom, the author and the license. 

What stays native is what has to work when nothing else does: the menu and the message of an application that crashed. The screen that says how to give the device a network is a system application too (`setup`, not in the menu): without it the device starts at the menu, and Device has the address. The splash at power-up is not among them: it is About's picture, and without it the device simply starts.

They are written with the **system API**, the `sys_*` part of `sdk/host_api.h` (`sys_info`: the version, the name, the network and its state, the address, the slot, free memory, the time zone; `sys_number_get` / `sys_number_set` and `sys_text_set`: the host's settings; `sys_time_set`; `sys_app_title` / `sys_app_remove`; `sys_do`: restart, forget the network, factory reset; `sys_logo`). It is not fenced off: the device is its owner's, and any application may have it. It has to ask - `"system": true` in its manifest - and the page says "<title> requests system access." before it installs one. A module that imports a `sys_*` call without having asked is refused by `tools/mkapp` and by the host.

### Execution model
- **Scene** - one application, it owns the screen and the buttons.
- **Polling** - the host itself fetches every `polls` entry of every installed application at its `every`. This is the background mechanism ("pushes").
- **Raw requests** - an application may itself call `http_request` (method, URL, headers, body) - for OAuth, calendars, actions on a button. Requests go into the host's queue, one at a time; only hosts from the manifest's `hosts` are allowed. The response arrives through the same `app_on_data`.
- **Delivery** - the host calls `app_on_data(req_id, ptr, len, flags)` in chunks (flags: FIRST, LAST, ERROR; the HTTP status is in the first chunk) on the owning application, **whether it is on the scene or not**:
  - on the scene - the call is made right away;
  - not on the scene - unload the scene (`app_on_blur`) → load the owner → deliver → if it called `request_focus()` it stays as the scene, otherwise unload it and bring the previous scene back (`app_on_focus`).
- **State** - every application must restore itself from `kv_*`: the host unloads it on every scene change and background delivery. The framebuffer is not touched during the swap (the screen is not redrawn); the host remembers the scene's timers and restarts them after the return.
- **Buttons** - the layout is the same everywhere in the host: 1 up, 2 ok, 3 down, 4 back. Applications are encouraged to keep it.
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

- **Two frames.** The back buffer (`host.c`) is where frames are put together; the display service holds the front copy of what the panel shows. Drawing never touches the panel, an unchanged frame costs no refresh, and a scene cannot spoil the screen halfway.
- **Layers are areas of one frame**, not buffers of their own: the header and the content do not overlap. A scene that is not full-screen gets a viewport under the header and cannot draw outside it.
- **A scene never refreshes anything.** It reports that its picture changed and draws when asked; the window manager passes on why the frame changed (a switch, a press, data), and the display service decides what the panel does.
- **One event queue** (`events.c`). A press, later a timer or a response from the network, is posted from its own task and taken off by the host loop one at a time: nothing in the host runs concurrently with anything else.
- **The menu is a scene** like any other, not a layer over an application: opening it takes the screen away from whatever had it.
- **Development tools** (`APP_DEV_CONSOLE`, on by default, off for a release): the device is driven and looked at without hands on it. Over the network, `/api/dev` gives the frame the panel shows, takes presses, opens an application and takes a new build of one; `tools/devtool` is the command line for it, and `devtool.py run <app dir>` builds an application, uploads it and restarts the device into it in about ten seconds, with no reflashing. Over the cable, characters typed into the USB console do the same for a device with no network (`tools/devshot`). Whatever touches the frame or the scenes is handed to the host loop through the event queue.

### Power-up
The splash is drawn once and the device moves on by itself after a few seconds (a press does it at once):
- **no network stored** - to the setup screen: the name of its access point and the address to open;
- **otherwise** - back to the application that was on the screen last (its name is kept in NVS), or to the menu when there was none.

A device on a shelf has to come back from a restart showing what it showed, without a press.

### Wi-Fi
- **No network stored** - the device puts up an open access point named after itself (`Perinkpheral-XXXX`) and waits to be set up at `192.168.4.1`. The setup screen shows a QR code that joins a phone to that access point, with the network name and the address in words under it. A QR code cannot both join a network and open a page, so the access point is a captive portal: its name server (`captive_dns.c`) answers every name with the device, the web server redirects whatever it does not know to its page, and the phone opens the page by itself. The name server answers only while the access point is up. Not yet tried with a phone.
- **A network stored** - it connects; a lost or failed connection is tried again after 2, 4, 8 ... at most 30 seconds.
- **Two minutes without success** - rescue: the setup access point comes up as well and the device keeps trying once a minute, so a wrong password never needs the cable. Connected again, the access point goes away.
- The rules are `components/net/wifi_plan.c`, pure and host-tested; `wifi.c` only carries them out. The Wi-Fi library keeps nothing in flash: the credentials are the host's settings.
- Time comes from SNTP whenever there is a network, in the user's time zone (a POSIX TZ string in the settings). Everything a person reads is local time: the header, the status page, the applications through `time_zone_offset()`. Until the first answer the device does not know the time: the header shows no date and `time_now()` is 0.

### Header
The host draws a header above its own screens and above every application that does not ask for the full screen:
- On the left the Wi-Fi icon in one of two states - connected, or not connected (the same icon struck through). It shows the outcome of the last connection attempt, which is what matters in the profiles where Wi-Fi is off between polls.
- On the right the date (`Wed 11 Nov`), blank while the time is unknown.
- Nothing else of the host's own: no application title, no network name, no clock - the date changes once a day, so the header never asks for a refresh of its own. There is no footer.
- **An application may put its own text into both**: beside the icon on the left, where the host has nothing, and on the right in place of the date (`header_text`). It gives a line and nothing more - the font and the size are the host's. A text that does not fit is cut with `...`, and the two never meet: when together they are too wide each is sure of half the width, and what one does not need of its half goes to the other (`wm_header_share`, `gfx_text_fit`). A side the application gave nothing for is the host's; a text of one space leaves it empty. The texts are forgotten when the application leaves the screen. `weather` shows its city and the date, each behind a setting of its own.
- An application chooses in its manifest: `"fullscreen": false` (the default) leaves it 400×260 under the header, with its coordinates starting there, and it cannot draw over the header; `"fullscreen": true` gives it the whole 400×300. A long press on button 4 opens the menu in either mode.
- The network name and the address are on the device page of the menu, not in the header.

### Host API (wasm imports)
The only contract is `sdk/host_api.h`; applications and the host's import table are built from it, and `tools/mkapp` refuses a module that imports anything else. It is not frozen and, until the first release, not versioned: the contract is simply changed, with no backward compatibility, and the applications are rebuilt - the ones in `sdk/apps` with the firmware, the ones in perinkpheral-apps by `build.sh` there. `HOST_API_VERSION` and the manifest's `abi` exist for later and stay at 2 until then. What exists today:
```
gfx_width() / gfx_height()             // the surface: 400x300 full-screen, 400x260 under the header
gfx_text(x, y, font_id, str_ptr, len)  // y is the top of the line; returns the width drawn
                                       // fonts: 0 text 22 px, 1 medium 33, 2 large 44, 3 display 66, 4 huge 110, 5 small 11; all of ASCII in each
gfx_text_width(font_id, str_ptr, len) -> px
gfx_font_height(font_id) -> px          // the whole line
gfx_font_ascent(font_id) -> px          // from the top of the line to the baseline
gfx_font_cap_height(font_id) -> px      // capitals and digits above the baseline
gfx_color(0..16)                       // how dark the calls below draw: 0 paper, 16 ink, between them a grey by dithering
gfx_rect(x, y, w, h, fill)
gfx_line(x0, y0, x1, y1)
gfx_bitmap(x, y, w, h, bits_ptr)       // a set bit takes the colour, a clear one leaves what is there
gfx_qr(x, y, cell, str_ptr, len)       // a QR code of the text, up to 57 modules a side; returns the modules, cell 0 only measures
request_redraw(flags)                  // the picture is out of date; flags: SOON - show it now, CLEAN - a full refresh,
                                       // NO_BLINK - now, and never as a full refresh: one picture of a row
request_exit()                         // done: give the screen back, as BACK does in the host's pages
header_text(side, str_ptr, len)        // its own line in the host's header, left or right; len 0 gives the side back
request_focus()                        // from app_on_data off the screen: worth showing now; the host decides
timer_set(id, ms)                      // one shot, 0 cancels; fires app_on_timer(id)
kv_get(key, key_len, buf, cap) -> len  // the application's own store
kv_set(key, key_len, buf, len)         // len 0 removes the key
net_is_online() -> 0 | 1
http_request(method, url, headers, body) -> request number   // only to the manifest's "hosts"
poll_now(n)                            // fetch poll n of the manifest now, not when it is due
poll_set_interval(n, seconds)          // another pace for it, until the device restarts
alarm_set(seconds)                     // app_on_alarm() once, on the screen or off it; 0 takes it back
file_size(name, name_len) -> bytes     // a file from the application's package (its `files` folder)
file_read(name, name_len, offset, buf, cap) -> bytes read
file_write(name, name_len, offset, buf, len)  // inside the file or at its end; len 0 at 0 removes it
time_now() -> unix seconds             // 0 while the time is unknown
time_zone_offset() -> seconds          // add to time_now() for the user's local time
time_local(struct_ptr)                 // the local time taken apart: year, month, day, weekday, hour, minute, second
time_uptime_ms() -> ms since start
log(level, str, len)
```
**What the contract still needs** - agreed, not written yet; in the order it will be built:
- **System icons**: `gfx_icon(id, x, y, scale)` from the host's set, drawn by the rules of the header's icon.
- **A picture straight from a file**: `gfx_bitmap` reading a file - a picture is 15 KB and does not fit an application's hands at once.

Done since and seen on the device: requests of an application's own (`http_request`: one at a time for the whole device, two waiting per application, only to the manifest's `hosts`, the answer in pieces through `app_on_data` under a number of 16 or more), files (`file_read`, `file_write`, 100 KB an application), the alarm (`sdk/apps/timer`: a countdown that takes the screen back from another application when it ends), `poll_now`. The alarm is kept in memory: a restart forgets it, and deep sleep will need it in RTC memory.

**Few applications, large ones**, the way Pebble did it: at most **5** installed, each with a module of up to 48 KB, 100 KB of files and 4 KB of `kv_*`. The partition is 1 MB; five such applications are about 780 KB of it. Whoever wants more rotates them - one day from a companion application on a phone.

**Put off: BLE.** The chip has BLE 5 and no classic Bluetooth. As a host service it could be media keys - the device pairs as a keyboard and an application sends play, pause, next, volume to whatever it is paired with, with no API of the player. What plays now can be read that way only from an iPhone (Apple Media Service); Android and computers give it over classic Bluetooth, which is not here. It is not in the plan of milestone 2: the stack is estimated, not measured, at 200-300 KB of flash and 60-90 KB of memory, next to a firmware slot with about 300 KB left and a heap that TLS still has to fit into. Measure before deciding.

**How soon a redraw is shown** follows from where it was asked: from `app_on_button` at once, from `app_on_timer` and `app_on_data` by the e-paper policy. Flags on `request_redraw`: `HOST_REDRAW_SOON` - show it now although nobody pressed anything; `HOST_REDRAW_CLEAN` - a full refresh, for a new picture; `HOST_REDRAW_NO_BLINK` - now, and not as the full refresh the host makes after every few partial ones: for pictures in a row. The traces then pile up until the first refresh that does not ask for this, or until the application leaves. None has a budget: the device is made by developers for developers, and one who wants to spend the battery and the panel may. The only floor is one refresh a second.

What a handler may count on: one handler runs at a time and has 3 seconds; it returns nothing and leaves requests (`request_redraw`, `request_exit`, `request_focus`) that take effect when it returns; presses are not lost, timers of one id collapse into one, and the pieces of one answer arrive in order with nothing between them.

There are no pointer callbacks. An application exports functions with fixed names (the host skips the missing ones):
```
app_init()
app_on_focus() / app_on_blur()
app_on_button(event)                // BTN_UP | BTN_OK | BTN_DOWN | BTN_BACK with _KEYDOWN, _KEYUP as it happens
                                    // and _PRESSED, _LONG_PRESSED as what it meant: BTN_OK_PRESSED
app_on_timer(id)
app_on_draw()                       // paint the whole picture from the state
app_on_data(request, ptr, len, flags, http_status)   // a piece of the answer to poll number `request`
app_data_buffer() -> ptr            // where the host puts that piece: 1 KB, sdk/lib has it ready
```
- **An application draws only inside `app_on_draw()`.** Handlers change the state and call `request_redraw()`; the host calls `app_on_draw()` when it needs the frame, with a white surface, and the `gfx_*` calls answer with an error anywhere else. The host puts the frame together, compares it with what the panel shows and decides whether and how the panel is refreshed - so there is no `gfx_flush`, no partial-or-full choice in the application's hands, and an application cannot leave a half-drawn screen.
- **The module is not trusted.** One that imports anything outside the host API is refused before it runs. Every pointer and length it passes is checked against its 64 KB before the host touches it, and coordinates and sizes beyond a few thousand pixels are refused, so that no call can cost more than the surface is large. A handler has 3 seconds: one that is still running then - a loop without an end - is stopped, and recursion is stopped before it runs through the host's stack. A handler that traps or is stopped ends the application, and the screen says so.
- **Data arrives as values or in pieces.** `main/poller.c` fetches every poll of every installed application when it is due, one request at a time, over HTTP and HTTPS. A poll with `"pick"` in the manifest is read to the end by the host, the connection is closed, and the application gets the values in one `app_on_values()` call, reading them with `data_value()` (`sdk/lib/app_values.h` turns them into numbers and text). Any other answer goes to the application as it comes: the host reads up to 1 KB, copies it into the buffer the application named with `app_data_buffer()` and calls `app_on_data()` - flags say first and last, the HTTP status comes with the first piece, and a request that failed is one call with `HOST_DATA_FAILED` and no data. Either way an application that is not on the screen is loaded for the delivery and unloaded after it (`app_delivery.c`). Seen on the device: `weather` asking Open-Meteo over HTTPS, on the screen and off it; a request takes about 6 seconds, most of it the TLS handshake, and the free heap goes down to about 80 KB while it lasts. The pieces path has run only under the host tests.
- **State lives in the store.** An application is unloaded whenever it leaves the screen, so what must survive goes through `kv_get` / `kv_set`: up to 256 bytes per key, keys of up to 15 characters, in NVS. The namespace is `a.<name>` and an application's name cannot hold a dot, so no application can reach the host's settings or another application's values. `hello` reads its count when it starts and saves it in `app_on_blur()`.
- **No floating point.** The interpreter is built without it - the chip has no FPU, and soft float in the host was 34 KB - and without reference types, extended constants and compact imports; a module still has its one table of functions and calls through pointers. `tools/mkapp` refuses an application whose code has a float operation and says which; one that got onto the device anyway stops when it reaches it. Numbers are integers - tenths, hundredths - and `sdk/lib/app_values.h` reads them that way.
- **No C library.** Applications are linked with `-nostdlib` against `sdk/lib`, a few hundred bytes of string building and the `memcpy` family; `printf` alone would take a quarter of the 64 KB. `hello` is 878 bytes.
- Time: SNTP on every Wi-Fi connection, the time zone is a host setting.

### E-paper policy
Implemented in `components/epd/refresh_policy.c` (pure, host-tested). **The policy's numbers are user settings**: fields of the host's settings with a range each, stored in NVS and handed to the display service at start. The form for them in the web UI is still to come. The defaults start from Waveshare's recommendations for the 4.2" V2:
- data-driven updates (`app_on_data`, a timer, the clock) - no more often than once per **60 s**, counted from the start of a refresh, so that a clock can show minutes. Waveshare's own advice is 180 s and the panel is rated for a million refreshes; a user who wants the panel to last longer raises the number;
- reaction to a button - the user is looking at the device - **at once**, no pause by default; the panel's own partial refresh (about a second) is the only limit;
- full refresh - after **5** partial ones;
- at least one update per **24 hours** (the host's keep-alive task).

Not configurable:
- Full refresh when the application on the scene changes; the first update after start is always full.
- After every update the panel goes to deep sleep (mode 1, RAM is kept), otherwise it degrades irreversibly.
- No animations and no real-time progress bars in the host's own screens. An application that wants a row of pictures has `HOST_REDRAW_NO_BLINK`; the panel itself takes a second for a partial refresh (0.4 s of it the panel, the rest sending two frames at 2 MHz), so that is one picture a second at best. `sdk/apps/badapple` is the test: 200x150 unpacked and drawn in about 0.1 s a frame by the interpreter.
- 1 bit per pixel. Fonts are bitmap, pre-converted (`tools/fontgen`), ASCII; the readable minimum is ~10 px.

### Web UI (on the device, `esp_http_server`)
What exists:
- `GET /` - the page. One HTML file, no frameworks, in the device's own black and paper: a menu on the left, the section on the right. **Apps** - a tile for every application on the device, with its icon, its version and Remove (and Update when a catalog has another version); the last tile is a plus that opens what the catalogs have and the device does not, and is disabled when the five places are taken; a gear on a tile opens the application's settings, the gear of the section the list of sources: the official one first and fixed, up to three more added and removed by the row. **Device** - what the device is, the firmware (the latest release asked of GitHub by the browser, an image to install), the log, restart and reset. **Settings** - in tabs: the name, the time zone and the time server, the display, the network. **About** - the author, the licenses, what it is made with. `#apps:weather` and `#apps:catalogs` open the dialogs by a link.
- `GET` / `POST /api/settings` - the host's own settings but the network: the name, the time server, the time zone, and how the panel is treated - seconds between refreshes nobody asked for, hours before a refresh is forced, the full refresh after so many quick ones (0: always full, 255: never, the way e-readers offer it), and whether greys are dithered. What can take effect at once does.
- `POST /api/restart`.
- `GET /api/status` - name, firmware, slot, whether the image is on probation, Wi-Fi phase, network, address, free memory.
- `GET /api/log` - the last 4 KB of the device's log, as text: what the console would have shown, kept in memory for when there is no cable.
- `POST /api/wifi` `{"ssid", "password"}` - validated like every setting, stored, and the device restarts to join.
- `GET /api/apps` - the installed applications, each with its settings as the manifest declares them and their values (a secret only as set or not); `POST /api/apps/settings?app=<name>` `{"key": "value"}` - checked against the manifest, stored in the application's store, the application told (`app_on_settings()`) and its polls fetched again at once; `POST /api/apps/remove?app=<name>` - its files; the list of applications is read anew, with no restart. The page's Applications section is a form for each.
- `POST /api/time` `{"tz"}` - the time zone, a POSIX TZ string; it takes effect at once, with no restart. The page works the string out from the browser's own zone and offers it; a device that was never told its zone gets it along with the Wi-Fi network, so setting the device up is one step.
- `POST /api/reset` - factory reset: the settings and the applications' stores are erased on the way up, and the device comes back with its setup access point. The page has a button for it. Like the upload, it asks for nothing.
- `/api/dev/...` - the development tools, only in a build with `APP_DEV_CONSOLE`: `GET screen` (a 1-bit BMP of what the panel shows), `POST button`, `POST open`, `POST app` (upload a file of an application), `POST run` (restart into an application).
- `POST /api/ota/release` `{"url", "size"}` - the device fetches the image of a release itself, from the project's releases on GitHub and nowhere else, through the redirect they are behind, and installs it as an upload. The page's "Check for updates" asks GitHub for the latest release, compares its tag (`v0.1.0`) with the firmware's version and offers the update. A release is built with `APP_VERSION=0.1.0` and carries `perinkpheral.bin`.
- `POST /api/ota` - a firmware image as the request body (`curl --data-binary @build/perinkpheral.bin http://<address>/api/ota`): written into the spare slot, and the device restarts into it.

**Firmware over the network.** The upload is refused before a byte is written unless it is an image of this project (the image and description magic numbers and the project name). The new image starts on probation: it is confirmed only once the way the next update would arrive is open again - the web server is up and the device is on its network, or, with no network stored, its setup access point has stayed up for 20 seconds. An image that is not there within 3 minutes is rolled back by the bootloader. The receiving (`ota_stream.c`) and the verdict (`ota_health.c`) are pure and host-tested; `main/ota.c` writes the slot.

Still to come:
- `POST /api/focus/<name>` - bring onto the scene.
- Host settings (power profile, e-paper policy, time zone, catalog URL).

### Catalogs
Applications are installed the way a Debian system installs packages: the device knows a few **sources**, each the address of a folder of static files, and reads the index in it; nothing but the index tells it what is there. No server, no git on the device, no building: any hosting that serves files over HTTPS will do.
- `tools/catalog/mkcatalog.py <folder> --out <catalog>` builds every application of a folder and writes `catalog.json` beside them: for each its name, title, version, `abi`, and its files with their sizes and SHA-256.
- The official applications are a repository of their own, https://github.com/GrakovNe/perinkpheral-apps - `clock`, `weather`, `timer`, `badapple` - and its `catalog` folder is always the first source of a device: it is not stored and cannot be removed. The user adds up to three more. What the firmware's image carries is `hello` alone, and the official catalog carries `hello` too (`build.sh` there takes it from `sdk/apps`): whatever a user removes, they can install again. This repository keeps the SDK, the tools and the sample.
- **Five places.** A device holds at most 5 applications; a catalog may hold five hundred. The page installs and removes in a click, and **an application that is removed keeps what it stored** - its settings among it - for when it comes back (`&forget=1` on the removal erases that too).
- The page passes the index on from the source as it comes (`GET /api/catalog?source=<n>`): the device does not read it. Installing (`POST /api/apps/install`) downloads the files the index lists, takes each only if its size and SHA-256 are what the index says, reads the manifest with the parser that runs applications - one made for another `abi` is refused with those words. Nothing restarts: the host reads the list of applications anew (`EVENT_APPS_CHANGED` - the scenes are made again, the poller's schedule too, and an application that was on the screen and is gone gives it to the menu). `GET` / `POST /api/sources` are the list.
- There are no signatures: what vouches for an application is HTTPS to the source the user chose. The application is in the sandbox all the same.

### Power and profiles
The battery is connected. There is no USB detector and there will be none, so the profile is a host setting, `power_saving`: off is `plugged`, the default, and on is `battery`. It is in Settings on the device (General, Power saving) and on the page. "Extreme power saving" is a separate setting on top of `battery`, not built yet.
- **plugged** - Wi-Fi always on, the web UI available, any `every`, polls on schedule.
- **battery** - **light sleep** between polls: memory, application state and the framebuffer are kept; wake-up by timer and by any of the four buttons (`gpio_wakeup_enable`). Wi-Fi comes up for the poll and goes down; the web UI is available for 2 minutes after a button press, after power-up and after the profile is changed. The minimum `every` is 300 s, anything smaller is rounded up. The board's current in light sleep has not been measured - measure it before any promises about battery life.
  - How it is built (`main/power.c`): the chip sleeps by itself whenever no task runs (ESP-IDF power management with tickless idle; `light_sleep_enable` follows the setting at run time). The radio is counted: the poller holds it while something waits for the network and gives it back after, a press holds it for two minutes, and with nobody holding it `wifi_set_radio(false)` stops Wi-Fi without the plan taking that for a lost network. A network that does not answer in 20 s is given up until the next poll's least distance. The buttons task waits for a level instead of sampling every 5 ms, or the chip would never sleep. The header's icon says how the last attempt went.
  - With a computer on the other end of the USB cable the chip does not sleep (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`), so the console and flashing work on the bench - and so sleep itself has only been seen to be allowed, not seen: the radio's schedule was watched from the network, the current was not measured.
- **extreme power saving** - off by default: **deep sleep** between polls. Every wake-up is a cold start: the application is loaded anew and lives only from `kv_*`, timers do not survive the sleep, the first draw after wake-up is a full refresh. Only buttons 1-3 (GPIO0-2) wake the device; button 4 (GPIO21) does not wake it from deep sleep - the menu is available after waking with any other. BSSID/channel are kept in RTC memory for a fast reconnect.
- Estimate for deep sleep, 4000 mAh and 85 % usable capacity (wake-up ~3 s at ~85 mA, ~50 µA asleep): a poll every 15 min ≈ 1 year, 5 min ≈ 4.5 months, 1 min ≈ 4 weeks. There are no numbers for light sleep until it is measured. Wi-Fi always on while on battery is hours, not days.

### TRMNL compatibility - as an application, not as a platform
TRMNL is a thin client: once per `refresh_rate` the device does `GET /api/display`, receives JSON with an `image_url`, downloads a PNG/JPEG/BMP and draws it; the protocol has no interactivity (one button, its action is set by the server through `special_function`); 4.2" is not in the list of models. So TRMNL is not adopted as a whole. Instead there is the `trmnl` application:
- `polls`: `GET {{server}}/api/display` with the headers `ID` (MAC), `Access-Token`, `FW-Version`, `Width: 400`, `Height: 300`, `Battery-Voltage`, `RSSI`; `every` is taken from the response's `refresh_rate`. We do not measure the battery voltage - `Battery-Voltage` carries a constant.
- `app_on_data`: parses `image_url`/`filename` (cache by filename - do not download the same thing again), downloads the picture (a 1-bit 400×300 BMP preferably; PNG only if a decoder fits into the arena), draws it with `gfx_bitmap`.
- Works with any BYOS server (Terminus, byos_fastapi and others) - they read `Width`/`Height` from the headers and render for 400×300. The TRMNL cloud is not a goal (BYOD license, 800×480).
- Gives access to the TRMNL plugin ecosystem with no changes in the host.

## Decided, being built

An architecture review on 2026-11-12 found the structure behind most of the open items; these are the owner's decisions on it. Until a line here is built the code is as the sections above describe it, and where the two disagree this section is where the code is going.

**Who may change what.** *(Built, but for the poller, which still looks at the launcher's own list while it is held.)*
- The host loop owns the host's state: the settings, the list of installed applications, the scenes. Every other task - the web server, the poller, Wi-Fi - reads a **published snapshot** and changes nothing.
- A snapshot carries a version: a reader notes it, copies, and copies again if it changed meanwhile. No lock on reading, anywhere; a reader may see what was true a moment ago.
- A change is a **command through the event queue**: the handler posts it and waits for the host loop's answer, as the development tools do. A page waits up to the length of a refresh; that is fine.
- Posting into the queue may be refused when it is full, and says so: the caller decides (a command answers "Device busy.", a deadline stays due, a press is lost).
- `settings.c` holds the settings and the one function that checks, stores and applies them.
- An application is installed in two steps: the files are fetched into a staging place from the web server's task, and put in place by the host loop together with reading the list anew.

**How an application is addressed.**
- Inside the host an application is its **id: 32 bits of the SHA-256 of its module and its manifest**. A new version is another application to everything that was waiting for the old one; the same version installed again is the same. System applications have ids the same way. The name stays for people and for the disk: the folder, the catalog, the page, the store, the application to return to after a restart.
- The schedule of polls, the requests and the alarms are kept **by id**. Removing an application removes its own; nothing else touches them.
- Several alarms an application: `alarm_set(id, seconds)`, `alarm_set_at(id, unix_time)`, `app_on_alarm(id)`.
- **Intents by subscription**: an application names in its manifest (`"intents"`) the kinds of news it wants, and gets them through `app_on_intent(type)`, loaded for it if it is not. To begin with: the time changed, the network is there again. A kind the host does not know fails the build in `tools/mkapp`.
- `app_on_terminate()` is called before an instance is unloaded, whatever the reason - it left the screen, a delivery in the background is done, the device restarts or sleeps, the application is removed. `app_on_blur()` keeps its narrow meaning and comes first. State is saved in `app_on_terminate`.

**Time.**
- **One module owns every deadline** of the host and does nothing else: the polls, the alarms, the timers of the application on the screen, the panel's daily refresh, the while after a press, Wi-Fi's retries. Set a deadline, take it back, ask for the nearest; one that has come is an event in the queue.
- **The scale is absolute time.** The RTC always has one - the user's, the network's or 1970 - and runs through sleep. Behind the API everything is a moment: "in 20 minutes" is turned into one when it is set.
- When the clock is changed - by the network or by hand - deadlines that were set as "in so long" move with it, and those set as "at such a time" stay.

**Stopping.**
- A restart, a factory reset and going to sleep are one sequence in the host loop: `app_on_terminate` for the loaded application, the panel's refresh to its end, the poller and the radio stopped, what the chip has to know when it wakes written down, and only then the restart or the sleep. No waiting on the off chance: every step is known to be done.

**Memory.**
- *(Built: `components/wasm3/m3_area.c`, 112 KB.)* RAM is two areas. The **application area** is one block taken at start: the application's page of 64 KB and the interpreter's working memory, of one fixed size for every application, reset as a whole when the application is unloaded. The **host's heap** is the rest, and no application ever touches it. What the host has is then the same whichever application is on the screen. Measured on the device (2026-11-12): an application takes its page and, over it, about three bytes for every byte of code it runs - the sample 18 KB, the weather 29 KB, Settings 38 KB. The area is the page and **48 KB**; that, and not the size of a module's file, is what an application has to fit, and one of 12 KB of code is about as large as there can be. With the area taken the host has 82 KB free at the menu, 68 KB with an installed application on the screen (its module is still a copy in the heap) and 22 KB of that left while a TLS session is open. A developer declares nothing.
- *(Built: `main/app_code.c`.)* An application's **code is executed from flash** and never copied into RAM, as the system applications' already is: a partition of five slots of 64 KB (`appcode`), one an installed application, mapped read-only. A module arrives as a file, as before - from the catalog, from the development tools, in the firmware's image - and the host loop moves it into a slot the next time it reads the list of applications. The host writes a slot when an application is installed and erases it when one is removed; a module may be 64 KB. The manifest, the files and the store stay where they are.
- This changes the partition table, which cannot be changed over the air: the one device there is gets flashed over USB, once. An image made for another partition table is refused by the update.
- The poller looks at the largest free block before a TLS session, not at the sum.

**The contract.**
- `http_request(tag, ...)`: the application chooses the tag, a number it has a name for, and the answer comes with it. Tags are an application's own; two applications with the same one never meet.
- `net_is_online()` goes. An application asks for data; "there is no network" is one of the answers, and a request that failed says why: no network, no answer, a host the manifest does not allow.
- The header's icon says whether the last use of the network worked.
- The system API gives applications by id, not by their place in a list.
- `"system": true` stays one consent for all of it.
- `main/poller.c` becomes three: the schedule with the alarms (pure), the one that talks to the network, and what ties them to the host. Calling the host loop and waiting for it moves to `events.c`.

Looked at and left: the settings as one table instead of a field in five places (it is only typing, once applying them is one function); memory an application declares in its manifest (a developer cannot know the number - the area is the same for all); blocks that move and a heap that is compacted, as the systems of the nineties had (the host is C over ESP-IDF, and one application at a time with an area that is reset gives the same for nothing).

## Repository layout
```
firmware/              the ESP-IDF project (build from this folder)
  main/                entry point, host loop, event queue, scenes, views (screens), services, settings (NVS), the web server and its page, OTA
  components/
    appconf/           the host settings struct, defaults, validate (pure)
    board/             what is not on the header: the antenna switch
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
  lib/                 what applications link instead of a C library
  apps/
    hello/             the sample: text, a reaction to a button, a value that survives. The one
                       application built into the firmware's image
tools/
  fontgen/             TTF → bitmap (ASCII)
  preview/             renders the host's views to PNG on the development machine - look before flashing
  devtool/             the device over the network: screenshots, presses, the log, upload and run an application, flash
  devshot/             the same over USB for a device with no network: presses and the frame its panel shows
  mkapp/               builds app.wasm (wasi-sdk) and checks its imports and memory
  mkvideo/             a video into a 1-bit stream small enough to be a file of an application
  catalog/             builds a folder of applications into a catalog a device installs from
assets/
  fonts/               DepartureMono-Regular.otf (OFL), its license
hardware/              everything about the physical device
  README.md            panel, datasheet, bench notes
  bom/                 the "hardware and assembly" page (links into render/ and step/)
  step/                the STEP model of the case
  render/              case renders from Blender (not in git)
```
`firmware/system` holds the system applications: `settings`, `device`, `about` and `setup`. `power`, `store`, `web`, the `plex` and `trmnl` applications and `tools/catalog` do not exist yet; the poller lives in `main/poller.c`, not in a component of its own.

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
4. **Power.** The `battery` profile on light sleep (wake-up by timer and button, rounding of `every`, switching profiles by the setting) - built, the radio's schedule seen on the device; measured current and real battery life numbers, then "extreme power saving" on deep sleep - not yet.
5. **Web UI and catalog.** Install from `catalog.json`, application and host settings (including the e-paper policy), the log, firmware updates pulled from releases (`esp_https_ota`).

## State
Done and verified on hardware: the display driver (full refresh 2.19 s of BUSY, partial 0.41 s), buttons with short and long presses, the refresh policy, drawing and fonts, the host kernel (display service with two frames, window manager, scenes, one event queue) with built-in scenes - the menu, a device page, the splash and the setup screen - and wasm applications installed on LittleFS: the menu lists what is there, `hello` counts presses and `clock` shows the time, the day of the week and the date and redraws once a minute, host tests of the pure modules.

Verified on the owner's network: joining it, SNTP and the date in the header, local time by the stored time zone, the web page, a firmware upload with its probation (both slots, several times), returning to the last application after a restart, the factory reset and setting the device up again after it (the owner did it with the QR code and reported it working; which phones open the page by themselves was not written down), and the development tools over the network.

Open:
- **How memory is divided between the host and applications is not settled.** Seen on the device: an application that had been on the screen could not be loaded again after a delivery in the background ("Out of memory.") with 197 KB free - its page needs 64 KB in one piece, and after a TLS session the largest free block was 62 KB. Closed for the page itself: it is one block, taken at start and kept (`m3_reserve.c`). Not looked at yet: what else of an instance has to be found in the heap at every load (the interpreter's stack and code pages, about 13 KB), how much the host, the radio and TLS may take before an application cannot start, whether those should have budgets of their own, and what the device does when one is exceeded. `/api/status` says `largest_free_block` for this.
- Anyone on the network can change the device: the API asks for nothing - a firmware upload, a factory reset, the development tools. Right for development, wrong for a release. Decided, not built yet: an operation that changes the device is confirmed on the device itself - a screen that says what is asked for, with Cancel and Confirm - and the request waits for the answer. No password, no window of time. Requests made by pages of other sites through a browser are refused already (`Origin`).
- With Wi-Fi connected and an application on the screen the free heap is about 130 KB, and it has been as low as 107 KB. Polling will add TLS (~45 KB) on top; the radio's buffers have not been tuned.
- The host task has 12 KB of stack and applications run on it; the interpreter may take 6 KB (`d_m3MaxNativeStack`). Watch the headroom (`m` in the development console) as the host grows.
- The down and up events of real buttons have not been seen on the device.
- A firmware upload replaces the firmware and not the applications: after a change of the host API the ones on the device are refused ("Not a valid application") until they are uploaded again - `devtool.py flash --apps` does both. Nothing tells the user why; the `abi` number is there for this and is not checked against yet.
- An application uploaded with the development tools is still started by restarting the device (`devtool.py run`); installing and removing through the page or Settings do not restart it.
- The time zone the page works out from a browser is a rule of fixed dates; a zone that changes its clocks otherwise (Morocco) comes out approximate.
- `hardware/render` is a symlink to `render/` in the repository root while Blender is still writing there by an absolute path; when the renders are done, replace the link with the folder itself.

Milestone 1 is done, the run from the battery included (seen by the owner). Next - what is left of milestone 2: the rest of the contract as listed under Host API, the poller (the device half of data for applications), `http_request`, and `weather` as the first application with data.

## Conventions
- Everything is written strictly in English, with a plain hyphen `-`, never an en or em dash.
- **Interface text is short and standard**: on the panel, on the web page, in the answers of the API. The words every interface uses - Save, Update, Remove, Install, Factory reset, "Invalid value.", "Storage error." - and not a sentence of our own making ("Read them again", "refreshes nobody asked for"). No paragraphs of help under a field: a label that needs explaining is the wrong label. Simple, exact, competent.
- Nothing in the repository depends on one developer's machine: no home-directory paths, no fixed serial port names, no OS-specific steps in code, tools or documentation.
- SPDX header in every source file, C11. Logic that does not need hardware is pure and host-tested; `settings.c` is the only owner of the host's NVS.
- When debugging hardware: the vendor's reference code and the datasheet first, hypotheses after. Do not suggest "check the wires".

## Branding
- **The logo is the word `perINKpheraL` set in the device's square font: lower case `per` and `phera`, capital `INK` and a capital `L` at the end, all of one height in the same squares, with no plate, frames or dots.** The mix of capitals and lower case inside one word is the mark itself. On the panel - black on white, scale 5 (27 px squares), 357 px.
- The square font: every glyph is a square on a grid of 3 strokes × 2 gaps (5+6+5+6+5 = 27 px), no ascenders or descenders, diagonals allowed. Implementation: `firmware/components/gfx/font_square.c`.
- The splash at power-up is the About application's picture: under the mark the version of the build, a QR code of the project's address, and `(c) 2026 Max Grakov` and `MIT License` in the small font. When the application cannot be shown there is no splash: the device goes straight to where the splash would have led.
- **The square font is for the word `perINKpheraL` only.** All other text on the device (splash, menu, host applications) is set in **Departure Mono** (SIL OFL 1.1, https://github.com/rektdeckard/departure-mono), a monospaced pixel font. It is rasterized by `tools/fontgen` once, at 11 px, where it lands on its own pixel grid; every size on the device is that bitmap with each dot drawn 1, 2, 3, 4, 6 or 10 pixels large, so a new size costs no flash. It was chosen on the panel against Jersey 20, Jersey 25 and Terminus. **Jersey 25** (SIL OFL 1.1, heavier, proportional) comes later as an optional second font the user can pick in the host settings; it is not in the firmware yet. The file is `assets/fonts/DepartureMono-Regular.otf` with `OFL-DepartureMono.txt`. Outline fonts rasterized to 1 bit (Tektur was the first try) come out thin and ragged at this pixel density - use fonts drawn for the pixel grid. Fonts are OFL/CC0 only.
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
- Do not "optimize" the panel driver: CS per byte, one SPI device, bounded BUSY waits.
