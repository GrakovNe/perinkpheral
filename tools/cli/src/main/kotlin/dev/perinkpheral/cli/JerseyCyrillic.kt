// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

/**
 * Cyrillic letters for the Jersey faces, which have none.
 *
 * Jersey (SIL OFL 1.1, no reserved font name): optical masters for the difficult lowercase
 * junctions, and shared Latin curves and stems for the remaining letters. Each included face
 * has its own drawings and weights; shortening a capital is not a finished lowercase design.
 * Covers Russian, Ukrainian, Belarusian, Serbian, Macedonian, Bulgarian and Kazakh.
 */
class JerseyCyrillic(private val render: (Int) -> FontFace.Glyph?) {
    private val h = render('H'.code)!!
    private val cap = h.height
    private val width = h.width
    private val xHeight = render('x'.code)!!.height
    private val descent = render('p'.code)!!.height - xHeight
    private val stem = h.rows[0].indexOfFirst { !it }
    private data class Metrics(
        val xHeight: Int, val stem: Int, val crossbar: Int, val bowlJoin: Int,
        val roundCap: Int, val width: Int, val wide: Int, val tailDepth: Int,
        val overhang: Int, val internalGap: Int,
    )
    private val metrics = when (cap) {
        10 -> Metrics(8, 2, 2, 1, 2, 7, 10, 2, 1, 1)
        15 -> Metrics(11, 3, 2, 2, 3, 10, 15, 3, 1, 2)
        25 -> Metrics(19, 5, 3, 3, 4, 16, 23, 4, 2, 3)
        else -> error("No Cyrillic optical master for Jersey $cap")
    }
    private val masters by lazy {
        drawings().mapValues { (letter, drawing) ->
            val rows = drawing.trimIndent().lines()
            require(rows.isNotEmpty() && rows.all { row -> row.length == rows[0].length && row.all { it == '#' || it == '.' } }) {
                "Jersey $cap $letter: invalid drawing"
            }
            val expectedHeight = when (letter) { 'б', 'У', 'З' -> cap; 'д' -> xHeight + metrics.tailDepth; else -> xHeight }
            require(rows.size == expectedHeight) { "Jersey $cap $letter: expected $expectedHeight rows, got ${rows.size}" }
            val expectedWidth = when (letter) {
                'ж', 'м' -> metrics.wide
                'к' -> render('k'.code)!!.width
                'д' -> metrics.width + 2 * metrics.overhang
                'У' -> width
                'З' -> render('3'.code)!!.width
                else -> metrics.width
            }
            require(rows[0].length == expectedWidth) { "Jersey $cap $letter: expected width $expectedWidth" }
            rows.map { row -> BooleanArray(row.length) { row[it] == '#' } }.toMutableList().also {
                checkCounters(letter, it)
            }
        }
    }
    private val out = HashMap<Int, FontFace.Glyph>()

    init {
        require(xHeight == metrics.xHeight && stem == metrics.stem && render('o'.code)!!.width == metrics.width)
    }

    private fun master(letter: Char) = copy(masters.getValue(letter))
    private fun bar(height: Int) = if (height == xHeight) metrics.crossbar else stem
    private fun bodyWidth(height: Int) = if (height == xHeight) metrics.width else width

    /** Enclosed paper, in reading order. The outside is not a counter, even beside a neck. */
    private fun counters(rows: List<BooleanArray>): List<IntRange> {
        val height = rows.size
        val width = rows[0].size
        val visited = canvas(width, height)
        val counters = ArrayList<IntRange>()
        for (y in 0 until height) for (x in 0 until width) {
            if (rows[y][x] || visited[y][x]) continue
            val pending = ArrayDeque<Pair<Int, Int>>()
            pending.add(x to y)
            visited[y][x] = true
            var touchesEdge = false
            var first = y
            var last = y
            while (pending.isNotEmpty()) {
                val (column, row) = pending.removeFirst()
                touchesEdge = touchesEdge || column == 0 || column == width - 1 || row == 0 || row == height - 1
                first = minOf(first, row)
                last = maxOf(last, row)
                for ((nextX, nextY) in listOf(column - 1 to row, column + 1 to row, column to row - 1, column to row + 1)) {
                    if (nextX in 0 until width && nextY in 0 until height && !rows[nextY][nextX] && !visited[nextY][nextX]) {
                        visited[nextY][nextX] = true
                        pending.add(nextX to nextY)
                    }
                }
            }
            if (!touchesEdge) counters += first..last
        }
        return counters
    }

