// SPDX-License-Identifier: MIT
package org.perinkpheral.cli

/**
 * Cyrillic letters for the Jersey faces, which have none.
 *
 * Jersey (SIL OFL 1.1, no reserved font name) is one construction at four sizes: stems of 2, 3,
 * 4 and 5 pixels for capitals of 10, 15, 20 and 25. A Cyrillic letter is made here from the
 * Latin glyphs of the same face - copied, mirrored, cut and joined - and from stems and bars of
 * the face's own weight, so one description serves all four faces. Covers Russian, Ukrainian,
 * Belarusian, Serbian, Macedonian, Bulgarian and Kazakh.
 */
class JerseyCyrillic(private val render: (Int) -> FontFace.Glyph?) {
    private val h = render('H'.code)!!
    private val cap = h.height
    private val left = h.left
    private val right = h.advance - h.width - h.left
    private val width = h.width
    private val xHeight = render('x'.code)!!.height
    private val descent = render('p'.code)!!.height - xHeight
    private val stem = h.rows[0].indexOfFirst { !it }
    private val out = HashMap<Int, FontFace.Glyph>()

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

    /** Python's row[first:last]. */
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

    /** Nearest rows: for letters whose strokes run the whole height at a slant. */
    private fun resample(rows: List<BooleanArray>, height: Int) =
        MutableList(height) { i -> rows[pyRound(i * (rows.size - 1).toDouble() / (height - 1))].copyOf() }

