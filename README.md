<img src="assets/mark/mark-on-paper.svg" width="96" alt="The perINKpheral mark: two corners of an empty frame">

# perINKpheral

Firmware, SDK and tools for perINKpheral, an ESP32-C6 desk E-Ink display
with four buttons. Applications are WebAssembly modules using ABI 3.

[Project](https://perinkpheral.dev) -
[Getting started](https://perinkpheral.dev/start) -
[Hardware](https://perinkpheral.dev/hardware) -
[Web flasher](https://perinkpheral.dev/flash) -
[Firmware documentation](https://perinkpheral.dev/firmware) -
[Developer documentation](https://perinkpheral.dev/sdk).

Application guide (PDF): [English](docs/guide/perinkpheral-app-guide.pdf),
[Russian](docs/guide/perinkpheral-app-guide.ru.pdf).
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

Host tests, from the repository root, require a C compiler and Make:

```sh
make -C firmware/test/host test
```

CI builds and tests every push. Each successful push to `main` creates a
[release](https://github.com/GrakovNe/perinkpheral/releases) with release and debug firmware.

## Repository

- `firmware/` - ESP-IDF project, components, system applications and host tests.
- `sdk/` - host API, application library, CMake support and Hello sample.
- `tools/` - command line, shared Kotlin core, simulator and screen preview.
- `docs/` - application guide and web API reference.
- `hardware/` - panel notes, assembly files and case model.

## License

[MIT](LICENSE). Departure Mono and Jersey fonts and their generated bitmaps
use [SIL OFL 1.1](assets/fonts/OFL-DepartureMono.txt)
([Jersey license](assets/fonts/OFL-Jersey.txt)).
