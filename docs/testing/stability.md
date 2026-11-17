# Stability testing: the plan

What this is: the plan of how perINKpheral is tested for staying up - for days on a shelf, under
whatever applications and users do to it, through a loss of power. It is written for whoever
builds it (an engineer or an agent working alone), with enough of the project in it to start
without asking. The owner agreed to it on 2026-11-17. Testing of the network - Wi-Fi going away,
servers that misbehave, TLS failures - is a separate plan, not this one (see the last section).

Read first: `CLAUDE.md` (the specification: architecture, memory budget, conventions, what must
not be done), `AGENTS.md` (the bench: building, flashing, the development tools).

## Why, and what counts as a failure

The device runs for weeks without a hand on it. A short test proves nothing: what breaks a device
like this is the heap that fragments over a day until TLS finds no 17 KB piece, the counter that
wraps after 49 days, the write cut by a loss of power, the stack that overflows once in ten
thousand calls. Espressif names the measures - the least free heap since start, and the largest
free block against the total free, whose gap is fragmentation ([ESP-IDF heap debugging][heap]).
Most "leaks" on an ESP32 are fragmentation ([SunFounder][sunfounder]); a test gates on numbers,
not on "it seems to work" ([Wokwi heap monitoring][wokwi]).

A failure is any of:
- a restart nobody asked for: the run of the device changes (`X-Log-Boot` of `/api/log`, or
  `uptime_s` of `/api/status` going down) without the test having asked for a restart;
- a fault record: `last_panic` in `/api/status`, `reset_reason` 4 (a fault) or 5 (the interrupt
  watchdog);
- memory that does not come back: free heap after a load lower than before it by more than a
  margin, a falling trend over hours, the largest free block under what TLS needs;
- an allocation that failed (`failed_allocations` in `/api/status`), or requests refused for
  memory under the normal load of the test (`memory_refusals`);
- the device not answering: the page, a press that brings no new frame within its time;
- after a loss of power: a storage the device calls broken, an application half installed, a
  value of the store that is neither the old nor the new one, a firmware that does not start.

## What exists already

- **The development tools** (a build with `APP_DEV_CONSOLE`, the default; not in a release build):
  `/api/dev/screen` (the frame on the panel as a BMP, `X-Frame` the count of frames shown,
  `X-Pending` whether one waits), `/api/dev/button`, `/api/dev/open`, `/api/dev/app` (upload a
  file of an application), `/api/dev/run`, `/api/dev/kv`, `/api/dev/restart`, `/api/dev/flash`,
  `/api/dev/sockets`, `/api/dev/crash`. Described in `docs/api/openapi.yaml`, made in
  `firmware/main/devtools.c`.
- **`/api/status`**: `free_memory`, `least_free_memory`, `largest_free_block`, `memory_refusals`,
  `failed_allocations`, `uptime_s`, `reset_reason`, `last_panic`, `storage_ok`, `storage_used`,
  `app_area_used`, the firmware and its slot. `/api/log?since=` with `X-Log-End` and `X-Log-Boot`.
- **The device from the command line**: `tools/core/.../device/DeviceClient.kt` (HTTP to the
  device), `LogFollower.kt` (the log across restarts), `UsbConsole.kt`; `tools/perinkpheral log
  --follow`, `run`, `usb`.
- **The memory guard** (`firmware/main/memory.c`): refusals before the heap runs short, and an
  orderly restart when it is critically short - which a test must count as a failure, not hide.
- **The crash record** (`firmware/main/crash_note.c`): the reason, the task and code addresses,
  kept through the restart; `riscv32-esp-elf-addr2line -pfiaC -e build/perinkpheral.elf <addresses>`.
- **Host tests and the simulator**: `firmware/test/host` (tinytest, pure modules, run on Linux in
  CI with ASan and LeakSanitizer), `tools/simulator` (the host's code on the computer, running one
  application), `firmware/test/sim` (scenarios with canned answers to the network).
- A soak of 2026-11-15 found that `esp_timer` went back in time inside the radio's modem sleep
  (see Open in `CLAUDE.md`): this is the kind of thing it is for.

## Rules for whoever builds it