    /** Shortens a letter of bars and bowls (B, 3) to `height`: bars stay, bowls lose rows from
     * their edges. */
    private fun thinBowls(source: List<BooleanArray>, height: Int): MutableList<BooleanArray> {
        val width = source[0].size
        val probe = minOf(maxOf(stem + (width - 2 * stem) / 2, 0), width - 1)
        val rows = copy(source)
        while (rows.size > height) {
            val runs = ArrayList<IntArray>()
            var start: Int? = null
            for (index in 0..rows.size) {
                val isBowl = index < rows.size && !rows[index][probe]
                if (isBowl && start == null) {
                    start = index
                } else if (!isBowl && start != null) {
                    runs += intArrayOf(start, index - start)
                    start = null
                }
            }
            val long = runs.filter { it[1] > 1 }
            if (long.isEmpty()) return squash(rows, height)
            val (first, length) = long.maxWith(compareBy<IntArray>({ it[1] }, { it[0] }))
            rows.removeAt(first)
            if (rows.size > height && length > 2) rows.removeAt(first + length - 2)
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
        val mark = marked.subList(0, minOf(extra, marked.size))
        val width = maxOf(base[0].size, mark[0].size)
        val rows = canvas(width, extra + base.size)
        blit(rows, mark, (width - mark[0].size) / 2, 0)
        blit(rows, base, (width - base[0].size) / 2, extra)
        return rows to rows.size
    }

    /** The descending tick of Ц and Щ at the right. */
    private fun tail(rows: List<BooleanArray>): Pair<MutableList<BooleanArray>, Int> {
        val over = maxOf(1, stem - 1)
        val depth = maxOf(2, stem - 1)
        val width = rows[0].size + over
        val out = canvas(width, rows.size + depth)
        blit(out, rows, 0, 0)
        fill(out, 0, rows.size - stem, width, stem)
        fill(out, width - stem, rows.size, stem, depth)
        return out to rows.size
    }

    /** A descending tick at the right foot of a letter: Қ, Ң. */
    private fun tick(rows: List<BooleanArray>): Pair<MutableList<BooleanArray>, Int> {
        val over = maxOf(1, stem - 1)
        val depth = maxOf(2, stem - 1)
        val out = canvas(rows[0].size + over, rows.size + depth)
        blit(out, rows, 0, 0)
        fill(out, rows[0].size - stem, rows.size - stem, stem + over, stem)
        fill(out, out[0].size - stem, rows.size, stem, depth)
        return out to rows.size
    }

    /** A tick under the middle: Џ. */
    private fun tickUnder(rows: List<BooleanArray>): Pair<MutableList<BooleanArray>, Int> {
        val depth = maxOf(2, stem - 1)
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
        fill(out, x + shift - over, row ?: ((rows.size - stem) / 2), width + 2 * over, maxOf(1, stem - 1))
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
        fill(rows, stem, (rows.size - maxOf(1, stem - 1)) / 2, rows[0].size - 2 * stem, maxOf(1, stem - 1))
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

    // ---- letters, at a height: capitals and small ones are built alike ----

    private fun pe(height: Int) = twoStems(height).also { fill(it, 0, 0, width, stem) }

    private fun tseBody(height: Int) = twoStems(height).also { fill(it, 0, height - stem, width, stem) }

    private fun sha(height: Int): MutableList<BooleanArray> {
        val shaWidth = 3 * stem + 2 * (width - 2 * stem)
        val rows = twoStems(height, shaWidth)
        fill(rows, (shaWidth - stem) / 2, 0, stem, height)
        fill(rows, 0, height - stem, shaWidth, stem)
        return rows
    }

    private fun el(height: Int, withFoot: Boolean = true): MutableList<BooleanArray> {
        val foot = if (withFoot) maxOf(1, stem - 1) else 0
        val rows = canvas(width + foot, height)
        fill(rows, foot, stem, stem, height - stem)
        fill(rows, foot + width - stem, 0, stem, height)
        for (row in 0 until stem) {
            val cut = stem - 1 - row
            fill(rows, foot + cut, row, width - cut, 1)
        }
        // The left leg flares out at its foot a step a row, the widest at the bottom: a slope
        // in the face's pixels, with no pixel standing alone.
        if (withFoot) {
            for (step in 0 until foot) fill(rows, step, height - 1 - step, foot - step, 1)
        }
        return rows
    }

    private fun de(height: Int): Pair<MutableList<BooleanArray>, Int> {
        val over = maxOf(1, stem - 1)
        val depth = maxOf(2, stem - 1)
        val body = el(height, withFoot = false)
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

    /** The bowl of U on its right stem: the bottom of the bowl runs into the stem whole, so Ч is
     * not taken for a 4. The stem goes on alone for two fifths of the height. */
    private fun che(height: Int, source: List<BooleanArray>): MutableList<BooleanArray> {
        val sourceWidth = source[0].size
        val alone = maxOf(stem + 1, height * 2 / 5)
        val bowl = squash(source, height - alone)
        // Under the counter, the outer corner at the right is filled: there the stem goes on.
        for (row in bowl) {
            val isTwoStems = (0 until stem).all { row[it] } && (stem until sourceWidth - stem).none { row[it] }
            if (!isTwoStems && row.any { it }) {
                val last = row.lastIndexOf(true)
                for (column in last until sourceWidth) row[column] = true
            }
        }
        val tail = canvas(sourceWidth, alone)
        fill(tail, sourceWidth - stem, 0, stem, alone)
        return (bowl + tail).toMutableList()
    }

    private fun zhe(arms: List<BooleanArray>) = beside(mirror(arms), twoStems(arms.size, stem), arms)

    private fun eReversed(source: List<BooleanArray>): MutableList<BooleanArray> {
        val rows = mirror(source)
        val height = rows.size
        val rowWidth = rows[0].size
        fill(rows, rowWidth / 2 - 1, (height - stem) / 2, rowWidth - rowWidth / 2 + 1 - stem, stem)
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
        fill(rows, stem, (height - stem) / 2, gap + 1, stem)
        return rows
    }

    /** Ь and a stem, a pixel closer than the stems of Н: further and they are two letters,
     * nearer and the bowl runs into the stem. */
    private fun yeru(soft: List<BooleanArray>): MutableList<BooleanArray> {
        val gap = maxOf(2, width - 2 * stem - 1)
        return beside(soft, canvas(gap, soft.size), twoStems(soft.size, stem))
    }

    private fun hardSign(soft: List<BooleanArray>): MutableList<BooleanArray> {
        val over = stem
        val rows = canvas(soft[0].size + over, soft.size)
        blit(rows, soft, over, 0)
        fill(rows, 0, 0, over + stem, stem)
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
        fill(rows, stem, (rows.size - stem) / 2, rows[0].size / 2, stem)
        return rows
    }

    /** The letters made, by code point. */
    fun glyphs(): Map<Int, FontFace.Glyph> {
        val small = xHeight
        for ((cyrillic, source) in "АВЕКМНОРСТХЁІЇЈЅаеорсухёіїјѕ".zip("ABEKMHOPCTXËIÏJSaeopcyxëiïjs")) same(cyrillic, source)

        val capital = "BCHKLMNORTU3".associateWith { latin(it).first }
        // Small letters are the capitals at the height of x: bars and corners stay.
        val lower = capital.mapValues { (_, rows) -> squash(rows, small) }.toMutableMap()
        lower['B'] = thinBowls(capital.getValue('B'), small)
        lower['3'] = thinBowls(capital.getValue('3'), small)
        lower['N'] = resample(capital.getValue('N'), small)
        val kSmall = latin('k').first
        lower['K'] = kSmall.subList(kSmall.size - small, kSmall.size).toMutableList()
        val oSmall = latin('o').first
        val cSmall = latin('c').first

        for ((isSmall, height, source) in listOf(Triple(false, cap, capital), Triple(true, small, lower))) {
            fun letter(pair: String, rows: List<BooleanArray>, top: Int? = null) = put(if (isSmall) pair[1] else pair[0], rows, top)
            fun letter(pair: String, made: Pair<List<BooleanArray>, Int>) = letter(pair, made.first, made.second)

            val ghe = upsideDown(source.getValue('L'))
            letter("Гг", ghe)
            letter("Ґґ", gheUpturn(ghe))
            letter("Зз", source.getValue('3'))
            val iCyrillic = mirror(source.getValue('N'))
            letter("Ии", iCyrillic)
            letter("Йй", accent(iCyrillic, if (isSmall) 'ŭ' else 'Ŭ', if (isSmall) 'u' else 'U'))
            letter("Лл", el(height))
            letter("Пп", pe(height))
            letter("Шш", sha(height))
            letter("Цц", tail(tseBody(height)))
            letter("Щщ", tail(sha(height)))
            letter("Дд", de(height))
            letter("Чч", che(height, source.getValue('U')))
            letter("Яя", mirror(source.getValue('R')))
            val arms = columns(source.getValue('K'), stem)
            letter("Жж", zhe(arms))
            val soft = softSign(height, source.getValue('B'))
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
                put('У', latin('y').first)
                put('Ў', accent(latin('y').first, 'Ŭ', 'U'))
            } else {
                put('в', source.getValue('B'))
                same('ѐ', 'è')
                same('һ', 'h')
                val barred = render(0x0127) // h with a stroke
                if (barred != null) {
                    out['ћ'.code] = barred
                    val (hooked, hookedTop) = tshe(true)
                    out['ђ'.code] = FontFace.Glyph(hooked[0].size, hooked.size, left, hookedTop, left + hooked[0].size + right, hooked)
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
                put('м', source.getValue('M'))
                put('н', source.getValue('H'))
                put('т', source.getValue('T'))
                // b with a flag at the top, as far right as its bowl reaches - where a row of
                // the stem stands alone between the flag and the bowl; the smallest face has no
                // room for it and keeps the 6.
                val (bSmall, bTop) = latin('b')
                if (bTop - small > stem) {
                    val flagged = copy(bSmall)
                    fill(flagged, 0, 0, flagged[0].size, stem)
                    put('б', flagged, bTop)
                } else {
                    put('б', latin('6').first)
                }
                put('ф', ef(small, oSmall, cap - small, descent))
                val (ySmall, yTop) = latin('y')
                val marked = accent(ySmall, 'ŭ', 'u').first
                put('ў', marked, yTop + marked.size - ySmall.size)
            }
        }
        return out
    }
}
