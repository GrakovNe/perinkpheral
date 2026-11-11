# wasm3

The interpreter core of [Wasm3](https://github.com/wasm3/wasm3) v0.9.0
(MIT, see `wasm3/LICENSE`): the eleven source files of the engine,
without WASI and without the libc bindings. Everything the device needs
is configured from `CMakeLists.txt`.

## The two local changes

`wasm3/m3_exec.h`, operations `ContinueLoop` and `ContinueLoopIf`: each
calls `m3_Yield()` before it goes round again, as `Call` already does.
Upstream yields only on calls, so a loop that calls nothing can never be
interrupted, and one such loop in an application would hang the host.
The lines are marked `perinkpheral`. When updating Wasm3, carry them
over or check whether upstream has closed its own TODO at that spot.

`wasm3/m3_core.c`, the allocator: with `d_m3ApplicationArea` defined the
interpreter's `calloc`, `free` and `realloc` are those of `m3_area.c`,
which is ours - a heap of its own inside one block of memory taken at
start. Nothing an application needs comes from the heap of the host, so
an application can neither fragment nor exhaust it, and the area is made
whole again every time an application is unloaded. The lines are marked
`perinkpheral`.