    private fun checkCounters(letter: Char, rows: List<BooleanArray>) {
        if (letter !in "бвьяъыљњЬЪЫЉЊфФ") return
        val spaces = counters(rows)
        val expected = if (letter in "вфФ") 2 else 1
        require(spaces.size == expected) { "Jersey $cap $letter: expected $expected enclosed counters" }
        val minimum = if (letter in "вя") metrics.bowlJoin else metrics.roundCap
        require(spaces.all { it.last - it.first + 1 >= minimum }) { "Jersey $cap $letter: counter too small" }
        if (letter == 'в') {
            require(spaces[1].last - spaces[1].first >= spaces[0].last - spaces[0].first) { "Jersey $cap: lower bowl of в is too small" }
            require(spaces[1].first - spaces[0].last - 1 == metrics.bowlJoin) { "Jersey $cap: wrong join weight in в" }
        }
    }

    // ---- rows: a list of rows of cells ----

    private fun copy(rows: List<BooleanArray>): MutableList<BooleanArray> = rows.map { it.copyOf() }.toMutableList()
    private fun canvas(width: Int, height: Int): MutableList<BooleanArray> = MutableList(height) { BooleanArray(width) }

    private fun fill(rows: MutableList<BooleanArray>, x: Int, y: Int, width: Int, height: Int, ink: Boolean = true) {
        for (row in maxOf(y, 0) until minOf(y + height, rows.size)) {
            for (column in maxOf(x, 0) until minOf(x + width, rows[row].size)) rows[row][column] = ink
        }
    }

    private fun blit(rows: MutableList<BooleanArray>, source: List<BooleanArray>, x: Int, y: Int) {
        source.forEachIndexed { row, line ->
            line.forEachIndexed { column, ink ->
                if (ink && y + row in rows.indices && x + column in rows[0].indices) rows[y + row][x + column] = true
            }
        }
    }

    private fun mirror(rows: List<BooleanArray>) = rows.map { it.reversedArray() }.toMutableList()
    private fun upsideDown(rows: List<BooleanArray>) = rows.reversed().map { it.copyOf() }.toMutableList()
    private fun turned(rows: List<BooleanArray>) = mirror(upsideDown(rows))

    /** A slice of columns. */
    private fun columns(rows: List<BooleanArray>, first: Int, last: Int? = null) = rows.map { row ->
        val end = minOf(last ?: row.size, row.size)
        row.copyOfRange(minOf(first, end), end)
    }.toMutableList()

    private fun beside(vararg parts: List<BooleanArray>): MutableList<BooleanArray> {
        val height = parts.maxOf { it.size }
        return MutableList(height) { row ->
            parts.map { part -> if (row < part.size) part[row] else BooleanArray(part[0].size) }
                .fold(BooleanArray(0)) { line, piece -> line + piece }
        }
    }

    /** Python's round(): half to even. */
    private fun pyRound(value: Double): Int = Math.rint(value).toInt()

    /** Shortens a glyph by taking rows out of its longest runs of equal rows: stems get
     * shorter, bars and corners stay. Without such runs, rows are dropped evenly. */
    private fun squash(source: List<BooleanArray>, height: Int): MutableList<BooleanArray> {
        val rows = copy(source)
        while (rows.size > height) {
            var bestStart = -1
            var bestLength = 1
            var start = 0
            for (index in 1..rows.size) {
                if (index == rows.size || !rows[index].contentEquals(rows[start])) {
                    // The lower of two equal runs: the upper half of a letter keeps its room.
                    if (index - start >= bestLength && index - start >= 2) {
                        bestStart = start
                        bestLength = index - start
                    }
                    start = index
                }
            }
            if (bestStart < 0) {
                return MutableList(height) { i -> rows[pyRound(i * (rows.size - 1).toDouble() / (height - 1))] }
            }
            rows.removeAt(bestStart + bestLength / 2)
        }
        return rows
    }

    // ---- the face ----

    private fun latin(character: Char): Pair<MutableList<BooleanArray>, Int> {
        val got = render(character.code) ?: throw NoSuchElementException(character.toString())
        return copy(got.rows) to got.top
    }

    /** `top`: rows above the baseline; the letter stands on it by default. */
    private fun put(character: Char, rows: List<BooleanArray>, top: Int? = null) {
        val width = rows[0].size
        val reference = when (character) {
            'б' -> 'b'
            'Ђ', 'Ћ', 'ђ', 'ћ' -> 'h'
            'з', 'э', 'є', 'ө', 'ә' -> 'c'
            'Э', 'Є', 'Ө', 'Ә', 'З' -> 'C'
            'в', 'ь', 'ъ', 'ы', 'ю', 'ф' -> 'o'
            'к', 'ќ', 'қ' -> 'k'
            'г', 'ґ', 'ѓ' -> 'L'
            'т' -> 'T'
            'м', 'ж', 'ш', 'щ' -> 'm'
            'н', 'п', 'и', 'й', 'ѝ', 'л', 'д', 'ч', 'ц', 'я', 'љ', 'њ', 'џ' -> 'n'
            else -> 'H'
        }
        val spacing = render(reference.code)!!
        val left = spacing.left
        val right = spacing.advance - spacing.left - spacing.width
        out[character.code] = FontFace.Glyph(width, rows.size, left, top ?: rows.size, left + width + right, rows)
    }

