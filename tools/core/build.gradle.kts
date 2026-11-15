// SPDX-License-Identifier: MIT
// The perINKpheral tools: build, check and run an application, on a device or in the
// simulator, and make a catalog. A library the CLion plugin uses, and a command line,
// tools/perinkpheral, for everybody else. Nothing here but the JDK and Kotlin.
plugins {
    id("org.jetbrains.kotlin.jvm")
}

group = "org.perinkpheral"
version = "0.1.0"

kotlin {
    jvmToolchain(21)
}

// The command line: one jar with Kotlin's library in it, build/libs/perinkpheral.jar.
// The library itself is the plain jar, which the CLion plugin takes without Kotlin's
// library (the IDE has its own).
tasks.register<Jar>("cli") {
    archiveFileName.set("perinkpheral.jar")
    manifest {
        attributes("Main-Class" to "org.perinkpheral.core.cli.MainKt")
    }
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    from(sourceSets.main.get().output)
    dependsOn(configurations.runtimeClasspath)
    from({ configurations.runtimeClasspath.get().map { if (it.isDirectory) it else zipTree(it) } }) {
        exclude("META-INF/*.SF", "META-INF/*.DSA", "META-INF/*.RSA", "META-INF/versions/**")
    }
}
