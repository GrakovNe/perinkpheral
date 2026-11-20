# Stability testing

The owner's test scope of 2026-11-17: operation for days, bounded memory, recovery after
an interrupted write and a loss of power. This file distinguishes existing checks from
manual tests. `tools/perinkpheral soak` collects read-only endurance observations;
it does not exercise buttons, change power modes or inject faults. No fuzz suite,
random-button runner or power-switch runner is implemented in `tools/cli`.

Read [CLAUDE.md](../../CLAUDE.md) for the specification and [AGENTS.md](../../AGENTS.md)
for build and device recipes. The HTTP surface is [openapi.yaml](../api/openapi.yaml).

## Order before the public release

The owner's sequence of 2026-11-20, under the feature freeze of the same day: the
[manual acceptance](acceptance.md) of one candidate, fixes, the endurance run below on
the image that passed, fixes, [load testing](load.md), the release. A fix after a run
began is a new image: the run of the release image starts again from its first hour.
The first candidate is firmware 0.1.63.

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
Makefile. Recovery tests cover the OTA writer with stubbed flash operations, the
one-use update hint, screen selection and confirmation key handling. They do not
exercise ESP-IDF, GPIO, flash hardware or radio timing.

Browser update and manual-entry flows are tested separately with Node.js:

```sh
node firmware/test/web/test_firmware_update.js
```

These tests execute the page's JavaScript with mocked HTTP responses. They cover
compressed release downloads, delayed recovery startup, lost replies, manual entry,
image identity and probation; they do not replace a browser/device check.

The simulator compares application frames with reviewed pictures:

```sh
tools/perinkpheral sim-test firmware/test/sim
```

`--update` replaces the expected pictures and requires visual review. A simulator frame
has no panel latency. Its network can use canned responses from each scenario.

Scenarios run several at a time (`--jobs N`; one a processor by default, `1` for one by
one) and are reported in their own order. Each has its own simulator, state folder and a
port the system picked; the applications are built once, before the first simulator starts.
The simulator's clock is the computer's and a `wait` step is real time: a scenario that
counts on what happens within a wait can fail on a busy computer, alone or among others.
`forward` moves the clock without waiting. `"serial": true` in a scenario runs it while no
other does.

## Device observations

A development build exposes the frame and injected presses, application uploads, the
store, restart, socket listing and deliberate faults under `/api/dev`.
Release builds omit these routes. There is no `/api/dev/health` route.
`/api/dev/flash` is retained only to return 409; firmware uploads in either build
go through recovery and physical confirmation.

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

### Observation collector

```sh
tools/perinkpheral --host <address> soak --out <private-run-directory> \
    --image <release-image-id> --hours 72
```

The expected image must be a release build, running with completed probation. Keep
the exact downloaded binary, its release identity and matching ELF when available;
an ELF from a different build is not an exact symbol file. The 0.1.44 CI artifacts
did not retain the ELF, which limits postmortem symbolication for that candidate.

The collector writes `run.json`, `status.jsonl`, `log.jsonl`, `events.jsonl` and an
atomically replaced `progress.json`. Status is sampled every minute and the log every
two seconds. Responses are not retried invisibly. Boot identifiers detect restarts
even when the new log cursor has already passed the previous one; missing ring-buffer
bytes and observation gaps are recorded. The collected files may contain private
application data and network names; keep the directory private and outside git.

The process must remain running on an awake computer. Resume with the same command
and directory: the original deadline remains, and interrupted observation is not
counted as evidence of continuous operation. Another collector cannot own the same
directory. There is no automatic device restart, repair, update or pass verdict.

Use `phase.txt` in the run directory to label the physical scenario. `powered` is
the default. `sleep` reduces log requests to one every 30 seconds; unavailable-device
requests also back off to 30 seconds. This file changes observation only, not the
device's settings. After the first 24 hours the collector records a reminder that
the physical sleep phase still needs to be arranged. At the end it says
`awaiting_review`, not passed. Settings, applications, real buttons, the panel and
recovery still need the manual checks below.