    private fun put(character: Char, letter: Pair<List<BooleanArray>, Int>) = put(character, letter.first, letter.second)

    private fun same(character: Char, latin: Char) {
        render(latin.code)?.let { out[character.code] = it }
    }

    // ---- parts ----

    private fun twoStems(height: Int, width: Int = this.width): MutableList<BooleanArray> {
        val rows = canvas(width, height)
        fill(rows, 0, 0, stem, height)
        fill(rows, width - stem, 0, stem, height)
        return rows
    }

    /** `base` with what `withAccent` has over `without`. */
    private fun accent(base: List<BooleanArray>, withAccent: Char, without: Char): Pair<MutableList<BooleanArray>, Int> {
        val (marked, markedTop) = latin(withAccent)
        val plainTop = latin(without).second
        val extra = markedTop - plainTop
        if (extra <= 0) return copy(base) to base.size
        val paddedMark = marked.subList(0, minOf(extra, marked.size))
        val inkColumns = paddedMark.flatMap { row -> row.indices.filter { row[it] } }
        if (inkColumns.isEmpty()) return copy(base) to base.size
        val mark = columns(paddedMark, inkColumns.min(), inkColumns.max() + 1)
        val width = maxOf(base[0].size, mark[0].size)
        val rows = canvas(width, extra + base.size)
        blit(rows, mark, (width - mark[0].size) / 2, 0)
        blit(rows, base, (width - base[0].size) / 2, extra)
        return rows to rows.size
    }

    /** The descending tick of Ц and Щ at the right. */
    private fun tail(rows: List<BooleanArray>): Pair<MutableList<BooleanArray>, Int> {
        val over = metrics.overhang
        val depth = metrics.tailDepth
        val width = rows[0].size + over
        val out = canvas(width, rows.size + depth)
        blit(out, rows, 0, 0)
        fill(out, 0, rows.size - bar(rows.size), width, bar(rows.size))
        fill(out, width - stem, rows.size, stem, depth)
        return out to rows.size
    }

    /** A descending tick at the right foot of a letter: Қ, Ң. */
    private fun tick(rows: List<BooleanArray>): Pair<MutableList<BooleanArray>, Int> {
        val over = metrics.overhang
        val depth = metrics.tailDepth
        val out = canvas(rows[0].size + over, rows.size + depth)
        blit(out, rows, 0, 0)
        fill(out, rows[0].size - stem, rows.size - bar(rows.size), stem + over, bar(rows.size))
        fill(out, out[0].size - stem, rows.size, stem, depth)
        return out to rows.size
    }

    /** A tick under the middle: Џ. */
    private fun tickUnder(rows: List<BooleanArray>): Pair<MutableList<BooleanArray>, Int> {
        val depth = metrics.tailDepth
        val out = canvas(rows[0].size, rows.size + depth)
        blit(out, rows, 0, 0)
        fill(out, (rows[0].size - stem) / 2, rows.size, stem, depth)
        return out to rows.size
    }

    /** A letter with the bowl of Ь on its right stem: Љ, Њ. */
    private fun withBowl(rows: List<BooleanArray>, soft: List<BooleanArray>) = beside(rows, columns(soft, stem))

    /** A bar across a stem, standing out `over` on both sides. */
    private fun stroke(rows: List<BooleanArray>, x: Int, width: Int, row: Int? = null): MutableList<BooleanArray> {
        val over = maxOf(1, stem - 1)
        val shift = if (x == 0) over else 0
        val out = canvas(rows[0].size + shift, rows.size)
        blit(out, rows, shift, 0)
        fill(out, x + shift - over, row ?: ((rows.size - metrics.crossbar) / 2), width + 2 * over, metrics.crossbar)
        return out
    }

    /** Ћ: h under a bar; Ђ: its leg goes on under the line and turns in. */
    private fun tshe(withHook: Boolean): Pair<MutableList<BooleanArray>, Int> {
        val over = maxOf(2, stem)
        val body = latin('h').first
        val depth = if (withHook) maxOf(2, stem - 1) else 0
        val rows = canvas(body[0].size + over, body.size + depth)
        blit(rows, body, over, 0)
        fill(rows, 0, 0, over + stem + (body[0].size - stem) / 2, stem)
        if (withHook) {
            fill(rows, rows[0].size - stem, body.size, stem, depth)
            fill(rows, rows[0].size - stem - maxOf(1, stem - 1), body.size + depth - maxOf(1, stem - 1), stem, maxOf(1, stem - 1))
        }
        return rows to body.size
    }

