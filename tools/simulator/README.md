# Simulator

The device without the device: one application, run by the firmware's own
interpreter, host API, kit and window manager on a computer. A frame here is
the frame the panel shows.

    tools/devtool/devtool.py sim <application folder> [--language ru] [--mocks mocks.json]

It answers on `http://127.0.0.1:8401` as the device does: `devtool.py --host
127.0.0.1:8401 shot | press | log | serve`, and the CLion plugin, work against it.

| Device | Simulator |
|---|---|
| Panel | a frame in memory, `/api/dev/screen` |
| Buttons | `/api/dev/button` |
| Store (NVS) | a folder, a file per key, the device's limits |
| Files (LittleFS) | the application's folder |
| Network | the computer's, through `fetch.py` and the Python that runs `devtool.py`; answers replaced by mocks |
| Clock | the computer's; moved on by `/api/sim/forward` |

Beyond the device's API:

| Request | Effect |
|---|---|
| `POST /api/sim/background` | the application leaves the screen; from then on it is loaded only for what comes to it, as on the device |
| `POST /api/sim/foreground` | the application takes the screen again |
| `POST /api/sim/intent?kind=started\|time_changed\|network_up` | the news, delivered if the manifest asks for it |
| `POST /api/sim/alarm?id=N` | alarm N comes now |
| `POST /api/sim/forward?seconds=N` | the clock and every deadline move on |

Mocks: a JSON object of address to answer. An address ending in `*` matches
every address that begins with the rest.

    {
      "https://api.open-meteo.com/*": { "status": 200, "file": "forecast.json" },
      "https://example.com/api/state": { "status": 401, "body": "{}" }
    }

Not simulated: the menu of several applications, the system API, e-paper
refresh timing, sleep and the battery. The application area of the device
(128 KB for the interpreter) is not enforced: `mkapp`'s report is the check.

Build: `make` for this computer (a C compiler, nothing else). `make platforms`
builds what the CLion plugin carries: `build/macos` (arm64 and x86_64, on macOS),
`build/linux-x86_64` and `build/linux-aarch64` (static, musl, cross-built with
[zig](https://ziglang.org)). Windows is not supported yet.
