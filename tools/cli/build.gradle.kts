// SPDX-License-Identifier: MIT
// The perINKpheral command line, tools/perinkpheral: build, check and run an application,
// on a device or in the simulator; make a catalog; and what the firmware and the project
// make of their sources - the host's screens before flashing, and the generators. It calls
// the core for everything the CLion plugin knows too.
plugins {
    id("org.jetbrains.kotlin.jvm")
}

group = "org.perinkpheral"
version = "0.1.0"

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation("org.perinkpheral:perinkpheral-core:0.1.0")
}

// One jar with the core and Kotlin's library in it: build/libs/perinkpheral.jar.
tasks.register<Jar>("cli") {
    archiveFileName.set("perinkpheral.jar")
    manifest {
        attributes("Main-Class" to "org.perinkpheral.cli.MainKt")
    }
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    from(sourceSets.main.get().output)
    dependsOn(configurations.runtimeClasspath)
    from({ configurations.runtimeClasspath.get().map { if (it.isDirectory) it else zipTree(it) } }) {
        exclude("META-INF/*.SF", "META-INF/*.DSA", "META-INF/*.RSA", "META-INF/versions/**")
    }
}
