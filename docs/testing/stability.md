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

On 2026-11-18 the assembled ESP32-C6 was migrated over USB from 0.1.33 after a
complete 4 MiB backup. The migration target wrote only bootloader, partition table,
OTA metadata, recovery and main. A second full flash read after recovery boot
confirmed byte-for-byte preservation of nvs, phy_init, appkv, appcode, apps and
glyphs. Recovery connected using the saved WPA3-SAE H2E credentials. An image header
from the old partition layout was rejected before confirmation or writing. Physical
BACK started main and main completed probation; host settings were unchanged.

The populated device also exposed heap exhaustion in GET /api/apps with thirteen
installed applications. The response now serializes one application at a time and
keeps a compact 1,852-byte metadata snapshot. Allocation-failure checks cover errors
before the response starts and after its first application: the former returns 503,
and the latter closes an incomplete response without a final HTTP chunk.
The fixed debug image, 0.1.34-cb5e2ff, was installed over USB. Its OTA selection
record was set to NEW over USB for the bench restart, retaining first-boot probation.
After confirmation, all thirteen application records and host settings matched the
pre-migration JSON, and the failed-allocation counter remained zero.

The network handoff reached recovery, and a direct HTTP upload reached physical
confirmation and expired with "Not confirmed" without erasing main. Two CLI attempts
stopped before confirmation. A regression test reproduced reuse of the handoff's
keep-alive socket; each OTA POST now owns a fresh client, with deadline and cancellation
tests against a stalled server. The corrected CLI then completed a physical OTA cycle
on battery power with USB disconnected: main entered recovery, the physical OK key
approved installation, and the intended image completed probation. The returned
0.1.34-39c9b26 image identity matched the uploaded header, `usb_host` was false and
the failed-allocation counter was zero. This also checks reinstalling the same image.

The redesigned recovery was subsequently installed with `recovery-flash`: its write
list contained only the factory image at 0x20000. Recovery connected with the saved
network through its read-only NVS initialization, displayed the confirmation using
the host UI and saved Russian language, and accepted physical OK. The CLI installed
0.1.34-fdfb88e and verified the exact uploaded image identity and completed probation.
All thirteen application records and host settings matched the pre-update values;
failed allocations remained zero. Native previews also cover both languages and a
31-character version without truncation.

The seven installed catalog applications from the reviewed batch (air, github, iss,
life, metals, pomodoro and rates) were updated to 0.1.1. Frames captured from the
device for air, iss and life were inspected, then the HA dashboard was restored.
Host settings and every application's exposed settings values remained unchanged.
Twenty repeated application-list requests completed without new allocation failures.

During the first installation/navigation run, four allocation failures exposed a
second admission gap: direct catalog downloads bypassed the poller's TLS memory
check. The guarded path was tested on the device in 0.1.34-39c9b26, again installed
over USB with a NEW OTA record and completed probation. Reinstalling all seven
applications while switching through air, iss, life and ha completed with zero
failed allocations, a 28,244-byte minimum free heap, and three temporary memory
refusals which returned 503 and succeeded on subsequent attempts. This is a short
functional check, not an endurance or power-loss qualification.

Recovery changes the update mechanism. Host tests and successful battery OTA do not
qualify physical power-loss behavior. The owner cannot disconnect the shelf device's
battery; removing its USB cable does not cut power. Controlled power cuts remain
unverified and require another fixture. Full bench qualification comprises the cases
below, including those already recorded above, with settings and applications backed up.

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
