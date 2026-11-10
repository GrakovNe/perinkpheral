# AGENTS.md

Working notes for coding agents: recipes and the constraints that are
not visible in the code. The specification is `CLAUDE.md`; read it
first, and where the two disagree it wins. Everything learned about the
panel is in `hardware/README.md`.

## Recipes

The toolchain is ESP-IDF v5.5 (verified on v5.5.5) with its `export.sh`
sourced, which also puts a Python with pyserial on the path, and
wasi-sdk (verified with 34) with `WASI_SDK_PATH` pointing at it. The
firmware build needs both: the image carries wasm applications.

The board is on its native USB-Serial/JTAG (VID 0x303A PID 0x1001):
`/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS. The name changes
with the USB socket, so look it up; the recipes call it `$PORT`. When
the port is gone the board is unplugged: there is nothing to fix in
software.

Build (the IDF project is `firmware/`):

```bash
. "$IDF_PATH/export.sh"
cd firmware
idf.py build
```

`IDF_SKIP_CHECK_SUBMODULES=1` in front of `idf.py` skips a git check
that needs the network. After editing `main/Kconfig.projbuild` or
adding a component: `idf.py reconfigure`.

Flash: `idf.py -p $PORT flash` (bootloader, partition table, otadata,
app). `app-flash` keeps settings in `nvs`; `erase-flash` gives the
factory state. `esptool` v5 spells its commands with dashes
(`write-flash`, `read-flash`).

Console capture. Opening the port does not reset the board, so a
capture can watch a running device; `idf.py flash` resets it, and the
port disappears for a moment while it does:

```bash
python - "$PORT" <<'EOF'
import serial, sys, time
P = sys.argv[1]
s = None; end = time.time() + 30; buf = b''
while time.time() < end:
    try:
        if s is None: s = serial.Serial(P, 115200, timeout=0.2)
        buf += s.read(4096)
    except Exception:
        s = None; time.sleep(0.3)
print(buf.decode('utf-8', 'replace'))
EOF
```

Host tests: `cd firmware/test/host && make test`. With `WASI_SDK_PATH`
set they include `test_app`, which builds a real module
(`test/host/apps/probe`) and runs it in the real interpreter; with
`IDF_PATH` set, the manifest and store tests, which need its cJSON.

An application: `tools/mkapp/mkapp.py sdk/apps/<name> --out <dir>`
builds `app.wasm` without a C library, with one page of memory,
refuses a module that imports anything `sdk/host_api.h` does not
declare, and checks `manifest.json` by the host's rules. Everything in
`sdk/apps` is built this way by the firmware build and packed into the
image of the `apps` partition; `idf.py flash` writes it with the
firmware.

Views before flashing: `tools/preview/preview.py [dir]` compiles `gfx`,
the window manager and `firmware/main/screens.c` natively and writes a
PNG per screen. Look at them; the panel shows exactly that.

The device without hands on it: `tools/devshot/devshot.py $PORT --keys
3,2 --out shot.png` types keys into the development console (`1`-`4` a
short press of that button, `q` `w` `e` `r` a long press of button 1-4),
then saves the frame the panel shows. Verify a change to the host on
the real device this way, not only in the preview.

Fonts: `tools/fontgen/fontgen.py <ttf> --px N --name <id> --out
firmware/components/gfx/fonts/<id>.h --preview <file>.png` rasterizes a
TTF with FreeType in monochrome (printable ASCII) into a `gfx_font_t`
for `gfx_text_font()`. It needs the `freetype-py` package. The UI font
is `assets/fonts/DepartureMono-Regular.otf`, a pixel font: rasterize it
only at multiples of 11 px, where it lands on its own grid. The headers
are 22 px (the host's text), 44 px and 66 px `--only "0123456789:+-.,%/ "`
(large text and digits for applications). The generated headers are
committed.

Firmware without the cable, once the device is on a network:
`curl --data-binary @firmware/build/perinkpheral.bin
http://<address>/api/ota`. The device restarts into the new image and
keeps it only if it comes back reachable; `GET /api/status` says
whether it is still on probation. The address is on the device page of
the menu.

