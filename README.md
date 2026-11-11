# perINKpheral

A desk e-paper companion: a 4.2" e-ink panel, four buttons and an
ESP32-C6. It sits on the shelf and shows what you glance at - a line of
the schedule, the track that is playing - without a server behind it.

The name is `perinkpheral` in code, repositories and domains and
`perINKpheral` in prose and on the logo: peripheral vision, peripheral
device, ink.

**Status: a working prototype.** The hardware is assembled and
verified. The firmware runs wasm applications from its own file
system, sets itself up over Wi-Fi and takes firmware updates over the
network; fetching data for applications is half built. See
[Roadmap](#roadmap) for what exists and what does not.

## Vision

A separate screen for the desk that is not a second monitor: it can be
ignored until it is needed and read out of the corner of the eye. In
one phrase: a TRMNL without the server and with buttons.

The goal is an extensible operating system with its own applets on
hardware anyone can repeat.

## Principles

In the order they win arguments:

1. **Peripheral, not a monitor.** No backlight, nothing that moves,
   nothing that asks for attention. The device is silent until it is
   looked at, and what it shows is readable at a glance. This is why it
   is e-ink and why it refreshes rarely.
2. **Not a thin client.** The device does the work itself: it fetches
   the data, runs the application and draws the frame. No server
   prepares pictures for it, the vendor's or your own.
3. **Your data stays yours.** Showing a calendar must not require
   binding an account to a machine you do not own. Requests go from the
   device straight to the source; there is no cloud of ours in between
   and there never will be. It is also simply faster than a trip around
   half the globe.
4. **It has buttons.** A companion is something you can answer, not a
   picture frame: applications react to presses, and you are always one
   long press away from taking the screen back.
5. **Autonomous.** It runs from its battery for months and charges over
   USB-C; it does not need a cable, a hub or a computer next to it.
6. **Cheap to repeat.** The most common DIY parts, no custom PCB, a
   case simple enough to print in any plastic on any printer. If a
   choice makes the device harder to build at home, it needs a very
   good reason.
7. **You decide what is on the screen.** Not a fixed set of dashboards:
   an SDK and applets that anyone can write and install without
   reflashing. The host stays small and unchanging so that everything
   interesting can live in applications.

What follows from this:

- No backend of ours, no accounts, no telemetry.
- A static catalog of applications instead of a store service.
- TRMNL compatibility as one application, not as the platform.
- Everything is open under the MIT license.

## How it works

This section describes the design the firmware is being built to; the
roadmap says how much of it runs today.

The firmware is a small host that never changes, in the manner of
Pebble; everything you see is an application.

- **Applications** are WebAssembly modules with a `manifest.json`,
  installed from a catalog without reflashing. They run in a sandbox,
  each in a fixed 64 KB of memory, one at a time.
- **The host fetches, the application draws.** An application declares
  in its manifest which URLs to poll and how often; the host makes the
  requests and hands the response to the application, which draws with
  a small graphics API. The application on the screen can also make its
  own requests, to the hosts its manifest lists and nowhere else.
- **Buttons.** Four of them. Presses go to the application on the
  screen; a long press on button 4 always opens the host menu, where
  you switch applications with buttons 1 and 3 and confirm with 2.
- **An application can ask for the screen** when its data arrives in
  the background - a track that started playing, say. The host decides,
  and the long press on button 4 always gives the screen back to you.
- **Settings in a browser.** The device serves a web page for
  installing applications, entering their settings (tokens, addresses)
  and its own. The page is on the device; nothing leaves your network.
- **The screen is treated with care.** E-ink ages when it is refreshed
  too often, so data-driven updates are spaced minutes apart, a button
  press is answered at once, and there are no animations. The
  intervals are yours to change.

### Power

The battery is charged over USB-C. The device does not measure the
battery and cannot tell whether USB is plugged in, so the power profile
is a setting, and the screen shows no charge level.

| Profile | What it does |
| --- | --- |
| plugged (default) | Wi-Fi always on, the web page always reachable, any polling interval |
| battery | sleeps between polls, wakes on a timer or any button; polls no more often than every 5 minutes; the web page is reachable for 2 minutes after a button press |
| extreme power saving | deep sleep between polls; every wake-up is a cold start; buttons 1-3 wake the device, button 4 does not |

Battery life has not been measured yet. The estimate for 4000 mAh in
deep sleep is about a year at one poll per 15 minutes and about four
and a half months at one per 5 minutes.

## Hardware

| Part | Notes |
| --- | --- |
| Seeed Studio XIAO ESP32-C6 | RISC-V 160 MHz, 512 KB RAM, 4 MB flash, no PSRAM; Wi-Fi 6 (2.4 GHz), BLE 5; Li-ion charger on board |
| Waveshare 4.2" e-Paper Module | 400×300, 1 bit, SPI; the **V2** panel (SSD1683), jumper on 4-line SPI. The V1 panel has a different controller and will not work |
| Four-button module ("Independent key switch module 4P") | active low, pull-ups on the module |
| Li-ion 1S 3.7 V, 4000 mAh, **with a protection board** | on the BAT+/BAT- pads under the XIAO; the protection is mandatory, the board does not cut off over-discharge |
| Wires | no custom PCB |

| Signal | XIAO pin | GPIO |
| --- | --- | --- |
| e-Paper DIN | D10 | 18 |
| e-Paper CLK | D8 | 19 |
| e-Paper CS | D7 | 17 |
| e-Paper DC | D5 | 23 |
| e-Paper RST | D4 | 22 |
| e-Paper BUSY | D6 | 16 |
| e-Paper VCC, GND | 3V3, GND | |
| Button 1-4 | D0, D1, D2, D3 | 0, 1, 2, 21 |
| Buttons V, G | 3V3, GND | |
| Battery + / - | BAT+ / BAT- pads | |

D9 (GPIO20) is free.

The full bill of materials with the case, fasteners and assembly
drawings is the page `hardware/bom/index.html` (open it in a browser);
the case is `hardware/step/perinkpheral.step`. Everything learned about
the panel, with the datasheet and the pitfalls, is in
`hardware/README.md`.

## Build and flash

You need ESP-IDF 5.5, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases)
with `WASI_SDK_PATH` pointing at it (the firmware image carries wasm
applications), and the board on USB.

