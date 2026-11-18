# Stability testing

The owner's test scope of 2026-11-17: operation for days, bounded memory, recovery after
an interrupted write and a loss of power. This file distinguishes existing checks from
manual tests. No soak runner, fuzz suite, random-button runner or power-switch runner is
implemented in `tools/cli`.

Read [CLAUDE.md](../../CLAUDE.md) for the specification and [AGENTS.md](../../AGENTS.md)
for build and device recipes. The HTTP surface is [openapi.yaml](../api/openapi.yaml).

## Existing checks

Run from the repository root with Java 21 or later, a C compiler and wasi-sdk:

```sh
cd firmware/test/host
WASI_SDK_PATH="$HOME/opt/wasi-sdk" make -s test
```

`WASI_SDK_PATH` names the local SDK installation; it need not be at that example path.
The host tests cover the pure modules, the real wasm interpreter, answer delivery,
frozen ABI 3 packages and the contract snapshot. They also check layers, languages and
the generated city table. CI runs them on Linux with the sanitizers selected by the
Makefile. They do not exercise ESP-IDF, GPIO, flash hardware or radio timing.

The simulator compares application frames with reviewed pictures:

```sh
tools/perinkpheral sim-test firmware/test/sim
```

`--update` replaces the expected pictures and requires visual review. A simulator frame
has no panel latency. Its network can use canned responses from each scenario.

## Device observations

A development build exposes the frame and injected presses, application uploads, the
store, restart, firmware upload, socket listing and deliberate faults under `/api/dev`.
Release builds omit these routes. There is no `/api/dev/health` route.

Use the current tools, with `PERINKPHERAL_HOST` set to the bench device:

```sh
tools/perinkpheral status
tools/perinkpheral log --follow
tools/perinkpheral open hello --shot hello.png
tools/perinkpheral press 1 2 3 4 --shot after.png
```

`/api/status` reports `free_memory`, `least_free_memory`, `largest_free_block`,
`memory_refusals`, `failed_allocations`, `uptime_s`, `reset_reason`, `last_panic`,
`storage_ok`, storage use and application-area use. `/api/log?since=` provides a byte
position in `X-Log-End` and a run identifier in `X-Log-Boot`. The log keeps only its last
4 KB; a reader must keep up. Heap totals and largest-block size measure different
limits; see [ESP-IDF heap debugging][heap].

Fault addresses need the ELF of the exact build. From `firmware/`, with ESP-IDF exported:

```sh
riscv32-esp-elf-addr2line -pfiaC -e build/perinkpheral.elf <addresses>
```

The rescue combination, real button edges, sleep current and physical panel timing
require hardware tests. Injected presses do not test the button pins.

## Manual endurance test

Use a separate bench device. The owner's shelf device is not a test bench; obtain the
owner's agreement before installing, removing or flashing on it. Record the firmware,
applications and versions, network, power profile, duration and operations performed.

Exercise screen changes, settings saves, polls, file and store use, alarms, notifications
and background delivery. Return to the same quiet state periodically and compare free
memory and largest free block. Check that navigation and unfinished edits survive a
background delivery through the session, and disappear after leaving the application.

A failure is:

- an unrequested restart, a new fault record or an interrupt watchdog;
- an allocation failure or memory refusal under the intended normal load;
- a persistent fall in quiet-state free memory or the largest free block;
- a page that stops answering, a press that produces no expected frame, or a stuck panel;
- loss, duplication or misordering of completed application data.

Measure press latency separately for partial and full refreshes. A full refresh normally
takes more than two seconds; a one-second limit cannot apply to every press. An unchanged
frame does not increment `X-Frame`. A deferred frame is reported by `X-Pending`.

Time changes can be tested with `/api/settings` (`time_auto: false`) and `/api/clock`.
Check relative and absolute alarms in both directions and restore network time afterwards.
Counter-wrap cases belong in host tests; they do not require waiting 49 days.

## Manual power-loss test

Use a spare board without a battery: unplugging USB does not remove power with a battery
attached. A switched USB port or relay can cut power at different points during an
installation, store or file write, settings save, glyph upload and firmware update.

After power returns, check:

- installation is complete or recoverable, with no mixed module and manifest;
- the store opens and its accounting agrees with its contents;
- files are usable, or the application handles an incomplete cache;
- glyph packs are whole or absent;
- the device runs a reachable firmware, confirming probation or rolling back;
- host settings remain valid. `settings_start()` uses defaults for invalid stored values;
  whole-settings transactional rollback is not promised.

A failed write is not promised to leave storage untouched. Judge the recovered state by
the operation's contract, rather than requiring all files or keys to change atomically.
See [SECURITY.md](../../SECURITY.md) for network access and physical confirmation rules.

## Coverage not implemented

Long-running automated soak, parser fuzzing, random navigation, network fault injection
and controlled power cuts need runners and, for the hardware cases, a dedicated bench.
They are not CI jobs and have no CLI commands. Battery life also needs measured current.
The owner requires reusable tool logic in `tools/core`, commands and generators in
`tools/cli`, and device or simulator code in C; no project Python.

## Results

The 2026-11-15 device soak recorded backward `esp_timer` jumps and interrupt-watchdog
faults inside ESP-IDF modem sleep. This is a historical fault observation, not a passing
endurance result. No completed run of the protocol above is recorded here.

[heap]: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/system/heap_debug.html

## Recovery qualification

The recovery branch changes the update mechanism. Host tests and builds do not qualify
physical power-loss behavior. Before release, run these on a bench device with backed-up
settings and applications; the shelf device is not a power-cut test fixture.

- Migrate over USB with the migration manifest: no full erase, no apps image. Compare the
  bytes of nvs, appkv, appcode, apps and glyphs before and after migration.
- First installation starts recovery. BACK tries the supplied main with probation. Holding
  BACK at reset for three seconds enters recovery and preserves all user data.
- Upload from a browser and CLI over saved WPA2 and WPA3 networks, and through the recovery
  access point with no saved network and with damaged NVS. Verify the displayed version,
  OK confirmation, timeout and BACK cancellation. A held key must not approve a new request.
- Reject foreign projects, old partition layouts, another chip, oversized images, truncated
  headers and damaged checksums. Invalid headers must leave main untouched.
- Cut power before and during boot-metadata erasure, during each portion of main erasure and
  writing, before and after image verification, and before and after selecting the new main.
  An interrupted main write must return to recovery; recovery must accept another image.
- Reset or crash main before probation confirmation, then test a network failure through the
  probation deadline. Recovery must remain available and never automatically retry main.
- Drop the HTTP response after a successful upload, reinstall the same version with different
  image bytes, delay reboot, and change DHCP address. Clients must not announce success until
  the intended image reports completed probation; unreachable devices get actionable errors.
- In both debug and release builds inspect flash_args, app-flash_args and both web manifests.
  Main belongs at 0x100000, recovery at 0x20000. app-flash must not include recovery or apps;
  migration must not include user data. Deliberately oversized recovery must fail its own
  size check even when the image would fit main.
