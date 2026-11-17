// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

import org.perinkpheral.core.BuildError
import org.perinkpheral.core.contract.Contract
import java.io.File
import java.nio.file.Files
import java.util.regex.Matcher
import java.util.regex.Pattern

/**
 * The application guide, docs/guide: guide.html, written by hand, into a PDF, and its
 * translations.
 *
 * A translation is made of pieces: a paragraph, a list item, a heading, a caption, a table cell
 * or a comment in a code block. guide.<language>.txt holds pairs: a piece as `units list` prints
 * it, its translation on the next line, an empty line. A piece that is not there stays as it is -
 * a name, a number, code; a translation of "=" says so on purpose.
 */
class Guide(root: File) {
    private val folder = File(root, "docs/guide")
    private val rootUrl = "file://" + root.absoluteFile.normalize().path

    // ---- the pieces ----

    private val piece = Pattern.compile(
        "(<p(?: [^>]*)?>)(.*?)(</p>)|(<li>)(.*?)(</li>)|(<h[123][^>]*>)(.*?)(</h[123]>)|(<figcaption>)(.*?)(</figcaption>)" +
            "|(<t[dh][^>]*>)(.*?)(</t[dh]>)|(<div class=\"rule\"><b>)(.*?)(</b>)|((?<=</b>) )(.*?)(</div>)" +
            "|(<div class=\"facts\">)(.*?)(</div>)|(<title>)(.*?)(</title>)", Pattern.DOTALL)
    private val comment = Pattern.compile(
        "(/\\* )(.*?)( \\*/)|(# )([^\\n<]*)()|(  [a-z.-]+/?  +)((?:optional|identity|source)[^\\n]*?)(  +\\(section|\\n|(?=</pre>))")

    private data class Piece(val start: Int, val end: Int, val text: String)

    private fun firstGroup(matcher: Matcher, groups: IntProgression): Int = groups.first { matcher.group(it) != null }

    /** Everything a reader reads, in the order of the file. */
    private fun pieces(source: String): List<Piece> {
        val found = ArrayList<Piece>()
        val bodyFrom = source.indexOf("<body>")
        val pre = Regex("<pre>.*?</pre>", RegexOption.DOT_MATCHES_ALL).findAll(source).map { it.range.first to it.range.last + 1 }.toList()
        fun inPre(at: Int) = pre.any { (a, b) -> at in a until b }
        Regex("<title>(.*?)</title>", RegexOption.DOT_MATCHES_ALL).findAll(source.substring(0, bodyFrom)).forEach {
            val group = it.groups[1]!!
            found += Piece(group.range.first, group.range.last + 1, group.value)
        }
        val pieces = piece.matcher(source)
        var from = bodyFrom
        while (from <= source.length && pieces.find(from)) {
            from = if (pieces.end() > pieces.start()) pieces.end() else pieces.end() + 1
            if (inPre(pieces.start())) continue
            val group = firstGroup(pieces, 2..29 step 3)
            found += Piece(pieces.start(group), pieces.end(group), pieces.group(group))
        }
        for ((a, b) in pre) {
            val comments = comment.matcher(source).region(a, b)
            while (comments.find()) {
                val group = firstGroup(comments, 2..8 step 3)
                if (comments.group(group).isNotBlank()) found += Piece(comments.start(group), comments.end(group), comments.group(group))
            }
        }
        return found.sortedWith(compareBy({ it.start }, { it.end }, { it.text }))
    }

    private val space = Pattern.compile("\\s+", Pattern.UNICODE_CHARACTER_CLASS)
    private fun flat(text: String) = space.matcher(text).replaceAll(" ").trim()

    private fun translations(language: String): Map<String, String> {
        val pairs = HashMap<String, String>()
        val lines = File(folder, "guide.$language.txt").readLines()
        var at = 0
        while (at < lines.size) {
            if (lines[at].isBlank()) {
                at++
                continue
            }
            if (at + 1 >= lines.size || lines[at + 1].isBlank()) {
                throw BuildError("guide.$language.txt, line ${at + 1}: a piece without a translation")
            }
            pairs[flat(lines[at])] = lines[at + 1]
            at += 2
        }
        return pairs
    }