    /** Ә: a round letter open at the upper left, a bar across its middle. */
    private fun schwa(source: List<BooleanArray>): MutableList<BooleanArray> {
        val rows = copy(source)
        val height = rows.size
        val width = rows[0].size
        val middle = (height - stem) / 2
        fill(rows, 0, stem + (if (stem > 2) 1 else 0), stem + 1, middle - stem, ink = false)
        fill(rows, 0, middle, width, stem)
        return rows
    }

    private fun barredO(source: List<BooleanArray>): MutableList<BooleanArray> {
        val rows = copy(source)
        fill(rows, stem, (rows.size - metrics.crossbar) / 2, rows[0].size - 2 * stem, metrics.crossbar)
        return rows
    }

    private fun numero(capitalN: List<BooleanArray>, smallO: List<BooleanArray>): MutableList<BooleanArray> {
        val gap = maxOf(1, stem - 1)
        val height = capitalN.size
        val rows = canvas(capitalN[0].size + gap + smallO[0].size, height)
        blit(rows, capitalN, 0, 0)
        val leftAt = capitalN[0].size + gap
        val ring = squash(smallO, maxOf(smallO.size - stem, 2 * stem + 1))
        blit(rows, ring, leftAt, 0)
        fill(rows, leftAt, height - maxOf(1, stem - 1), smallO[0].size, maxOf(1, stem - 1))
        return rows
    }

    // ---- letters assembled from approved parts ----

    private fun pe(height: Int) = twoStems(height, bodyWidth(height)).also {
        fill(it, 0, 0, it[0].size, bar(height))
    }

    private fun en(height: Int) = twoStems(height, bodyWidth(height)).also {
        fill(it, 0, (height - bar(height)) / 2, it[0].size, bar(height))
    }

    private fun tseBody(height: Int) = twoStems(height, bodyWidth(height)).also {
        fill(it, 0, height - bar(height), it[0].size, bar(height))
    }

    private fun sha(height: Int): MutableList<BooleanArray> {
        val capitalWidth = maxOf(render('M'.code)!!.width, render('W'.code)!!.width)
        val gap = if (height == xHeight) (metrics.wide - 3 * stem) / 2 else (capitalWidth - 3 * stem + 1) / 2
        val shaWidth = 3 * stem + 2 * gap
        val rows = twoStems(height, shaWidth)
        fill(rows, (shaWidth - stem) / 2, 0, stem, height)
        fill(rows, 0, height - bar(height), shaWidth, bar(height))
        return rows
    }

    private fun el(height: Int): MutableList<BooleanArray> {
        if (height == xHeight) return master('л')
        val slope = when (cap) { 10 -> 1; 15 -> 2; else -> 3 }
        val rows = canvas(width, height)
        fill(rows, width - stem, 0, stem, height)
        fill(rows, slope, 0, width - slope, stem)
        for (row in stem until height) {
            val offset = slope * (height - 1 - row) / (height - stem - 1)
            fill(rows, offset, row, stem, 1)
        }
        return rows
    }

    private fun de(height: Int): Pair<MutableList<BooleanArray>, Int> {
        if (height == xHeight) return master('д') to height
        val over = metrics.overhang
        val depth = metrics.tailDepth
        val body = el(height)
        val rows = canvas(width + 2 * over, height + depth)
        blit(rows, body, over, 0)
        fill(rows, 0, height - stem, rows[0].size, stem)
        fill(rows, 0, height, stem, depth)
        fill(rows, rows[0].size - stem, height, stem, depth)
        return rows to height
    }

    /** The lower bowl of B on a stem: Ь, and the body of Б, Ъ, Ы. */
    private fun softSign(height: Int, source: List<BooleanArray>): MutableList<BooleanArray> {
        val bowl = copy(source)
        val bowlWidth = bowl[0].size
        val middle = (height - stem) / 2
        val rows = canvas(bowlWidth, height)
        fill(rows, 0, 0, stem, height)
        val lower = bowl.subList(bowl.size - (height - middle), bowl.size).map { it.copyOf() }.toMutableList()
        // The bowl begins as it ends: its last rows, upside down.
        for (index in 0 until stem) lower[index] = lower[lower.size - 1 - index].copyOf()
        blit(rows, lower, 0, middle)
        return rows
    }

    /** A vertical entry and a rounded shoulder; the right stem continues below the join. */
    private fun che(height: Int): MutableList<BooleanArray> {
        if (height == xHeight) return master('ч')
        val rows = canvas(width, height)
        val joinTop = height / 2 - metrics.roundCap / 2
        fill(rows, 0, 0, stem, joinTop)
        fill(rows, width - stem, 0, stem, height)
        for (row in 0 until metrics.roundCap) {
            val inset = minOf(row + 1, stem - 1)
            fill(rows, inset, joinTop + row, width - inset, 1)
        }
        return rows
    }

