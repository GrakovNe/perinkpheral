// SPDX-License-Identifier: MIT
rootProject.name = "perinkpheral-cli"

pluginManagement {
    plugins {
        id("org.jetbrains.kotlin.jvm") version "2.4.21"
    }
}

plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
    }
}

// The core, built beside it from its own folder.
includeBuild("../core")