    /** Whether a person reads it: letters left once code and markup are taken away. */
    private fun isText(piece: String) =
        Regex("[A-Za-z]{2}").containsMatchIn(Regex("<code>.*?</code>|<[^>]+>|&[a-z]+;").replace(piece, ""))

    private fun source() = File(folder, "guide.html").readText()

    /** The pieces of guide.html, one a line. */
    fun list(): List<String> = pieces(source()).map { flat(it.text) }

    /** The pieces guide.<language>.txt has no translation for. */
    fun missing(language: String): List<String> {
        val pairs = translations(language)
        val seen = LinkedHashSet<String>()
        for (piece in pieces(source())) {
            val text = flat(piece.text)
            if (isText(piece.text) && text !in pairs) seen += text
        }
        return seen.toList()
    }

    /** guide.<language>.html from guide.html and guide.<language>.txt. */
    fun apply(language: String): String {
        val source = source()
        val found = pieces(source)
        val pairs = translations(language)
        val out = StringBuilder()
        var at = 0
        var done = 0
        for (piece in found) {
            val new = pairs[flat(piece.text)]
            out.append(source, at, piece.start)
            if (new == null || new == "=") {
                out.append(source, piece.start, piece.end)
            } else {
                out.append(new)
                done++
            }
            at = piece.end
        }
        out.append(source, at, source.length)
        var result = out.toString().replace("<html lang=\"en\">", "<html lang=\"$language\">")
        // A reference to a section at the end of a line of code is outside the comment before it.
        pairs["(section"]?.let { word -> result = Regex("\\(section (\\d+)\\)").replace(result) { "$word ${it.groupValues[1]})" } }
        // The running foot is in the style sheet, not in the text.
        val foot = "perINKpheral / application guide"
        pairs[foot]?.let { result = result.replace("content: \"$foot\"", "content: \"$it\"") }
        File(folder, "guide.$language.html").writeText(result)
        return "guide.$language.html: $done pieces translated of ${found.size}"
    }

    // ---- the PDF ----

    private data class Heading(val level: Int, val id: String, val title: String)

    /** The h1 and h2 of the guide, in order. */
    private fun headings(text: String): List<Heading> =
        Regex("<h([12])([^>]*)>(.*?)</h\\1>", RegexOption.DOT_MATCHES_ALL).findAll(text).mapNotNull { match ->
            val (level, attributes, title) = match.destructured
            val id = Regex("id=\"([^\"]+)\"").find(attributes)?.groupValues?.get(1)
            if (id == null || "notoc" in attributes) null
            else Heading(level.toInt(), id, Regex("<[^>]+>").replace(title, "").trim())
        }.toList()

    private fun escape(text: String) = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        .replace("\"", "&quot;").replace("'", "&#x27;")

    private fun contents(found: List<Heading>, pages: Map<String, Int>) = found.joinToString("\n") { (level, id, title) ->
        "<a class=\"toc toc$level\" href=\"#$id\"><span>${escape(title)}</span><i></i><b>${pages[id] ?: ""}</b></a>"
    }

