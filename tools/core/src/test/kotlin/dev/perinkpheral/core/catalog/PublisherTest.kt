// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.catalog

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

object PublisherTest {
    private val system = Publisher.CommandRunner.SYSTEM
    private lateinit var scratch: File
    private var fixtures = 0

    @JvmStatic fun main(args: Array<String>) {
        scratch = Files.createTempDirectory("perinkpheral-publisher-test-").toFile()
        try {
            publishUpdateAndRepeat()
            emptyRepository()
            invalidRepositories()
            failedBuild()
            occupiedTargets()
            changedRemote()
            invalidBuildOutput()
            changedByAttributes()
            ignoredCatalog()
            System.getenv("WASI_SDK_PATH")?.let(::realApplicationBuild)
        } finally { scratch.deleteRecursively() }
        println("Publisher checks passed: real Git publication, preservation, empty/default branches, idempotence, refusals and races")
    }

    private class Fixture(initial: Boolean = true, val branch: String = "releases/apps") {
        val directory = File(scratch, "case-${fixtures++}").apply { mkdir() }
        val remote = File(directory, "remote.git")
        val source = File(directory, "apps").apply { mkdir() }
        var metadata: Map<String, Any?> = linkedMapOf("full_name" to "test-user/my-apps", "private" to false,
            "permissions" to mapOf("push" to true), "default_branch" to branch)
        var builds = 0
        var pushes = 0
        var requests = 0
        var beforePush: (() -> Unit)? = null
        var beforeGit: ((List<String>, File) -> Unit)? = null
        var apps = linkedMapOf("clock" to "first", "weather" to "first")

        init {
            git(directory, "init", "--bare", "--initial-branch=$branch", remote.path)
            if (initial) editRemote {
                File(it, "README.md").writeText("source repository\n")
                File(it, "apps/clock").mkdirs()
                File(it, "apps/clock/clock.c").writeText("keep source code\n")
            }
        }

        val runner = Publisher.CommandRunner { command, directory ->
            requests++
            if (command.first() == "gh") {
                check(command.take(4) == listOf("gh", "api", "--hostname", "github.com"))
                val json = if (command.last() == "user") mapOf("login" to "test-user", "id" to 42L) else metadata
                Publisher.CommandResult(0, Json.pretty(json))
            } else {
                check(command.first() == "git")
                check("credential.helper=" in command)
                check("credential.https://github.com.helper=!gh auth git-credential" in command)
                check(command.none { it == "--global" || it.startsWith("+HEAD") })
                check("--force" !in command || "add" in command)
                beforeGit?.invoke(command, directory)
                if ("push" in command) {
                    pushes++
                    beforePush?.invoke()
                    check(command.takeLast(3) == listOf("push", "origin", "HEAD:refs/heads/$branch"))
                }
                system.run(command.map { if (it == "https://github.com/test-user/my-apps.git") remote.path else it }, directory)
            }
        }

        fun publisher(build: ((List<File>, File) -> Int)? = null) = Publisher(build ?: { folders, out ->
            builds++
            check(folders == listOf(source.canonicalFile))
            writeCatalog(out, apps)
        }, runner)

        fun publish() = publisher().publish(listOf(source), "test-user/my-apps")

        fun editRemote(edit: (File) -> Unit) {
            val clone = Files.createTempDirectory(directory.toPath(), "other-").toFile()
            git(directory, "clone", "--quiet", remote.path, clone.path)
            edit(clone)
            git(clone, "add", "--all")
            git(clone, "commit", "--quiet", "-m", "Repository change")
            git(clone, "push", "--quiet", "origin", "HEAD:refs/heads/$branch")
            clone.deleteRecursively()
        }

        fun head() = git(directory, "--git-dir=${remote.path}", "rev-parse", "refs/heads/$branch").trim()
        fun file(path: String) = git(directory, "--git-dir=${remote.path}", "show", "refs/heads/$branch:$path")
        fun paths() = git(directory, "--git-dir=${remote.path}", "ls-tree", "-r", "--name-only", "refs/heads/$branch").lines()
    }

    private fun publishUpdateAndRepeat() {
        val fixture = Fixture()
        val first = fixture.publish()
        check(first.changed && first.applications == 2 && first.repositoryUrl == "https://github.com/test-user/my-apps")
        check(fixture.file("README.md") == "source repository\n")
        check(fixture.file("apps/clock/clock.c") == "keep source code\n")
        check(fixture.paths().contains("catalog/weather/app.wasm"))
        fixture.apps = linkedMapOf("clock" to "second")
        check(fixture.publish().changed)
        check(!fixture.paths().contains("catalog/weather/app.wasm"))
        check(fixture.file("catalog/clock/app.wasm") == "second")
        val head = fixture.head()
        check(!fixture.publish().changed)
        check(fixture.head() == head && fixture.pushes == 2)
        check(fixture.file("README.md") == "source repository\n")
        check(fixture.file("apps/clock/clock.c") == "keep source code\n")
        check(fixture.source.list()!!.isEmpty())
    }

    private fun emptyRepository() {
        val fixture = Fixture(initial = false, branch = "first-release")
        check(fixture.publish().changed)
        check(fixture.paths().all { it.isEmpty() || it.startsWith("catalog/") })
        check(!fixture.publish().changed)
    }

