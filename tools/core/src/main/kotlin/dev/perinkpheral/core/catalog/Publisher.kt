// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.catalog

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.security.MessageDigest

/** Builds locally, then replaces only a repository's generated catalog on its default branch. */
class Publisher(
    private val buildCatalog: (List<File>, File) -> Int,
    private val runner: CommandRunner = CommandRunner.SYSTEM,
) {
    data class Result(val repositoryUrl: String, val applications: Int, val changed: Boolean)
    data class CommandResult(val exitCode: Int, val output: String)

    fun interface CommandRunner {
        fun run(command: List<String>, directory: File): CommandResult

        companion object {
            val SYSTEM = CommandRunner { command, directory ->
                val process = ProcessBuilder(command).directory(directory).redirectErrorStream(true).apply {
                    environment().keys.removeIf { it.startsWith("GIT_") }
                    environment()["GIT_TERMINAL_PROMPT"] = "0"
                }.start()
                val output = process.inputStream.bufferedReader().use { it.readText() }
                CommandResult(process.waitFor(), output)
            }
        }
    }

    fun publish(folders: List<File>, repository: String): Result {
        if (!Regex("[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9_.-]{1,100}").matches(repository) ||
            repository.substringAfter('/') in setOf(".", "..")) {
            throw BuildError("--repo must be owner/repository, without a URL")
        }
        if ("https://raw.githubusercontent.com/$repository/HEAD/catalog".length > SOURCE_URL_MAX) {
            throw BuildError("repository name is too long for the device's catalog address")
        }
        if (folders.isEmpty() || folders.any { !it.isDirectory }) {
            throw BuildError("publish needs existing folders containing applications")
        }
        val sources = folders.map { it.canonicalFile }
        val temporary = Files.createTempDirectory("perinkpheral-publish-").toFile()
        try {
            val metadata = github("repos/$repository", temporary)
            if (metadata["private"] != false) throw BuildError("$repository must be public for the device to download it")
            if ((metadata["permissions"] as? Map<*, *>)?.get("push") != true || metadata["archived"] == true) {
                throw BuildError("the authenticated GitHub user cannot push to $repository")
            }
            if (!(metadata["full_name"] as? String).equals(repository, ignoreCase = true)) {
                throw BuildError("repository was renamed; use its current owner/repository")
            }
            val branch = metadata["default_branch"] as? String
                ?: throw BuildError("GitHub did not report the repository's default branch")
            val user = github("user", temporary)
            val login = user["login"] as? String ?: throw BuildError("GitHub did not identify the authenticated user")
            val id = (user["id"] as? Number)?.toLong() ?: throw BuildError("GitHub did not identify the authenticated user")
            if (!Regex("[A-Za-z0-9-]+").matches(login) || id <= 0) throw BuildError("invalid GitHub user identity")

            val generated = File(temporary, "generated")
            val count = buildCatalog(sources, generated)
            validateCatalog(generated, verifyHashes = true)

            val checkout = File(temporary, "checkout").apply { mkdir() }
            val hooks = File(temporary, "hooks").apply { mkdir() }
            // Per-command configuration does not change the user's Git configuration or expose a token.
            fun git(vararg arguments: String): CommandResult = execute(listOf("git", "-c", "credential.helper=",
                "-c", "credential.https://github.com.helper=!gh auth git-credential",
                "-c", "core.hooksPath=${hooks.path}", "-c", "commit.gpgsign=false",
                "-c", "user.name=$login", "-c", "user.email=$id+$login@users.noreply.github.com") + arguments, checkout)
            fun checkedGit(vararg arguments: String): String {
                val result = git(*arguments)
                if (result.exitCode != 0) throw BuildError("git ${arguments.first()}: ${result.output.trim()}")
                return result.output
            }
            checkedGit("check-ref-format", "--branch", branch)
            checkedGit("init", "--quiet", "--initial-branch=$branch")
            checkedGit("remote", "add", "origin", "https://github.com/$repository.git")
            val heads = checkedGit("ls-remote", "--heads", "origin")
            val branchRef = "refs/heads/$branch"
            if (heads.lineSequence().any { it.substringAfter('\t', "") == branchRef }) {
                checkedGit("fetch", "--quiet", "origin", "$branchRef:refs/remotes/origin/$branch")
                checkedGit("checkout", "--quiet", "-B", branch, "refs/remotes/origin/$branch")
            } else if (heads.isNotBlank()) {
                throw BuildError("default branch $branch is absent; no catalog was published")
            }

            val target = File(checkout, "catalog")
            if (Files.exists(target.toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS)) {
                validateCatalog(target, verifyHashes = false, allowEmpty = true)
                if (!target.deleteRecursively()) throw BuildError("cannot replace catalog in temporary checkout")
            }
            generated.copyRecursively(target)
            checkedGit("add", "--force", "--all", "--", "catalog")
            // Git attributes and clean filters must not invalidate the index's byte hashes.
            generated.walkTopDown().filter { it.isFile }.forEach { file ->
                val relative = file.relativeTo(generated).invariantSeparatorsPath
                val expected = checkedGit("hash-object", "--no-filters", file.path).trim()
                val staged = checkedGit("rev-parse", ":catalog/$relative").trim()
                if (expected != staged) throw BuildError("Git attributes changed catalog/$relative; adjust .gitattributes before publishing")
            }
            val diff = git("diff", "--cached", "--quiet", "--", "catalog")
            if (diff.exitCode == 0) return Result("https://github.com/$repository", count, false)
            if (diff.exitCode != 1) throw BuildError("cannot inspect the staged catalog: ${diff.output.trim()}")
            checkedGit("commit", "--quiet", "-m", "Update application catalog")
            val pushed = git("push", "origin", "HEAD:$branchRef")
            if (pushed.exitCode != 0) {
                throw BuildError("catalog push failed; no force push was attempted. Run publish again after checking the repository.\n" +
                    pushed.output.trim())
            }
            return Result("https://github.com/$repository", count, true)
        } finally {
            temporary.deleteRecursively()
        }
    }

    private fun github(path: String, directory: File): Map<*, *> {
        val response = execute(listOf("gh", "api", "--hostname", "github.com", path), directory)
        if (response.exitCode != 0) throw BuildError("GitHub access failed; run gh auth login and check repository access.\n" +
            response.output.trim())
        return try { Json.parse(response.output) as? Map<*, *> ?: throw IllegalArgumentException() }
        catch (_: Exception) { throw BuildError("GitHub returned invalid repository or user information") }
    }

    private fun execute(command: List<String>, directory: File): CommandResult = try {
        runner.run(command, directory)
    } catch (error: IOException) {
        throw BuildError("cannot run ${command.first()}: install Git and GitHub CLI (gh), then run gh auth login")
    }

    /** Only files declared by the index may be replaced; source trees and links are never removed. */
    private fun validateCatalog(folder: File, verifyHashes: Boolean, allowEmpty: Boolean = false) {
        if (Files.isSymbolicLink(folder.toPath()) || !folder.isDirectory) throw BuildError("catalog target must be a directory, not a file or symlink")
        val paths = Files.walk(folder.toPath()).use { it.toList() }
        if (paths.any { Files.isSymbolicLink(it) }) throw BuildError("catalog contains a symlink; refusing to replace it")
        if (allowEmpty && paths.size == 1) return
        val index = File(folder, Catalog.INDEX_NAME)
        if (!index.isFile || index.length() > INDEX_MAX_BYTES) throw BuildError("catalog has no valid catalog.json; refusing to replace it")
        val catalog = try { Json.parse(index.readText()) as? Map<*, *> } catch (_: Exception) { null }
        val entries = catalog?.get("apps") as? List<*>
        if ((catalog?.get("catalog") as? Number)?.toInt() != 1 || entries == null) {
            throw BuildError("catalog has no valid application index; refusing to replace it")
        }
        val allowed = mutableSetOf(Catalog.INDEX_NAME)
        val names = mutableSetOf<String>()
        for (entry in entries) {
            val app = entry as? Map<*, *> ?: throw BuildError("invalid catalog application")
            val name = app["name"] as? String ?: throw BuildError("catalog application has no name")
            if (!Regex("[a-z0-9_-]{1,13}").matches(name) || !names.add(name)) throw BuildError("invalid or duplicate catalog application name")
            allowed += name
            val files = app["files"] as? List<*> ?: throw BuildError("$name has no package files")
            if (files.size !in 2..PACKAGE_FILES_MAX) throw BuildError("$name: a published package must contain 2 to $PACKAGE_FILES_MAX files")
            val filenames = mutableSetOf<String>()
            for (item in files) {
                val record = item as? Map<*, *> ?: throw BuildError("$name: invalid package file")
                val filename = record["name"] as? String ?: throw BuildError("$name: unnamed package file")
                if (!Regex("[a-z0-9_-][a-z0-9_.-]{0,22}").matches(filename) || filename.endsWith(".new") || !filenames.add(filename)) {
                    throw BuildError("$name: invalid or duplicate package filename")
                }
                val relative = "$name/$filename"
                allowed += relative
                val file = File(folder, relative)
                if (!file.isFile) throw BuildError("catalog file $relative is missing")
                if (verifyHashes) {
                    if (filename == "manifest.json" && file.length() > MANIFEST_MAX_BYTES) throw BuildError("$name: packaged manifest exceeds $MANIFEST_MAX_BYTES bytes")
                    val hash = MessageDigest.getInstance("SHA-256").digest(file.readBytes()).joinToString("") { "%02x".format(it) }
                    if ((record["size"] as? Number)?.toLong() != file.length() || record["sha256"] != hash) {
                        throw BuildError("catalog file $relative does not match its index")
                    }
                }
            }
            if (!filenames.containsAll(listOf("app.wasm", "manifest.json"))) throw BuildError("$name: incomplete application package")
        }
        if (paths.drop(1).any { folder.toPath().relativize(it).joinToString("/") !in allowed }) {
            throw BuildError("catalog contains files outside its index; move them before publishing")
        }
    }

    companion object {
        // Firmware catalog.h, web_apps.c and app_store.c bounds, including generated metadata.
        private const val SOURCE_URL_MAX = 127
        private const val INDEX_MAX_BYTES = 512 * 1024
        private const val PACKAGE_FILES_MAX = 8
        private const val MANIFEST_MAX_BYTES = 8192
    }
}