    private fun browser(): String {
        val candidates = listOfNotNull(System.getenv("CHROME")) + listOf("google-chrome", "chromium", "chromium-browser", "chrome",
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Chromium.app/Contents/MacOS/Chromium")
        for (candidate in candidates) {
            if (File(candidate).canExecute()) return candidate
            System.getenv("PATH").orEmpty().split(File.pathSeparator).map { File(it, candidate) }.firstOrNull { it.canExecute() }?.let { return it.path }
        }
        throw BuildError("no Chromium browser found: set CHROME to one")
    }

    private fun run(vararg command: String): String {
        val process = ProcessBuilder(*command).redirectErrorStream(true).start()
        val output = process.inputStream.readBytes().toString(Charsets.UTF_8)
        if (process.waitFor() != 0) throw BuildError("${command[0].substringAfterLast('/')} failed: ${output.takeLast(400)}")
        return output
    }

    private fun printPdf(page: File, out: File) {
        run(browser(), "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--print-to-pdf=${out.path}", "file://${page.path}")
        if (!out.isFile) throw BuildError("the browser did not print")
    }

    /** On which page each heading stands: its marker is printed, invisibly small, inside it. */
    private fun pagesOf(pdf: File): Map<String, Int> {
        val text = run("pdftotext", "-layout", pdf.path, "-")
        val pages = HashMap<String, Int>()
        text.split("\u000c").forEachIndexed { index, page ->
            Regex("\\[\\[([a-z0-9-]+)]]").findAll(page).forEach { pages.putIfAbsent(it.groupValues[1], index + 1) }
        }
        return pages
    }

    /**
     * The guide into a PDF: a table of contents made of its headings, printed with a Chromium
     * browser twice - the first print tells on which page every heading landed - and the cover,
     * printed to its edges, joined in front with pdfunite of poppler. A number of the guide's
     * table of limits that the headers have otherwise stops it.
     */
    fun pdf(contract: Contract, language: String, outFile: File?): String {
        ContractTools.checkLimits(contract, File(folder, "guide.html"))
        val suffix = if (language == "en") "" else ".$language"
        val out = outFile ?: File(folder, "perinkpheral-app-guide$suffix.pdf")
        // A translation is made of guide.html and guide.<language>.txt each time: it is never edited.
        if (suffix.isNotEmpty()) apply(language)
        var source = File(folder, "guide$suffix.html").readText()
        val found = headings(source)
        // Where the fonts, the mark and the screens are: the page is printed from a scratch folder.
        source = source.replace("{{ROOT}}", rootUrl)
        // A table's first row is its head, and is said again on every page the table runs to.
        source = Regex("<table>\\s*(<tr><th.*?</tr>)", RegexOption.DOT_MATCHES_ALL).replace(source) { "<table><thead>${it.groupValues[1]}</thead>" }
        // A section's number on a plate of its own: "4. A first application".
        source = Regex("(<h1 id=\"[^\"]+\">)(\\d+)\\. ").replace(source) { "${it.groupValues[1]}<span class=\"plate\">${it.groupValues[2]}</span>" }
        // The cover is a sheet of its own, printed to its edges; a browser does not print one page
        // of a document without margins and the rest with them. The two are joined afterwards.
        val coverFrom = source.indexOf("<div class=\"cover\">")
        var coverTo = source.indexOf("</div>", source.indexOf("class=\"facts\"")) + "</div>".length
        coverTo = source.indexOf("</div>", coverTo) + "</div>".length
        val bodyFrom = source.indexOf("<body>") + "<body>".length
        val cover = source.substring(0, bodyFrom) + "\n<style>@page { margin: 0; @bottom-left { content: none; border: 0; } " +
            "@bottom-right { content: none; border: 0; } }</style>\n" + source.substring(coverFrom, coverTo) + "\n</body>\n</html>\n"
        // The pages after the cover count from 2.
        source = source.substring(0, coverFrom) + "<style>html { counter-reset: page 1; }</style>" + source.substring(coverTo)
        val scratch = Files.createTempDirectory("perinkpheral-guide").toFile()
        try {
            val page = File(scratch, "guide.html")
            val first = File(scratch, "first.pdf")
            val coverPdf = File(scratch, "cover.pdf")
            val bodyPdf = File(scratch, "body.pdf")
            // A marker inside every heading, for the first print to be searched for.
            val marked = Regex("(<h[12][^>]*id=\"([^\"]+)\"[^>]*>)").replace(source) { "${it.groupValues[1]}<span class=\"mark\">[[${it.groupValues[2]}]]</span>" }
            page.writeText(marked.replace("<!--CONTENTS-->", contents(found, emptyMap())))
            printPdf(page, first)
            val pages = pagesOf(first).mapValues { it.value + 1 }
            page.writeText(source.replace("<!--CONTENTS-->", contents(found, pages)))
            printPdf(page, bodyPdf)
            page.writeText(cover)
            printPdf(page, coverPdf)
            run("pdfunite", coverPdf.path, bodyPdf.path, out.absolutePath)
            val missing = found.map { it.id }.filter { it !in pages }
            return (if (missing.isNotEmpty()) "no page found for: ${missing.joinToString(", ")}\n" else "") +
                "${out.path}: ${found.size} headings"
        } finally {
            scratch.deleteRecursively()
        }
    }
}