`firmware/CMakeLists.txt` takes the version from `APP_VERSION` when it
is set.

## Constraints

Code:

- SPDX header in every source file, C11, 4 spaces, `snake_case`.
  Commit bodies say why.
- Code is written to be read by a person. Names carry the meaning:
  `buffer_x`, `row_height`, not `a`, `b`, `t`. Numbers that mean
  something are named constants. Comments only for what cannot be read
  off the code: why, not what.
- Logic that does not need hardware is pure and host-tested: `gfx`,
  `btn_core`, `refresh_policy`, `appconf`. `display.c` and `buttons.c`
  only move it onto SPI, GPIO and tasks.
- `settings_load()` never fails and `reset_early_init()` runs before
  anything opens NVS. Only `settings.c` touches NVS for the host;
  applications get their own namespaces through the host API `kv_*`.
- `sdk/host_api.h` is the only contract between host and applications.
  An import is added in three places and nowhere else: the header, the
  `HOST_API` table in `appsys/app.c`, and a case in
  `test/host/apps/probe` with its check in `test_app.c`. After ABI 1
  freezes, changing a signature means a new `HOST_API_VERSION`.
- The application is not trusted: `app.c` checks every offset and
  length against the module's memory before using it. Keep it free of
  ESP-IDF; what it needs from the device comes in as `app_services_t`.
- `components/wasm3/wasm3` is upstream v0.9.0 with one local change,
  described in `components/wasm3/README.md`: loops yield, so that an
  application stuck in one can be stopped. Configure the interpreter
  from the component's `CMakeLists.txt`; do not edit it further.
- `managed_components/` is downloaded; do not edit it.
- `gfx_text_sq()` draws the square font: `scale` is the stroke in
  pixels and the gap is a fifth wider, so scale 5 gives the 27 px
  square of the logo.

Display:

- `components/epd/epd.c` follows Waveshare's `epd4in2_V2` byte for
  byte. Three things in it were found the hard way and must stay (the
  full story is in `hardware/README.md`):
  - CS is toggled per byte by GPIO (`spics_io_num = -1`). CS held low
    over a block of data starts a refresh that never ends: BUSY stays
    high for 30 s and more and the image fades to grey.
  - One SPI device on the bus. A second `spi_bus_add_device` on the
    same CS GPIO takes the GPIO matrix and the first device's CS stops
    moving; the panel then ignores everything.
  - Every BUSY wait is bounded (init 1 s, full 4 s, partial 3 s). On a
    timeout the controller is cut with a hardware reset. After deep
    sleep (`0x10`) BUSY stays high for good; the next show always
    starts with a hardware reset, which is also how the panel wakes.
- SPI is 2 MHz; nothing faster has been tried with the per-byte CS.
- Only `display.c` touches the panel. Whoever draws owns a buffer of its
  own and hands the whole frame to `display_show(frame, reason)`; the
  service keeps a copy of what the panel shows, leaves the panel alone
  when the frame is the same, and may answer "deferred" - then offer
  the frame again later.
- The first image after boot is full: it fills the RAM 0x26 that
  partial updates compare against. `epd.c` sleeps the panel at the end
  of every show, mode 1 so that RAM is kept. The 24 h keep-alive is the
  `epd-keepalive` task.

Board and system:

- GPIO16/GPIO17 are UART0 TX/RX and here they are BUSY and CS, so the
  console is on USB-JTAG (`sdkconfig.defaults`). The efuse that
  silences the ROM boot log is irreversible: do not touch it.
- IOMUX SPI2 on the C6 is CS0 = GPIO16 and MISO = GPIO2, which are our
  BUSY and button 3. We are on the GPIO matrix, so it does not matter.
- `CONFIG_FREERTOS_HZ=1000`. At the default 100 Hz `pdMS_TO_TICKS(5)`
  is zero and the button task starves everything else; the boot log
  then stops right after the button pins are reported.
- `board_init()` drives the antenna switch before anything else.
- Neither the partition table nor the bootloader can be changed over
  the air. Only `main/ota.c` calls `esp_ota_*`. A new image is confirmed
  by the probation task, never at boot: confirming early would keep an
  image that cannot be reached to replace it.
