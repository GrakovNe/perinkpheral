<img src="assets/mark/mark-on-paper.svg" width="96" alt="The perINKpheral mark: two corners of an empty frame">

# perINKpheral

Firmware, SDK and tools for perINKpheral, an ESP32-C6 desk E-Ink display
with four buttons. Applications are WebAssembly modules using ABI 3.

[Project](https://perinkpheral.dev) -
[Getting started](https://perinkpheral.dev/start) -
[Hardware](https://perinkpheral.dev/hardware) -
[Web flasher](https://perinkpheral.dev/flash) -
[Device setup](https://perinkpheral.dev/setup) -
[Firmware documentation](https://perinkpheral.dev/firmware) -
[Developer documentation](https://perinkpheral.dev/sdk).

Application guide (PDF): [English](https://github.com/GrakovNe/perinkpheral/releases/latest/download/perinkpheral-app-guide.pdf),
[Russian](https://github.com/GrakovNe/perinkpheral/releases/latest/download/perinkpheral-app-guide.ru.pdf).
Official applications and catalog: [perinkpheral-apps](https://github.com/GrakovNe/perinkpheral-apps).

## Build

Requires ESP-IDF 5.5, wasi-sdk and Java 21 or later. CI uses ESP-IDF 5.5.5
and wasi-sdk 34. `tools/perinkpheral` builds the Kotlin tools on first use.

```sh
export WASI_SDK_PATH=/path/to/wasi-sdk
. "$IDF_PATH/export.sh"
cd firmware
idf.py build
```

Host tests, from the repository root, require Java 21 or later, a C compiler and
Make. Keep `WASI_SDK_PATH` set to include the interpreter, delivery and frozen ABI checks:

```sh
make -C firmware/test/host test
```

CI builds and tests every push. Each successful push to `main` creates a
[release](https://github.com/GrakovNe/perinkpheral/releases) with release and debug firmware.

Devices with the old two-slot layout need a one-time USB migration. Choose **Keep data**
in the [web flasher](https://perinkpheral.dev/flash), then press BACK on the device to start
the installed firmware. Later updates use Wi-Fi and ask for OK on the device. A failed
update returns to recovery, where another firmware image can be uploaded; the previous
full firmware is no longer kept in a second slot.

For a normal update, open **Firmware** on the device's web page and check for updates
or choose `perinkpheral.bin`. The device shows preparation, then asks for physical OK.
To open recovery without starting an update, choose **Device > Enter recovery**.
BACK, labelled **Exit recovery**, tries the installed firmware if it is still valid.
Recovery keeps the `perink-recovery` access point available at `http://192.168.4.1`
and also tries the saved Wi-Fi network.

Wi-Fi updates replace main only. To update recovery itself, use **Keep data** in
the USB web flasher with the required release, or build locally and run
`idf.py -p "$PORT" recovery-flash` from `firmware/`. The latter writes only recovery;
install the matching main firmware separately when the change needs both.

## Repository

- `firmware/` - ESP-IDF project, components, system applications and host tests.
- `recovery/` - independent firmware updater, built with the main firmware.
- `sdk/` - host API, application library, CMake support and Hello sample.
- `tools/` - command line, shared Kotlin core, simulator and screen preview.
- [docs/](docs/README.md) - documentation index, application guide and web API reference.
- [hardware/](hardware/README.md) - panel notes, case model, print files and separate design studies.

## Related repositories

| Repository | Owns |
|---|---|
| [perinkpheral-apps](https://github.com/GrakovNe/perinkpheral-apps) | Applications, their simulator scenarios and the published catalog |
| [perinkpheral-clion-plugin](https://github.com/GrakovNe/perinkpheral-clion-plugin) | IDE integration using this repository's `tools/core` |
| [perinkpheral-hacs](https://github.com/GrakovNe/perinkpheral-hacs) | Home Assistant integration for the catalog's HA Companion app |
| [perinkpheral-dev-portal](https://github.com/GrakovNe/perinkpheral-dev-portal) | The bilingual site, assembly guide and browser flasher |

Firmware recovery is part of this repository under `recovery/`; it has no separate
source repository. Published firmware assets and flasher manifests come from this
repository's release workflows.

## License

[MIT](LICENSE). Departure Mono and Jersey fonts and their generated bitmaps
use [SIL OFL 1.1](assets/fonts/OFL-DepartureMono.txt)
([Jersey license](assets/fonts/OFL-Jersey.txt)).