### Release candidate sequence

Use one frozen release image and application set for 72 hours, without scheduled
restarts: 24 hours with power saving disabled, 24 hours with Always and no USB host,
then 24 hours with Auto and controlled network outages of 1, 10 and 30 minutes on a
test network. Leave the final 12 hours quiet. Record each intervention and compare
memory only in equivalent settled states. USB removal with the battery connected
is a power-profile transition, not a power cut. An ordinary charger can sustain the
sleep test without presenting a USB host; this is not a battery-life measurement.

Afterward check OTA, physical confirmation, exact image identity and probation,
then manual recovery entry and exit, and compare settings and application versions.
A separate 26-hour static-image run with the header clock disabled checks the daily
panel refresh: routine screen updates can postpone that deadline.

### Physical checks

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
- main completes probation, or recovery remains reachable and accepts a replacement
  image; there is no previous full firmware slot to roll back to;
- Perinkpheral OS settings remain valid. `settings_start()` uses defaults for invalid stored values;
  whole-settings transactional rollback is not promised.

A failed write is not promised to leave storage untouched. Judge the recovered state by
the operation's contract, rather than requiring all files or keys to change atomically.
See [SECURITY.md](../../SECURITY.md) for network access and physical confirmation rules.

## Coverage not implemented

Automated workload generation, parser fuzzing, random navigation, network fault
injection and controlled power cuts need runners and, for the hardware cases, a
dedicated bench. The read-only soak collector does not perform these operations.
They are not CI jobs and have no CLI commands. Battery life also needs measured current.
Firmware/SDK tool logic belongs in `tools/core`, commands and generators in
`tools/cli`, and device or simulator code in C. The separate Python CAD exporter
in `tools/case` is not a platform for device-test runners.

## Results

The 2026-11-15 device soak recorded backward `esp_timer` jumps and interrupt-watchdog
faults inside ESP-IDF modem sleep. This is a historical fault observation, not a passing
endurance result. No completed run of the protocol above is recorded here.

On 2026-11-18 the owner stopped the 0.1.44 observation run for audit fixes; it must
not resume without a new instruction. A later status read on the unchanged release
reported reset reason 5 and an interrupt-watchdog panic in the Wi-Fi task
(`pc 40017606`, `ra 40807bd6`). The last observation before stopping had reset reason
3 and no panic. The firmware image was
`2a1b813cedeccf96be593d93158e84170d9565506e0f8971fd9d90753b95d770`.
This remains an unresolved device failure; passing host tests does not close it.

### 15 dBm Wi-Fi experiment