    private fun zhe(arms: List<BooleanArray>) = beside(mirror(arms), twoStems(arms.size, stem), arms)

    private fun eReversed(source: List<BooleanArray>): MutableList<BooleanArray> {
        val rows = mirror(source)
        val height = rows.size
        val rowWidth = rows[0].size
        val weight = bar(height)
        val start = if (height == xHeight) maxOf(1, stem / 2) else stem
        val middle = if (height == xHeight) render('e'.code)!!.rows.indexOfFirst { row -> row.all { it } } else (height - weight) / 2
        fill(rows, start, middle, rowWidth - start, weight)
        return rows
    }

    /** A ring with a stem through it; the stem runs from `stemTop` rows over the ring to
     * `stemBottom` under it. */
    private fun ef(height: Int, source: List<BooleanArray>, stemTop: Int, stemBottom: Int): Pair<MutableList<BooleanArray>, Int> {
        val gap = maxOf(2, stem)
        val efWidth = 3 * stem + 2 * gap
        val squashed = squash(source, height)
        val half = squashed[0].size / 2
        val ring = squashed.map { row ->
            val filler = BooleanArray(maxOf(0, efWidth - squashed[0].size)) { row[half] }
            row.copyOfRange(0, half) + filler + row.copyOfRange(half, row.size)
        }
        val rows = canvas(efWidth, stemTop + height + stemBottom)
        blit(rows, ring, 0, stemTop)
        fill(rows, (efWidth - stem) / 2, 0, stem, rows.size)
        return rows to stemTop + height
    }

    private fun yu(source: List<BooleanArray>): MutableList<BooleanArray> {
        val gap = maxOf(2, stem)
        val height = source.size
        val rows = canvas(stem + gap + source[0].size, height)
        fill(rows, 0, 0, stem, height)
        blit(rows, source, stem + gap, 0)
        val weight = bar(height)
        fill(rows, stem, (height - weight) / 2, gap + 1, weight)
        return rows
    }

    /** The gap stays smaller than the separation of two glyphs. */
    private fun yeru(soft: List<BooleanArray>): MutableList<BooleanArray> {
        val gap = metrics.internalGap
        return beside(soft, canvas(gap, soft.size), twoStems(soft.size, stem))
    }

    private fun hardSign(soft: List<BooleanArray>): MutableList<BooleanArray> {
        val over = stem
        val rows = canvas(soft[0].size + over, soft.size)
        blit(rows, soft, over, 0)
        fill(rows, 0, 0, over + stem, bar(soft.size))
        return rows
    }

    private fun be(soft: List<BooleanArray>) = copy(soft).also { fill(it, 0, 0, it[0].size - 1, stem) }

    private fun gheUpturn(ghe: List<BooleanArray>): MutableList<BooleanArray> {
        val rise = maxOf(1, stem - 1)
        val rows = canvas(ghe[0].size, ghe.size + rise)
        blit(rows, ghe, 0, rise)
        fill(rows, ghe[0].size - stem, 0, stem, rise)
        return rows
    }

    private fun ieUkrainian(source: List<BooleanArray>): MutableList<BooleanArray> {
        val rows = copy(source)
        val weight = bar(rows.size)
        val middle = if (rows.size == xHeight) render('e'.code)!!.rows.indexOfFirst { row -> row.all { it } } else (rows.size - weight) / 2
        fill(rows, stem, middle, rows[0].size - stem, weight)
        return rows
    }


