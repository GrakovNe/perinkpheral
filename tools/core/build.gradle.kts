// SPDX-License-Identifier: MIT
// The perINKpheral tools' core: a library, what the tools know once - the contract, the
// formats of an application, its build and checks, a catalog, the device and the
// simulator's network. The CLion plugin uses it, and so does the command line, tools/cli.
// Nothing here but the JDK and Kotlin.
plugins {
    id("org.jetbrains.kotlin.jvm")
}

group = "org.perinkpheral"
version = "0.1.0"

kotlin {
    jvmToolchain(21)
}