```bash
. "$IDF_PATH/export.sh"
cd firmware
idf.py set-target esp32c6
idf.py build
idf.py -p <port> flash
```

The port is `/dev/ttyACM*` on Linux and `/dev/cu.usbmodem*` on macOS.
If the board does not answer, hold BOOT while plugging it in.

### First start

A device with no network stored puts up an open Wi-Fi access point
named after itself, `Perinkpheral-XXXX`. Join it, open
<http://192.168.4.1>, enter your network and its password; the device
restarts and joins. If it cannot - a wrong password - its own access
point comes back after two minutes. The device page in the menu shows
where the device is.

### Updating without the cable

Open the device's page and choose the new image under Firmware, or:

```bash
curl --data-binary @firmware/build/perinkpheral.bin http://<address>/api/ota
```

The device restarts into the new firmware and keeps it only if it
comes back reachable; otherwise it returns to the one it ran before.

What you get today: a splash, then - on a device with no network yet -
a screen that says how to set it up. A press leads to the menu (button
1 up, 2 ok, 3 down, 4 back; a long press on button 4 returns to the
menu from anywhere), which lists the installed wasm applications -
`Hello` counts presses and keeps the count, `Clock` redraws once a
minute - and the host's own pages: the device and the splash.

Host tests of the hardware-independent modules need only a C compiler:

```bash
cd firmware/test/host && make test
```