    // These are native-pixel drawings, not scaled copies of another Jersey face.
    private fun drawings(): Map<Char, String> = when (cap) {
        10 -> mapOf(
            'б' to """
                .######
                ##.....
                ######.
                #######
                ##...##
                ##...##
                ##...##
                ##...##
                #######
                .#####.
            """,
            'в' to """
                #####..
                ##..##.
                ##..##.
                ######.
                ##...##
                ##...##
                ######.
                #####..
            """,
            'з' to """
                .####..
                ##..##.
                ....##.
                ..###..
                .....##
                ##...##
                .######
                ..####.
            """,
            'и' to """
                ##...##
                ##...##
                ##..###
                ##.####
                ####.##
                ###..##
                ##...##
                ##...##
            """,
            'ж' to """
                ##..##..##
                ##..##..##
                .##.##.##.
                ..######..
                .##.##.##.
                ##..##..##
                ##..##..##
                ##..##..##
            """,
            'к' to """
                ##..##
                ##..##
                ##.##.
                ####..
                ##.##.
                ##..##
                ##..##
                ##..##
            """,
            'л' to """
                ..#####
                ..#####
                .##..##
                .##..##
                ##...##
                ##...##
                ##...##
                ##...##
            """,
            'д' to """
                ...#####.
                ...#####.
                ..##..##.
                ..##..##.
                .##...##.
                .##...##.
                #########
                #########
                ##.....##
                ##.....##
            """,
            'ч' to """
                ##...##
                ##...##
                ###..##
                .######
                ..#####
                .....##
                .....##
                .....##
            """,
            'ь' to """
                ##.....
                ##.....
                #####..
                ######.
                ##...##
                ##...##
                ######.
                #####..
            """,
            'я' to """
                .######
                ##...##
                ##...##
                .######
                ..#####
                ..##.##
                .##..##
                ##...##
            """,
            'м' to """
                ##......##
                ###....###
                ####..####
                ##.####.##
                ##..##..##
                ##..##..##
                ##......##
                ##......##
            """,
            'У' to """
                ##...##
                ##...##
                ##...##
                ###.###
                .######
                ..####.
                ...##..
                ..##...
                .##....
                ##.....
            """,
            'З' to """
                .####..
                ######.
                ##..##.
                ....##.
                ..###..
                .....##
                .....##
                ##...##
                #######
                .#####.
            """,
        )
        15 -> mapOf(
            'б' to """
                ..########
                .#########
                ###.......
                ###.......
                #########.
                ##########
                ###....###
                ###....###
                ###....###
                ###....###
                ###....###
                ###....###
                ##########
                .########.
                ..######..
            """,
            'в' to """
                ########..
                #########.
                ###....###
                ###....###
                #########.
                ########..
                ###....###
                ###....###
                ###....###
                #########.
                ########..
            """,
            'з' to """
                ..#####...
                .#######..
                ###...###.
                ......###.
                ...#####..
                ......###.
                .......###
                ###....###
                ###....###
                .########.
                ..######..
            """,
            'и' to """
                ###....###
                ###...####
                ###...####
                ###..#####
                ###..#####
                ###.######
                ######.###
                ######.###
                #####..###
                #####..###
                ####...###
            """,
            'ж' to """
                ###...###...###
                ###...###...###
                ###...###...###
                .###..###..###.
                ..###.###.###..
                ...#########...
                ..###.###.###..
                .###..###..###.
                ###...###...###
                ###...###...###
                ###...###...###
            """,
            'к' to """
                ###..###
                ###..###
                ###..###
                ###.####
                #######.
                ######..
                #######.
                ###.####
                ###..###
                ###..###
                ###..###
            """,
            'л' to """
                ..########
                ..########
                ..###..###
                ..###..###
                .###...###
                .###...###
                .###...###
                ###....###
                ###....###
                ###....###
                ###....###
            """,
            'д' to """
                ...########.
                ...########.
                ...###..###.
                ...###..###.
                ..###...###.
                ..###...###.
                ..###...###.
                .###....###.
                .###....###.
                ############
                ############
                ###......###
                ###......###
                ###......###
            """,
            'ч' to """
                ###....###
                ###....###
                ###....###
                ####...###
                .#########
                ..########
                .......###
                .......###
                .......###
                .......###
                .......###
            """,
            'ь' to """
                ###.......
                ###.......
                ###.......
                ###.......
                ########..
                #########.
                ###....###
                ###....###
                ###....###
                #########.
                ########..
            """,
            'я' to """
                ..########
                .#########
                ###....###
                ###....###
                .#########
                ..########
                ...###.###
                ..###..###
                .###...###
                ###....###
                ###....###
            """,
            'м' to """
                ###.........###
                ####.......####
                #####.....#####
                ######...######
                ###.###.###.###
                ###..#####..###
                ###...###...###
                ###...###...###
                ###.........###
                ###.........###
                ###.........###
            """,
            'У' to """
                ###....###
                ###....###
                ###....###
                ###....###
                ###....###
                ####..####
                .########.
                ..######..
                ...####...
                ....###...
                ...####...
                ..####....
                .####.....
                ####......
                ###.......
            """,
            'З' to """
                .######...
                .########.
                ##########
                ###....###
                .......###
                ......####
                ...######.
                ...######.
                ......####
                .......###
                .......###
                ###....###
                ##########
                .########.
                ..######..
            """,
        )
        else -> drawings25()
    }