- Everything in English, plain hyphens, SPDX headers; interface and report text short and plain.
- **No Python of the project's own**: every tool is Kotlin, in `tools/core` (what is reusable) and
  `tools/cli` (the command), as the rest of `tools/perinkpheral` is. C for what runs on the device
  or in the simulator.
- Logic that needs no hardware is a pure module with host tests; a new component or pure module
  gets its place in the tables of `tools/perinkpheral check-layers`.
- **The owner's device is not a test bench.** Never call `/api/reset`, `/api/wifi`, `/api/ota*`,
  never answer "Factory reset" or "Forget network" on the screen, never `sys_do`. Flash a device
  over USB only with the owner's agreement. A bench device of its own is planned (phase 4); until
  then, ask before a run that installs and removes applications on the owner's device.
- Every push to `main` is a release (`CLAUDE.md`, Continuous integration). Work on a branch,
  merge a phase when it is done and green.
- Write down what a run found, briefly, in the Results section at the end of this file.

## Phase 1 - a soak on the device

The first and the most useful: data from the real device in a day.

### 1.1 The health of the device (firmware, development builds only)

`GET /api/dev/health`, in `firmware/main/devtools.c`, JSON:
- `stacks`: for every task of `components/board/include/priorities.h` and the system's own
  (`main`, `buttons`, `poller`, `wifi`, `web_waits`, the web server's, `tiT`, `sys_evt`,
  `esp_timer`, `IDLE`) its name and its least free stack in bytes
  (`uxTaskGetStackHighWaterMark` on each handle). `CONFIG_FREERTOS_USE_TRACE_FACILITY` is off;
  either keep the handles where the tasks are made, or turn the facility on in the development
  build only and use `uxTaskGetSystemState` - measure what each costs in RAM and pick the cheaper.
- `heap_ok`: `heap_caps_check_integrity_all(true)`. It walks the heap: call it only when asked,
  and say in the reply how long it took.
- `heap`: the internal heap's free, least free, largest block, and the number of free blocks
  (`heap_caps_get_info`) - the number of pieces is the fragmentation the largest block hides.
- `app_area`: what the application area has used at its most.
- `deadlines`: how many deadlines the table holds and its capacity (`components/deadlines`).
- `sockets_open`: the count `/api/dev/sockets` lists.
Add it to `docs/api/openapi.yaml`. Its handler runs through the host loop like the other
development tools do when it touches its state.

### 1.2 An application that exercises the host

`firmware/test/soak-app/` (built with the SDK like `firmware/test/host/apps/probe`; installed with
`/api/dev/app`): an application whose only purpose is to make the host do everything it does for
applications, at the most the contract allows, and to say in the log when anything answered
otherwise than the contract says. Per minute:
- a repeating alarm (`alarm_set_every`) and two one-shot alarms (`alarm_set`, `alarm_set_at`);
- timers on the screen (`timer_set`), a row of `HOST_REDRAW_NO_BLINK` frames;
- store writes within the 12 a minute the host tolerates, near its 2 KB (`kv_set`), and reads back
  with a check of what was written;
- a file written, read back and checked (`file_write`, `"writes_files": true`), up to its 32 KB;
- `session_set` in every `app_on_blur`, `session_get` checked in every `app_on_focus`;
- `header_status` kept and given back;
- `notify` now and then;
- `request_focus` from an alarm (the host refuses it within 30 s of a press: count both);
- the drawing calls over the whole kit (`ui_list_row`, `ui_cell`, `ui_chart`, `ui_notice`,
  `gfx_qr`, text in every font).
No network (that is the other plan). It keeps counters - calls made, answers that were wrong -
and logs a line of them every ten minutes, `soak: ...`, which the tool reads.

### 1.3 The tool: `tools/perinkpheral soak`

```
tools/perinkpheral soak <device> --hours 24 [--out build/soak] [--seed N] [--profile default|calm|busy]
```
Kotlin: the work in `tools/core/.../soak/`, the command in `tools/cli/.../Main.kt`. Parts:
- **Sampler** - every 10 s `/api/status`, every 60 s `/api/dev/health`; a row of `samples.csv`
  each (the time, the run of the device, every number above). A device that does not answer is a
  row that says so.
- **Log recorder** - follows `/api/log` (as `LogFollower` does), writes `log.txt`, a line where a
  new run of the device begins, and keeps the lines at `W` and `E` and the `soak:` lines apart.