    private fun invalidRepositories() {
        for (repository in listOf("https://github.com/user/repo", "user/repo/extra", "-user/repo", "user/..", "user/a;rm", "user/" + "a".repeat(100))) {
            val fixture = Fixture()
            fails { fixture.publisher().publish(listOf(fixture.source), repository) }
            check(fixture.requests == 0 && fixture.builds == 0 && fixture.pushes == 0)
        }
        for (override in listOf(mapOf("private" to true), mapOf("permissions" to mapOf("push" to false)),
            mapOf("archived" to true), mapOf("full_name" to "another/repo"))) {
            val fixture = Fixture()
            fixture.metadata = fixture.metadata + override
            val head = fixture.head()
            fails { fixture.publish() }
            check(fixture.head() == head && fixture.builds == 0 && fixture.pushes == 0)
        }
        val fixture = Fixture()
        fixture.metadata = fixture.metadata + ("default_branch" to "absent")
        val head = fixture.head()
        fails { fixture.publish() }
        check(fixture.pushes == 0 && fixture.head() == head)
    }

    private fun failedBuild() {
        val fixture = Fixture()
        val head = fixture.head()
        val publisher = fixture.publisher { _, out ->
            writeCatalog(out, mapOf("clock" to "partial"))
            throw BuildError("compiler rejected source")
        }
        fails { publisher.publish(listOf(fixture.source), "test-user/my-apps") }
        check(fixture.pushes == 0 && fixture.head() == head)
        check(fixture.source.list()!!.isEmpty())
    }

    private fun occupiedTargets() {
        val fixtures = listOf<(File) -> Unit>(
            { File(it, "catalog").writeText("not a directory") },
            { File(it, "catalog").mkdir(); File(it, "catalog/source.c").writeText("not generated") },
            { File(it, "catalog").mkdir(); File(it, "catalog/catalog.json").writeText("not JSON") },
            { writeCatalog(File(it, "catalog"), mapOf("clock" to "old")); File(it, "catalog/source.c").writeText("not generated") },
            { Files.createSymbolicLink(File(it, "catalog").toPath(), File("apps").toPath()) },
            { writeCatalog(File(it, "catalog"), mapOf("clock" to "old"));
                Files.createSymbolicLink(File(it, "catalog/clock/extra").toPath(), File("../../README.md").toPath()) },
        )
        for (edit in fixtures) {
            val fixture = Fixture()
            fixture.editRemote(edit)
            val head = fixture.head()
            fails { fixture.publish() }
            check(fixture.pushes == 0 && fixture.head() == head)
            check(fixture.file("README.md") == "source repository\n")
        }
    }

    private fun changedRemote() {
        val fixture = Fixture()
        fixture.beforePush = { fixture.editRemote { File(it, "README.md").writeText("someone else's change\n") } }
        fails { fixture.publish() }
        check(fixture.pushes == 1)
        check(fixture.file("README.md") == "someone else's change\n")
        check(fixture.paths().none { it.startsWith("catalog/") })
        fixture.beforePush = null
        check(fixture.publish().changed)
        check(fixture.file("README.md") == "someone else's change\n")
    }

    private fun invalidBuildOutput() {
        val fixture = Fixture()
        val head = fixture.head()
        val publisher = fixture.publisher { _, out ->
            writeCatalog(out, mapOf("clock" to "new"))
            File(out, "clock/app.wasm").writeText("changed after hashing")
            1
        }
        fails { publisher.publish(listOf(fixture.source), "test-user/my-apps") }
        check(fixture.pushes == 0 && fixture.head() == head)
    }

    private fun changedByAttributes() {
        val fixture = Fixture()
        fixture.editRemote { File(it, ".gitattributes").writeText("catalog/** text eol=lf\n") }
        fixture.apps = linkedMapOf("clock" to "binary\r\npayload")
        val head = fixture.head()
        fails { fixture.publish() }
        check(fixture.pushes == 0 && fixture.head() == head)
    }

    private fun ignoredCatalog() {
        val fixture = Fixture()
        fixture.editRemote { File(it, ".gitignore").writeText("catalog/\n") }
        check(fixture.publish().changed)
        check(fixture.file(".gitignore") == "catalog/\n")
        check(fixture.paths().contains("catalog/clock/app.wasm"))
    }

    private fun realApplicationBuild(wasi: String) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val fixture = Fixture()
        File(root, "sdk/apps/hello").copyRecursively(File(fixture.source, "hello"))
        val builder = AppBuilder(Sdk(root), File(wasi)) {}
        val publisher = fixture.publisher { folders, out -> Catalog.make(builder, folders, out) }
        check(publisher.publish(listOf(fixture.source), "test-user/my-apps").changed)
        check(fixture.file("catalog/hello/manifest.json").contains("hello"))
        check(!publisher.publish(listOf(fixture.source), "test-user/my-apps").changed)
    }

    private fun writeCatalog(out: File, apps: Map<String, String>): Int {
        out.mkdirs()
        val entries = apps.map { (name, body) ->
            val folder = File(out, name).apply { mkdirs() }
            val files = mapOf("app.wasm" to body, "manifest.json" to "{\"name\":\"$name\",\"abi\":3}").map { (filename, content) ->
                val bytes = content.toByteArray()
                File(folder, filename).writeBytes(bytes)
                mapOf("name" to filename, "size" to bytes.size,
                    "sha256" to MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) })
            }
            mapOf("name" to name, "files" to files)
        }
        File(out, Catalog.INDEX_NAME).writeText(Json.pretty(mapOf("catalog" to 1, "apps" to entries)) + "\n")
        return entries.size
    }

    private fun git(directory: File, vararg args: String): String {
        val result = system.run(listOf("git", "-c", "user.name=Publisher test", "-c", "user.email=test@example.invalid",
            "-c", "commit.gpgsign=false") + args, directory)
        check(result.exitCode == 0) { "git ${args.first()}: ${result.output}" }
        return result.output
    }

    private fun fails(action: () -> Unit) {
        try { action(); error("unsafe publication accepted") } catch (_: BuildError) { }
    }
}