    private fun drawings25(): Map<Char, String> = mapOf(
        'б' to """
            ..##############
            .###############
            ################
            #####...........
            #####...........
            #####...........
            ############....
            ##############..
            ###############.
            ###############.
            ######....######
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            ######....######
            .##############.
            .##############.
            ..############..
            ....########....
        """,
        'в' to """
            ############....
            ##############..
            ###############.
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            ###############.
            ##############..
            ###############.
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            ###############.
            ##############..
            ############....
        """,
        'з' to """
            ...#########....
            .#############..
            ###############.
            #####......#####
            ...........#####
            ...........#####
            ..........######
            .....##########.
            .....#########..
            .....##########.
            ..........######
            ...........#####
            ...........#####
            ...........#####
            #####......#####
            ######....######
            .##############.
            ..############..
            ....########....
        """,
        'и' to """
            #####......#####
            #####......#####
            #####.....######
            #####.....######
            #####....#######
            #####....#######
            #####...########
            #####...########
            #####..#########
            #####.##########
            ##########.#####
            #########..#####
            #########..#####
            ########...#####
            ########...#####
            #######....#####
            #######....#####
            ######.....#####
            ######.....#####
        """,
        'ж' to """
            #####....#####....#####
            #####....#####....#####
            #####....#####....#####
            #####....#####....#####
            .#####...#####...#####.
            .#####...#####...#####.
            ..#####..#####..#####..
            ...#####.#####.#####...
            ...#####.#####.#####...
            .....#############.....
            ...#####.#####.#####...
            ...#####.#####.#####...
            ..#####..#####..#####..
            .#####...#####...#####.
            .#####...#####...#####.
            #####....#####....#####
            #####....#####....#####
            #####....#####....#####
            #####....#####....#####
        """,
        'к' to """
            #####....#####
            #####....#####
            #####....#####
            #####....#####
            #####...######
            #####..######.
            ############..
            ###########...
            ##########....
            ###########...
            ############..
            #####..######.
            #####...######
            #####....#####
            #####....#####
            #####....#####
            #####....#####
            #####....#####
            #####....#####
        """,
        'л' to """
            ...#############
            ...#############
            ...#############
            ...#####...#####
            ...#####...#####
            ...#####...#####
            ..#####....#####
            ..#####....#####
            ..#####....#####
            ..#####....#####
            .#####.....#####
            .#####.....#####
            .#####.....#####
            .#####.....#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
        """,
        'д' to """
            .....#############..
            .....#############..
            .....#############..
            .....#####...#####..
            .....#####...#####..
            .....#####...#####..
            ....#####....#####..
            ....#####....#####..
            ....#####....#####..
            ....#####....#####..
            ...#####.....#####..
            ...#####.....#####..
            ...#####.....#####..
            ...#####.....#####..
            ..#####......#####..
            ..#####......#####..
            ####################
            ####################
            ####################
            #####..........#####
            #####..........#####
            #####..........#####
            #####..........#####
        """,
        'ч' to """
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            ######.....#####
            .###############
            ..##############
            ...#############
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
        """,
        'ь' to """
            #####...........
            #####...........
            #####...........
            #####...........
            #####...........
            #####...........
            #####...........
            ############....
            ##############..
            ###############.
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            ###############.
            ##############..
            ############....
        """,
        'я' to """
            ....############
            ..##############
            .###############
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            #####......#####
            .###############
            ..##############
            ....############
            .....#####.#####
            .....#####.#####
            ....#####..#####
            ...#####...#####
            ..#####....#####
            .#####.....#####
            #####......#####
            #####......#####
        """,
        'м' to """
            #####.............#####
            #####.............#####
            ######...........######
            ######...........######
            #######.........#######
            ########.......########
            #########.....#########
            #####.#####.#####.#####
            #####..#########..#####
            #####...#######...#####
            #####....#####....#####
            #####....#####....#####
            #####.............#####
            #####.............#####
            #####.............#####
            #####.............#####
            #####.............#####
            #####.............#####
            #####.............#####
        """,
        'У' to """
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            #####.......#####
            ######.....######
            .######...######.
            ..######.######..
            ...###########...
            ....#########....
            .....#######.....
            ......#####......
            .....######......
            ....######.......
            ...######........
            ..######.........
            .######..........
            ######...........
            #####............
        """,
        'З' to """
            ...#########....
            .############...
            ##############..
            ###############.
            ######....######
            #####......#####
            ...........#####
            ...........#####
            ..........######
            .........######.
            .....#########..
            .....########...
            .....#########..
            ..........######
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            ...........#####
            #####......#####
            ######....######
            .##############.
            ..############..
            ....########....
        """,
    )

