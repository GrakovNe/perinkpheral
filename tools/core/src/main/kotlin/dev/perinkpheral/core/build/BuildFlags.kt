// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

/**
 * How an application is compiled and linked: written here once. The build uses them, the
 * SDK's CMake module asks for them (`tools/perinkpheral flags`), and the CLion plugin writes
 * them into its toolchain file, so that what an IDE compiles is what the device gets.
 */
object BuildFlags {
    /** One wasm page: the whole memory of an application, fixed for every one. */
    const val WASM_PAGE = 65536

    /** The stack at the bottom of that page. */
    const val APP_STACK = 8192

    const val TARGET = "--target=wasm32"

    /** No C library, built for size, every warning an error. */
    val compile = listOf("-nostdlib", "-ffreestanding", "-fno-builtin", "-Oz", "-flto", "-Wall", "-Wextra", "-Werror")

    /** What the linker is told: no entry, nothing kept that is not called, one page and no more. */
    val linker = listOf("-Wl,--no-entry", "-Wl,--strip-all", "-Wl,--gc-sections",
        "-Wl,--initial-memory=$WASM_PAGE", "-Wl,--max-memory=$WASM_PAGE", "-Wl,-z,stack-size=$APP_STACK")

    /** The link step on its own, as a build system makes it: the driver has to hear again
     * that there is no C library and that the code is link-time optimized. */
    val link = listOf("-nostdlib", "-flto") + linker

    /** A list as CMake reads one. */
    fun cmake(flags: List<String>): String = flags.joinToString(";")
}
