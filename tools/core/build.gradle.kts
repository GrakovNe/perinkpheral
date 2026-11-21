// SPDX-License-Identifier: MIT
// The perINKpheral tools' core: a library, what the tools know once - the contract, the
// formats of an application, its build and checks, a catalog, the device and the
// simulator's network. The CLion plugin uses it, and so does the command line, tools/cli.
// Nothing here but the JDK and Kotlin.
plugins {
    id("org.jetbrains.kotlin.jvm")
}

group = "dev.perinkpheral"
version = "0.1.0"

kotlin {
    jvmToolchain(21)
}

// Uses only JDK/Kotlin, including a local HTTP server for transport failure cases.
val firmwareUpdateTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.device.FirmwareUpdateTest")
}
tasks.check { dependsOn(firmwareUpdateTest) }
val enduranceTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.device.EnduranceTest")
}
tasks.check { dependsOn(enduranceTest) }
val builderSafetyTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.BuilderSafetyTest")
}
tasks.check { dependsOn(builderSafetyTest) }
// The tests above have a main entry point, not JUnit annotations.
tasks.test { failOnNoDiscoveredTests.set(false) }

val publisherTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.catalog.PublisherTest")
}
tasks.check { dependsOn(publisherTest) }

val manifestAuthorTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.ManifestAuthorTest")
}
tasks.check { dependsOn(manifestAuthorTest) }

val simulatorHeadersTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.simulator.SimulatorHeadersTest")
}
tasks.check { dependsOn(simulatorHeadersTest) }

val simulatorPortTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.simulator.SimulatorPortTest")
}
tasks.check { dependsOn(simulatorPortTest) }

val inOrderTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.simulator.InOrderTest")
}
tasks.check { dependsOn(inOrderTest) }

val preinstalledTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.core.catalog.PreinstalledTest")
}
tasks.check { dependsOn(preinstalledTest) }