    /** The letters made, by code point. */
    fun glyphs(): Map<Int, FontFace.Glyph> {
        val small = xHeight
        for ((cyrillic, source) in "АВЕКМНОРСТХЁІЇЈЅаеорсухёіїјѕ".zip("ABEKMHOPCTXËIÏJSaeopcyxëiïjs")) same(cyrillic, source)

        val capital = "BCHKLMNORTU".associateWith { latin(it).first }
        val lower = mutableMapOf('H' to en(small), 'K' to master('к'))
        val gheSmall = canvas(latin('L').first[0].size, small)
        fill(gheSmall, 0, 0, stem, small)
        fill(gheSmall, 0, 0, gheSmall[0].size, metrics.crossbar)
        lower['L'] = upsideDown(gheSmall)
        val teSmall = canvas(latin('T').first[0].size, small)
        fill(teSmall, (teSmall[0].size - stem) / 2, 0, stem, small)
        fill(teSmall, 0, 0, teSmall[0].size, metrics.crossbar)
        lower['T'] = teSmall
        val oSmall = latin('o').first
        val cSmall = latin('c').first

        for ((isSmall, height, source) in listOf(Triple(false, cap, capital), Triple(true, small, lower))) {
            fun letter(pair: String, rows: List<BooleanArray>, top: Int? = null) = put(if (isSmall) pair[1] else pair[0], rows, top)
            fun letter(pair: String, made: Pair<List<BooleanArray>, Int>) = letter(pair, made.first, made.second)

            val ghe = upsideDown(source.getValue('L'))
            letter("Гг", ghe)
            letter("Ґґ", gheUpturn(ghe))
            letter("Зз", master(if (isSmall) 'з' else 'З'))
            val iCyrillic = if (isSmall) master('и') else mirror(source.getValue('N'))
            letter("Ии", iCyrillic)
            letter("Йй", accent(iCyrillic, if (isSmall) 'ŭ' else 'Ŭ', if (isSmall) 'u' else 'U'))
            letter("Лл", el(height))
            letter("Пп", pe(height))
            letter("Шш", sha(height))
            letter("Цц", tail(tseBody(height)))
            letter("Щщ", tail(sha(height)))
            letter("Дд", de(height))
            letter("Чч", che(height))
            letter("Яя", if (isSmall) master('я') else mirror(source.getValue('R')))
            letter("Жж", if (isSmall) master('ж') else zhe(columns(source.getValue('K'), stem)))
            val soft = if (isSmall) master('ь') else softSign(height, source.getValue('B'))
            letter("Ьь", soft)
            letter("Ыы", yeru(soft))
            letter("Ъъ", hardSign(soft))
            val roundC = if (isSmall) cSmall else source.getValue('C')
            val roundO = if (isSmall) oSmall else source.getValue('O')
            letter("Ээ", eReversed(roundC))
            letter("Єє", ieUkrainian(roundC))
            letter("Юю", yu(roundO))
            // Serbian and Macedonian
            val acute = if (isSmall) 'é' to 'e' else 'É' to 'E'
            val grave = if (isSmall) 'è' to 'e' else 'È' to 'E'
            letter("Ѓѓ", accent(ghe, acute.first, acute.second))
            letter("Ќќ", accent(source.getValue('K'), acute.first, acute.second))
            letter("Ѝѝ", accent(iCyrillic, grave.first, grave.second))
            letter("Љљ", withBowl(el(height), soft))
            letter("Њњ", withBowl(source.getValue('H'), soft))
            letter("Џџ", tickUnder(tseBody(height)))
            // Kazakh
            letter("Ғғ", stroke(ghe, 0, stem))
            letter("Ққ", tick(source.getValue('K')))
            letter("Ңң", tick(source.getValue('H')))
            letter("Өө", barredO(roundO))
            if (!isSmall) {
                put('Б', be(soft))
                same('Ѐ', 'È')
                put('Ћ', tshe(false))
                put('Ђ', tshe(true))
                same('Һ', 'h')
                same('Ү', 'Y')
                val yCapital = latin('Y').first
                val stemAt = (yCapital[0].size - stem) / 2
                put('Ұ', stroke(yCapital, stemAt, stem, cap - 2 * stem))
                put('Ә', schwa(source.getValue('O')))
                put('№', numero(source.getValue('N'), oSmall))
                put('Ф', ef(cap - 2 * (stem - 1), source.getValue('O'), stem - 1, stem - 1).first)
                put('У', master('У'))
                put('Ў', accent(master('У'), 'Ŭ', 'U'))
            } else {
                put('в', master('в'))
                same('ѐ', 'è')
                same('һ', 'h')
                val barred = render(0x0127) // h with a stroke
                if (barred != null) {
                    out['ћ'.code] = barred
                    val (hooked, hookedTop) = tshe(true)
                    put('ђ', hooked, hookedTop)
                } else {
                    put('ћ', tshe(false))
                    put('ђ', tshe(true))
                }
                // A small Y that goes under the line, as the Latin y does.
                val tall = squash(latin('Y').first, small + descent)
                put('ү', tall, small)
                val stemAt = (tall[0].size - stem) / 2
                put('ұ', stroke(tall, stemAt, stem, tall.size - 2 * stem), small)
                put('ә', turned(latin('e').first))
                put('к', source.getValue('K'))
                put('м', master('м'))
                put('н', source.getValue('H'))
                put('т', source.getValue('T'))
                put('б', master('б'))
                put('ф', ef(small, oSmall, cap - small, descent))
                val (ySmall, yTop) = latin('y')
                val marked = accent(ySmall, 'ŭ', 'u').first
                put('ў', marked, yTop + marked.size - ySmall.size)
            }
        }
        for ((code, glyph) in out) checkCounters(code.toChar(), glyph.rows)
        return out
    }
}