The owner approved a 15 dBm transmit-power limit on 2026-11-18 for the next run.
Reports in [ESP-IDF issue 19036](https://github.com/espressif/esp-idf/issues/19036)
describe backward timer jumps and watchdog faults on ESP32-C6 v0.2 boards. Several
participants reported no further timer faults after reducing Wi-Fi transmit power
from 20 to 15 dBm. Espressif is still investigating. The symptoms resemble ours;
we have not established that they have the same cause.

Main and recovery request 15 dBm through `esp_wifi_set_max_tx_power(60)`; the API
uses quarter-dBm units. Main reapplies it whenever the station starts, including
after a radio power cycle. PHY defaults also cap startup power at 15 dBm. The
startup log reports the limit read back from the driver. OTA replaces main only;
updating the installed recovery image requires USB.

For the next owner-started run, record the firmware image ID and the reported
power limit. Keep the board, antenna, network, power profile and app workload
unchanged. Record resets, timer regressions, lost connectivity and request failures.
Check that the lower power still reaches the access point reliably. A completed
run without faults supports this mitigation but does not prove the cause or a
permanent fix. Other audit fixes are included in this image, so it is not a
controlled comparison of transmit power alone. Do not restart the run automatically.

[heap]: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/system/heap_debug.html

## Recovery qualification

On 2026-11-18 the assembled ESP32-C6 was migrated over USB from 0.1.33 after a
complete 4 MiB backup. The migration target wrote only bootloader, partition table,
OTA metadata, recovery and main. A second full flash read after recovery boot
confirmed byte-for-byte preservation of nvs, phy_init, appkv, appcode, apps and
glyphs. Recovery connected using the saved WPA3-SAE H2E credentials. An image header
from the old partition layout was rejected before confirmation or writing. Physical
BACK started main and main completed probation; Perinkpheral OS settings were unchanged.

The populated device also exposed heap exhaustion in GET /api/apps with thirteen
installed applications. The response now serializes one application at a time and
keeps a compact 1,852-byte metadata snapshot. Allocation-failure checks cover errors
before the response starts and after its first application: the former returns 503,
and the latter closes an incomplete response without a final HTTP chunk.
The fixed debug image, 0.1.34-cb5e2ff, was installed over USB. Its OTA selection
record was set to NEW over USB for the bench restart, retaining first-boot probation.
After confirmation, all thirteen application records and Perinkpheral OS settings matched the
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
Perinkpheral OS UI and saved Russian language, and accepted physical OK. The CLI installed
0.1.34-fdfb88e and verified the exact uploaded image identity and completed probation.
All thirteen application records and Perinkpheral OS settings matched the pre-update values;
failed allocations remained zero. Native previews also cover both languages and a
31-character version without truncation.

The seven installed catalog applications from the reviewed batch (air, github, iss,
life, metals, pomodoro and rates) were updated to 0.1.1. Frames captured from the
device for air, iss and life were inspected, then the HA dashboard was restored.
Perinkpheral OS settings and every application's exposed settings values remained unchanged.
Twenty repeated application-list requests completed without new allocation failures.

During the first installation/navigation run, four allocation failures exposed a
second admission gap: direct catalog downloads bypassed the poller's TLS memory
check. The guarded path was tested on the device in 0.1.34-39c9b26, again installed
over USB with a NEW OTA record and completed probation. Reinstalling all seven
applications while switching through air, iss, life and ha completed with zero
failed allocations, a 28,244-byte minimum free heap, and three temporary memory
refusals which returned 503 and succeeded on subsequent attempts. This is a short
functional check, not an endurance or power-loss qualification.

The startup-screen fix was checked on 2026-11-18 in `0.1.37-ui-a8d6d2f` with USB
connected. A complete flash backup preceded writing only main, recovery and OTA
metadata. A deliberately prepared PENDING_VERIFY boot record became ABORTED in the
bootloader (verified by reading it back), which selected recovery. Its first view
was "Recovery / Connecting to Wi-Fi", followed by the page address after the saved
WPA3 network connected; it never claimed to be preparing an update.

After main was started with a NEW record and completed probation, the normal CLI
OTA exercised the one-use hint. The recovery console reported `entry: update` and
only waiting/preparing (state 0, waiting 0), confirmation (state 2), writing (state 4)
and restarting (state 8). It drew no network-instruction view between preparation
and confirmation. The owner pressed physical OK. The expected image
`7733b19bcc532a56a435b072baea4f806bd12bd481b4cceb7b72c088b5e47579` then completed
probation; Perinkpheral OS settings and the installed-app list compared equal before and after,
with zero failed allocations. The boot-record test exercises rollback selection,
not an actual power cut or a failed upload. Timeout and absent-network view choices
remain covered by host tests and English/Russian previews.

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
- Verify recovery entry screens separately: a requested update keeps "Preparing update"
  through connection and header receipt, then asks for confirmation; manual entry, failed
  probation and interrupted writes show recovery rather than preparation. A requested
  update which never uploads exposes network help after 30 seconds. Without saved Wi-Fi,
  access-point instructions appear immediately. Host tests and reviewed English/Russian
  previews cover these choices; the hardware record above covers planned OTA and the
  bootloader's fallback from an unconfirmed image.
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