- **Actor** - a loop of steps, each chosen by weight with the seed (the same seed, the same run):
  a short or long press of a button (`/api/dev/button`); open an installed application
  (`/api/dev/open`); install one from the official catalog and remove one (`/api/apps/install`,
  `/api/apps/remove`, as the page does - the files the catalog's index lists); save an
  application's settings (`/api/apps/settings`); change a host setting that is safe to change and
  put it back (`/api/settings`: the font, the refresh numbers, the header clock, the language - not
  the network, not the name); open the device's page and its sections as a browser would; now and
  then a restart the tool asks for (`/api/dev/restart`), which is not a failure; with
  `--profile busy`, a burst of a dozen page requests at once. Between steps, a pause drawn from a
  range - a person, not a fuzzer.
- **Latency** - for every press: the time from the request to the next `X-Frame`, and whether
  `X-Pending` stayed up; for every page request, its time.
- **Detectors** - the failures listed at the top of this file, each with a threshold the command
  takes and a default: no unplanned restart; no `last_panic`; `failed_allocations` 0;
  `memory_refusals` 0 under `default`; free memory, sampled in the quiet minute after every hour,
  within 2 KB of the first hour's; the least largest block at least 20 KB (TLS needs 17 KB in one
  piece); a frame within 1 s of a press at the 99th percentile; the least free stack of every task
  at least 512 bytes; `heap_ok` always true; the soak app's wrong answers 0.
- **Report** - `report.md` and `summary.json` in the output folder: the verdict, every failure
  with the time and the lines of the log around it, the charts of free memory, the largest block,
  the least free and the number of free blocks over the run (drawn as text or SVG by the tool,
  nothing outside the JVM), latency percentiles, the soak app's counters. The exit code is 0 for
  a pass and 1 for a failure, so that it can gate.