`tools/preview/preview.py [dir]` renders the host's screens to PNG
without the device. `tools/fontgen/fontgen.py` turns a TTF into a 1-bit
bitmap font header and needs the `freetype-py` package.

## Writing applications

An application is C compiled to WebAssembly. Its handlers change its
state and ask for a redraw; `app_on_draw()` paints the state:

```c
#include "app_text.h"
#include "host_api.h"

static int32_t presses;

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_OK_PRESSED:
        presses++;
        request_redraw(HOST_REDRAW_NORMAL);
        break;
    case BTN_BACK_PRESSED:
        request_exit();
        break;
    }
}

void app_on_draw(void)
{
    gfx_text(16, 16, HOST_FONT_TEXT, "Hello from wasm", 15);
}
```

The whole contract is one header, `sdk/host_api.h`; `sdk/apps/hello` is
the complete sample. Build it with
`tools/mkapp/mkapp.py sdk/apps/hello --out <dir>`. An application has
64 KB for everything and no C library; `sdk/lib` has what it needs to
build strings. It is unloaded whenever it leaves the screen, so what
must survive goes into its own small store with `kv_set` and comes
back with `kv_get`.

An application is a folder with `app.wasm` and `manifest.json`. Today
the ones in `sdk/apps` are packed into the device's file system when
the firmware is built; installing from a catalog comes later, and the
contract is not frozen yet: it changes while the first applications are
written.

While you work on one, the device takes it over the network, no
reflashing and no cable:

```
export PERINKPHERAL_HOST=<the device's address>
tools/devtool/devtool.py run sdk/apps/hello --shot hello.png
tools/devtool/devtool.py press 2 2 --shot pressed.png
```

`run` builds the application, uploads it and restarts the device into
it, about ten seconds in all; `--shot` saves what the panel shows as a
PNG, and `press` presses the buttons for you. `devtool.py --help` has
the rest: the log, the status, flashing the firmware itself.

## Roadmap

1. **Applications without the network - done.** The host kernel
   (display service, window manager, scenes), the wasm runtime, the
   host API, applications on the device's file system, two samples
   (`hello`, `clock`). Left: a run from the battery without USB.
2. **Drivers and the SDK - in progress.** Done: Wi-Fi with setup from a
   browser, firmware updates over the network, time, timers, a store
   for applications, fonts, and the half of data delivery that needs no
   network. Not yet run on a real network. Left: the poller that
   fetches, requests from applications, the first real application
   (Plex "now playing"), button down and up events.
3. **Host screens - started.** Done: the menu, power-up, the Wi-Fi
   setup screen. Left: status and error screens, a second font.
4. **Power.** Sleep between polls, measured current and real battery
   life numbers, then extreme power saving.
5. **Web UI and catalog.** Installing applications from a catalog,
   settings, the log, firmware updates from releases.

Version 1 is ASCII only; other scripts are a task for version 2.

## Repository

```
firmware/              the ESP-IDF project
  main/                entry point, host loop, scenes, views, services
  components/          board, epd (panel and display service), gfx, input, ui,
                       appsys (application runtime), wasm3, appconf
  test/host/           host tests of the pure modules
sdk/
  host_api.h           the contract between host and applications
  lib/                 what applications link instead of a C library
  apps/                the sample applications: hello, clock
tools/
  preview/             renders the host's screens to PNG without the device
  devtool/             the device over the network: screenshots, presses, upload and run an app
  devshot/             the same over USB for a device with no network
  mkapp/               builds an application to app.wasm and checks it
  fontgen/             TTF -> 1-bit bitmap font header
assets/fonts/          Departure Mono (SIL OFL 1.1), the UI font, and its license
hardware/              the panel notes, the BOM and assembly page, the case model
CLAUDE.md              the specification: architecture, decisions, milestones, state
AGENTS.md              bench notes and constraints for coding agents
```

## License

MIT, see `LICENSE`. The Departure Mono font in `assets/fonts/` is under the SIL
Open Font License 1.1.
