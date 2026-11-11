# Wasm3 v0.9.0 (https://github.com/wasm3/wasm3, MIT), the interpreter core
# only: no WASI, no libc bindings. The sources in wasm3/ carry one local
# change, described in README.md; everything else the device needs is set
# from here.
set(wasm3_dir "wasm3")
set(wasm3_sources
    "m3_bind.c" "m3_code.c" "m3_compile.c" "m3_core.c" "m3_env.c" "m3_exec.c"
    "m3_function.c" "m3_info.c" "m3_module.c" "m3_parse.c" "m3_validate.c")
list(TRANSFORM wasm3_sources PREPEND "${wasm3_dir}/")

idf_component_register(SRCS ${wasm3_sources}
                       INCLUDE_DIRS "${wasm3_dir}")

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    # Selects wasm3's defaults for small devices.
    ESP32
    # Keeps wasm3 from pinning every operation into RAM: on the C6 code
    # runs from flash and RAM is what applications are short of.
    M3_IN_IRAM
    # Compiled code is kept in pages of this size; the default is 32 KB.
    d_m3CodePageAlignSize=4096
    # An application gets one 64 KB page and can never grow: a module
    # that asks for more does not load.
    d_m3MaxLinearMemoryPages=1
    # How much of the host task's stack an application may take by calling
    # itself deeper and deeper; past it the call traps. Off by default on
    # this platform, which would let recursion run through the stack.
    d_m3MaxNativeStack=6144
    d_m3VerboseErrorMessages=0
    d_m3LogOutput=0
    # What an application may be made of is kept to what a 64 KB module
    # built without a C library uses. No floating point: the chip has no
    # FPU and every such operation would be the host's soft float, 34 KB
    # of it; a module with float operations loads and traps when it gets
    # to one, and tools/mkapp refuses it at build time. No reference
    # types, extended constants or compact imports: a module may still
    # have its one table of functions and call through it.
    d_m3HasFloat=0
    d_m3HasRefTypes=0
    d_m3HasExtendedConst=0
    d_m3HasCompactImports=0)

# An interpreter lives in its dispatch loop: optimize it for speed whatever
# the rest of the firmware is built with. The warnings are upstream's.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -w)
