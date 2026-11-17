# Frozen application contracts

ABI 3 is being prepared as the first frozen generation. Nothing is frozen until
the release. The host supports the explicit set `{3}`;
ABI 2 has no adapter and receives "Update the app."

`WASI_SDK_PATH=<wasi-sdk> tools/perinkpheral contract snapshot` creates or extends
`3.txt` and `3/fixtures.txt`. It refuses to replace a recorded entry. `contract check` compares all
recorded entries, allowing additions, and runs with the host tests in CI. Actual
Wasm types are also checked during every application build. Compiler measurements
use wasm32 C11, independently of the native host's pointer width.

The snapshot records import/export names and Wasm types, import privileges,
constants and function macros, permanent introduction stamps, struct sizes,
alignments, field types, sizes and offsets. The separate `3/fixtures.txt` records
SHA-256 hashes of the package inputs, including translations and saved state;
`contract check` verifies them with the same no-change rule as the contract.
After release, changing an existing signature or meaning requires another ABI; additions preserve
old entries. `HOST_SINCE_<name>` stamps only entries added after the ABI 3
firmware floor, 0.1.30. A new fixed layout
needs a new type/call. `answer_info` may grow after its unchanged prefix.

`3/` contains packages built once for this generation. Tests never rebuild them:

- Probe runs the same drawing, buffer, timer, time, storage, data and trap checks
  as the current probe. Delivery tests use its frozen module and manifest,
  including returning installation and trapping update handlers.
- Hello restores `presses.bin`, four little-endian bytes containing 42, handles
  one press, and saves 43. This file is the fixture for the `presses` KV key,
  rather than an application data file.
- Settings exercises a frozen system application and loading its English and
  Russian string tables; host tests compare the resulting frames. Its manifest
  translations remain package inputs too.

After release, do not rebuild or overwrite these inputs when changing the SDK. Add fixtures for
new features. Host tests cover the pure settings allowlist, request preparation
and notification consumption independently of these old binaries.

The ABI 3 firmware floor is `0.1.30`, the first firmware supporting ABI 3. Package minima are the
maximum of the generation floor, used import/export introduction stamps and the
explicit manifest minimum. New flags or manifest behaviour need an explicit
minimum when imported names cannot express their requirements.
