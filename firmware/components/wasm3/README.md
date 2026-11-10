# wasm3

The interpreter core of [Wasm3](https://github.com/wasm3/wasm3) v0.9.0
(MIT, see `wasm3/LICENSE`): the eleven source files of the engine,
without WASI and without the libc bindings. Everything the device needs
is configured from `CMakeLists.txt`.

## The one local change

`wasm3/m3_exec.h`, operations `ContinueLoop` and `ContinueLoopIf`: each
calls `m3_Yield()` before it goes round again, as `Call` already does.
Upstream yields only on calls, so a loop that calls nothing can never be
interrupted, and one such loop in an application would hang the host.
The lines are marked `perinkpheral`. When updating Wasm3, carry them
over or check whether upstream has closed its own TODO at that spot.
