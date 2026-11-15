# Simulator

The device without the device: one application, run by the firmware's own
interpreter, host API, kit and window manager on a computer. A frame here is
the frame the panel shows.

    tools/perinkpheral sim <application folder> [--language ru] [--mocks mocks.json]

It answers on `http://127.0.0.1:8401` as the device does: `tools/perinkpheral --host
127.0.0.1:8401 shot | press | log | serve`, and the CLion plugin, work against it.

| Device | Simulator |
|---|---|
| Panel | a frame in memory, `/api/dev/screen`; its address in a browser shows the page of `perinkpheral serve` - the screen, the buttons, the log, and the requests below as buttons |
| Buttons | `/api/dev/button` |
| Store (NVS) | a folder, a file per key, the device's limits |
| Files (LittleFS) | the application's folder |
| Network | that of the program that started it - tools/perinkpheral, or the CLion plugin in the IDE's own process - asked a JSON line an exchange on `--fetcher-port`; a redirect is followed by the simulator itself, by the firmware's rules (`app_manifest_is_same_host`, `app_manifest_allows_url`): a GET three times at most, a request of the application's own inside its hosts, its headers not to another host; an application's own certificate authorities (`certificates`) trusted for its requests beside the computer's; answers replaced by mocks |
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
(128 KB for the interpreter) is not enforced: the build's report is the check.

Build: `make` for this computer (a C compiler, nothing else). `make platforms`
builds what the CLion plugin carries: `build/macos` (arm64 and x86_64, on macOS),
`build/linux-x86_64` and `build/linux-aarch64` (static, musl, cross-built with
[zig](https://ziglang.org)). Windows is not supported yet.