The output folder is not in git. Unit tests for the detectors and the report (tools/core's tests),
fed with a recorded `samples.csv`.

### 1.4 The first runs

1. A run of 1 hour, to see the tool work.
2. 24 hours, `default`, on the bench device (or the owner's, with his yes).
3. 72 hours, `default`, after what 24 hours found is fixed.
Put each verdict, and each defect found with its fix, in Results below.

### 1.5 Time

Also in phase 1, as a profile `--profile clock`: the clock moved by hand forward and back, by
minutes and by days (`/api/clock`, with the network's time off through `/api/settings`, put back
after), while the soak app's alarms are set: an alarm `at` a moment follows the clock, one `in`
so long does not (`components/deadlines`); nothing is lost, nothing comes twice, the header's
minute keeps turning. And the counters that wrap: `time_uptime_ms` and the deadlines' clock past
2^32 ms (49.7 days) - test it in the host tests by starting the table's clock near the wrap,
not on the device.

## Phase 2 - fuzzing what reads bytes from outside

Every parser that takes bytes from a server, a manifest, the page or a module gets a libFuzzer
target, built with `-fsanitize=fuzzer,address,undefined`.

- `firmware/test/fuzz/` - one `fuzz_<name>.c` a target, a `Makefile` like the host tests', a
  `corpus/<name>/` of a few small seeds each (in git), and nothing else.
- The targets:
  - `components/appstore/json_tree.c` - the page's requests and the manifests;
  - `sdk/lib/json_pick.c` - answers in pieces, fed in random cuts, with list paths;
  - the manifest's parse and checks (`components/appstore/app_manifest.c`), with and without its
    translation;
  - `{{key}}` substitution (`text_template.c`) with hostile setting values;
  - the firmware image's receiving and verdict (`components/net/ota_stream.c`), in random pieces;
  - the host settings from JSON (`components/appconf`), as `/api/settings` takes them;
  - the catalog's sources (`components/appstore/catalog.c`);
  - `sdk/lib/app_fixed.c` (`app_fixed_parse`), and `app_values.c`;
  - the loading of a wasm module with its imports checked (`components/appsys` `app_load`, as
    the simulator builds it), seeded with the modules of `sdk/apps` and the catalog, with the
    interpreter's memory limit as on the device;
  - in perinkpheral-apps, `apps/calendar/ics.c` with its recurrence rules (its own CI).
- CI: a job in `.github/workflows/ci.yml` on Linux that builds the targets and runs each for 60
  seconds on every push; a scheduled workflow that runs each for an hour every night and keeps
  what it found as an artifact. A crash found is a test: its input goes into the host tests of
  that module, the fix with it.
- macOS: Apple's clang has no libFuzzer; on a desk use Homebrew's LLVM, or let CI do it.

## Phase 3 - a monkey in the simulator

`tools/perinkpheral monkey <app dir> --minutes N --seed S`: the simulator, without a window, and
random presses - short and long, weighted toward UP and DOWN - for every application of
`sdk/apps`, `firmware/system` and perinkpheral-apps, now and then a settings change through
`/api/dev/kv` and a clock jump.
- The network is the scenario's canned answers (`firmware/test/sim/*/scenario.json`,
  `perinkpheral-apps/tests`), never the real one: misbehaving answers belong to the network plan.
- Built with ASan and LeakSanitizer on Linux. A failure: the simulator crashed, a handler trapped
  or was stopped, a leak at exit, a "slow handler" line, a draw outside `app_on_draw`, a redraw
  that waited too long.
- CI: the scheduled nightly workflow, ten minutes an application, the seed in the log so that a
  failure can be run again.

## Phase 4 - a loss of power

Needs hardware; ask the owner for it:
- a second XIAO ESP32-C6 with the panel and the buttons, **without a battery** - the battery keeps
  the board running when USB goes, so a cut of USB is a cut of power only without it;
- a USB hub with per-port power switching that `uhubctl` drives ([uhubctl][uhubctl]; the same is
  done by Espressif's own test utilities, [esp-test-utils][esptest]) - or a relay on the 5 V line.

`tools/perinkpheral power-cut --hub <location> --port <n> <device> --cycles 300 --during <op>`
(Kotlin; `uhubctl` is the one outside tool it calls): start an operation, wait a random time
within the time the operation takes, cut the power for 2 s, give it back, wait for the device
(over the network as a reachable address, or `UsbConsole`), and check. Operations:
- **install**: an application from the catalog. After: the list of applications is the old one or
  the new one, never half; no "Storage error" (`storage_ok`); the module is in its slot of the
  code partition or comes back as a file (`main/app_code.c`); the application starts.
- **store**: the soak app writing the store and its file in a loop. After: every value is the
  old or the new one, the store opens, the 2 KB accounting matches what is there.
- **firmware**: an image through `/api/dev/flash`. After: the device runs the old firmware or the
  new one; a new one on probation confirms itself or rolls back (`main/ota.c`, `ota_health.c`).
- **glyphs**: a glyph pack written (`/api/glyphs`). After: the pack is whole or absent
  (`main/glyphs.c` writes the first bytes last).
- **settings**: host settings saved in a loop. After: NVS opens, the settings are the old or the
  new, nothing reset to defaults.
Each operation 300 cycles at least; a report as phase 1's. A device that does not come back is
found by `uhubctl` cycling it again, and is a failure.

## Running it all the time

Once phases 1 and 4 have their bench device: it lives on the bench, takes every build of `main`
(a development build, through `/api/dev/flash`), and runs `soak --hours 24` then `power-cut` in a
loop; the reports are kept and a failure is told (a GitHub issue, or a line to the owner). A
regression is then seen on the bench before it reaches a shelf.

## Not in this plan

- **The network**, a plan of its own: the access point going away and coming back (the rescue
  after 2 minutes, the retries), a server that is slow, cuts its answer, lies in Content-Length,
  sends gigabytes, loops redirects or sends garbage for JSON, TLS that fails, DNS that does not
  answer - on the device against a misbehaving local server, and in the simulator's network.
- Power saving and battery life: they need the current measured; not before there is a way to.

## Results

Nothing run yet.

[heap]: https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32/api-reference/system/heap_debug.html
[sunfounder]: https://www.sunfounder.com/blogs/news/esp32-flash-psram-memory-leaks-understanding-the-memory-map-and-preventing-crashes
[wokwi]: https://docs.wokwi.com/wokwi-ci/heap-monitoring
[uhubctl]: https://manpages.ubuntu.com/manpages/lunar/man8/uhubctl.8.html
[esptest]: https://esp-test-utils.readthedocs.io/en/latest/_modules/esptest/devices/usb_hub.html
