# SPDX-License-Identifier: MIT
"""Cyrillic letters for the Jersey faces, which have none.

Jersey (SIL OFL 1.1, no reserved font name) is one construction at four sizes:
stems of 2, 3, 4 and 5 pixels for capitals of 10, 15, 20 and 25. A Cyrillic
letter is made here from the Latin glyphs of the same face - copied, mirrored,
cut and joined - and from stems and bars of the face's own weight, so one
description serves all four faces.

    glyphs(render) -> {code point: (width, height, left, top, advance, rows)}

`render(code point)` is fontgen.render for the face: the same tuple, or None.
Covers Russian, Ukrainian and Belarusian.
"""


def _copy(rows):
    return [list(row) for row in rows]


def _canvas(width, height):
    return [[False] * width for _ in range(height)]


def _fill(rows, x, y, width, height, ink=True):
    for row in range(max(y, 0), min(y + height, len(rows))):
        for column in range(max(x, 0), min(x + width, len(rows[row]))):
            rows[row][column] = ink


def _blit(rows, source, x, y):
    for row, line in enumerate(source):
        for column, ink in enumerate(line):
            if ink and 0 <= y + row < len(rows) and 0 <= x + column < len(rows[0]):
                rows[y + row][x + column] = True


def _mirror(rows):
    return [list(reversed(row)) for row in rows]


def _upside_down(rows):
    return [list(row) for row in reversed(rows)]


def _turned(rows):
    return _mirror(_upside_down(rows))


def _columns(rows, first, last=None):
    return [row[first:last] for row in rows]


def _beside(*parts):
    height = max(len(part) for part in parts)
    out = []
    for row in range(height):
        line = []
        for part in parts:
            line += part[row] if row < len(part) else [False] * len(part[0])
        out.append(line)
    return out


def _squash(rows, height):
    """Shortens a glyph by taking rows out of its longest runs of equal rows:
    stems get shorter, bars and corners stay. Without such runs, rows are
    dropped evenly."""
    rows = _copy(rows)
    while len(rows) > height:
        best_start, best_length, start = -1, 1, 0
        for index in range(1, len(rows) + 1):
            if index == len(rows) or rows[index] != rows[start]:
                # The lower of two equal runs: the upper half of a letter keeps its room.
                if index - start >= best_length and index - start >= 2:
                    best_start, best_length = start, index - start
                start = index
        if best_start < 0:
            keep = [round(i * (len(rows) - 1) / (height - 1)) for i in range(height)]
            return [rows[i] for i in keep]
        del rows[best_start + best_length // 2]
    return rows


def _resample(rows, height):
    """Nearest rows: for letters whose strokes run the whole height at a slant."""
    keep = [round(i * (len(rows) - 1) / (height - 1)) for i in range(height)]
    return [list(rows[i]) for i in keep]


def _thin_bowls(rows, height, stem):
    """Shortens a letter of bars and bowls (B, 3) to `height`: bars stay,
    bowls lose rows from their edges."""
    width = len(rows[0])
    probe = width - stem - 1 if width > 2 * stem + 1 else width // 2
    probe = min(max(stem + (width - 2 * stem) // 2, 0), width - 1)
    rows = _copy(rows)
    while len(rows) > height:
        runs, start = [], None
        for index, row in enumerate(rows + [None]):
            is_bowl = row is not None and not row[probe]
            if is_bowl and start is None:
                start = index
            elif not is_bowl and start is not None:
                runs.append((start, index - start))
                start = None
        runs = [run for run in runs if run[1] > 1]
        if not runs:
            return _squash(rows, height)
        first, length = max(runs, key=lambda run: (run[1], run[0]))
        del rows[first]
        if len(rows) > height and length > 2:
            del rows[first + length - 2]
    return rows


class _Face:
    def __init__(self, render):
        self.render = render
        h = render(ord("H"))
        self.cap = h[1]
        self.left = h[2]
        self.right = h[4] - h[0] - h[2]
        self.width = h[0]
        self.x_height = render(ord("x"))[1]
        self.descent = render(ord("p"))[1] - self.x_height
        row = h[5][0]
        self.stem = row.index(False)
        self.out = {}

    def latin(self, character):
        got = self.render(ord(character))
        if got is None:
            raise KeyError(character)
        return _copy(got[5]), got[3]

    def put(self, character, rows, top=None):
        """`top`: rows above the baseline; the letter stands on it by default."""
        if top is None:
            top = len(rows)
        width = len(rows[0])
        self.out[ord(character)] = (width, len(rows), self.left, top,
                                    self.left + width + self.right, rows)

    def same(self, character, latin):
        got = self.render(ord(latin))
        if got is not None:
            self.out[ord(character)] = got

    # ---- parts ----

    def two_stems(self, height, width=None):
        width = width or self.width
        rows = _canvas(width, height)
        _fill(rows, 0, 0, self.stem, height)
        _fill(rows, width - self.stem, 0, self.stem, height)
        return rows

    def accent(self, base, latin_with_accent, latin_without):
        """`base` with what `latin_with_accent` has over `latin_without`."""
        marked, marked_top = self.latin(latin_with_accent)
        plain, plain_top = self.latin(latin_without)
        extra = marked_top - plain_top
        if extra <= 0:
            return base, len(base)
        mark = marked[:extra]
        width = max(len(base[0]), len(mark[0]))
        rows = _canvas(width, extra + len(base))
        _blit(rows, mark, (width - len(mark[0])) // 2, 0)
        _blit(rows, base, (width - len(base[0])) // 2, extra)
        return rows, len(rows)

    def tail(self, rows):
        """The descending tick of Ц and Щ at the right."""
        stem = self.stem
        over = max(1, stem - 1)
        depth = max(2, stem - 1)
        width = len(rows[0]) + over
        out = _canvas(width, len(rows) + depth)
        _blit(out, rows, 0, 0)
        _fill(out, 0, len(rows) - stem, width, stem)
        _fill(out, width - stem, len(rows), stem, depth)
        return out, len(rows)

    # ---- letters, at a height: capitals and small ones are built alike ----

    def pe(self, height):
        rows = self.two_stems(height)
        _fill(rows, 0, 0, self.width, self.stem)
        return rows

    def tse_body(self, height):
        rows = self.two_stems(height)
        _fill(rows, 0, height - self.stem, self.width, self.stem)
        return rows

    def sha(self, height):
        width = 3 * self.stem + 2 * (self.width - 2 * self.stem)
        rows = self.two_stems(height, width)
        _fill(rows, (width - self.stem) // 2, 0, self.stem, height)
        _fill(rows, 0, height - self.stem, width, self.stem)
        return rows

    def el(self, height, with_foot=True):
        stem = self.stem
        foot = max(1, stem - 1) if with_foot else 0
        rows = _canvas(self.width + foot, height)
        _fill(rows, foot, stem, stem, height - stem)
        _fill(rows, foot + self.width - stem, 0, stem, height)
        for row in range(stem):
            cut = stem - 1 - row
            _fill(rows, foot + cut, row, self.width - cut, 1)
        if with_foot:
            _fill(rows, 0, height - stem, foot, stem)
            if foot >= 2:
                rows[height - 1][0] = False
                rows[height - stem][0] = False
        return rows

    def de(self, height):
        stem = self.stem
        over = max(1, stem - 1)
        depth = max(2, stem - 1)
        body = self.el(height, with_foot=False)
        rows = _canvas(self.width + 2 * over, height + depth)
        _blit(rows, body, over, 0)
        _fill(rows, 0, height - stem, len(rows[0]), stem)
        _fill(rows, 0, height, stem, depth)
        _fill(rows, len(rows[0]) - stem, height, stem, depth)
        return rows, height

    def soft_sign(self, height, source):
        """The lower bowl of B on a stem: Ь, and the body of Б, Ъ, Ы."""
        stem = self.stem
        bowl = _copy(source)
        width = len(bowl[0])
        middle = (height - stem) // 2
        rows = _canvas(width, height)
        _fill(rows, 0, 0, stem, height)
        lower = bowl[len(bowl) - (height - middle):]
        # The bowl begins as it ends: its last rows, upside down.
        for index in range(stem):
            lower[index] = list(lower[len(lower) - 1 - index])
        _blit(rows, lower, 0, middle)
        return rows

    def che(self, height):
        turned, _ = self.latin("h")
        turned = _turned(turned)
        arch = self.x_height
        stem_only = _canvas(len(turned[0]), 1)
        _fill(stem_only, len(turned[0]) - self.stem, 0, self.stem, 1)
        if height >= len(turned):
            lift = (len(turned) - arch) // 2
            return turned[lift:] + [list(stem_only[0]) for _ in range(lift)]
        # A small one: the arch of h, turned, under a shorter pair of stems.
        rows = turned[len(turned) - arch - (height - arch):][:height]
        rows = turned[:arch]
        lift = max(1, (height - self.stem) // 3)
        rows = _squash(rows, height - lift)
        return rows + [list(stem_only[0]) for _ in range(lift)]

    def zhe(self, arms):
        return _beside(_mirror(arms), self.two_stems(len(arms), self.stem), arms)

    def e_reversed(self, source):
        rows = _mirror(source)
        height, width, stem = len(rows), len(rows[0]), self.stem
        _fill(rows, width // 2 - 1, (height - stem) // 2, width - width // 2 + 1 - stem, stem)
        return rows

    def ef(self, height, source, stem_top, stem_bottom):
        """A ring with a stem through it; the stem runs from `stem_top` rows
        over the ring to `stem_bottom` under it."""
        stem = self.stem
        gap = max(2, stem)
        width = 3 * stem + 2 * gap
        ring = _squash(source, height)
        half = len(ring[0]) // 2
        filler = [[row[half]] * (width - len(ring[0])) for row in ring]
        ring = [left + middle + right for left, middle, right in
                zip(_columns(ring, 0, half), filler, _columns(ring, half))]
        rows = _canvas(width, stem_top + height + stem_bottom)
        _blit(rows, ring, 0, stem_top)
        _fill(rows, (width - stem) // 2, 0, stem, len(rows))
        return rows, stem_top + height

    def yu(self, source):
        stem = self.stem
        gap = max(2, stem - 1)
        height = len(source)
        rows = _canvas(stem + gap + len(source[0]), height)
        _fill(rows, 0, 0, stem, height)
        _blit(rows, source, stem + gap, 0)
        _fill(rows, stem, (height - stem) // 2, gap + 1, stem)
        return rows

    def yeru(self, soft):
        stem = self.stem
        gap = max(2, stem - 1)
        return _beside(soft, _canvas(gap, len(soft)), self.two_stems(len(soft), stem))

    def hard_sign(self, soft):
        stem = self.stem
        over = stem
        rows = _canvas(len(soft[0]) + over, len(soft))
        _blit(rows, soft, over, 0)
        _fill(rows, 0, 0, over + stem, stem)
        return rows

    def be(self, soft):
        rows = _copy(soft)
        _fill(rows, 0, 0, len(rows[0]) - 1, self.stem)
        return rows

    def ghe_upturn(self, ghe):
        stem = self.stem
        rise = max(1, stem - 1)
        rows = _canvas(len(ghe[0]), len(ghe) + rise)
        _blit(rows, ghe, 0, rise)
        _fill(rows, len(ghe[0]) - stem, 0, stem, rise)
        return rows

    def ie_ukrainian(self, source):
        rows = _copy(source)
        height, stem = len(rows), self.stem
        _fill(rows, stem, (height - stem) // 2, len(rows[0]) // 2, stem)
        return rows


def glyphs(render):
    face = _Face(render)
    cap, small, stem = face.cap, face.x_height, face.stem
    put, same, latin = face.put, face.same, face.latin

    for cyrillic, source in zip("АВЕКМНОРСТХЁІЇЈЅаеорсухёіїјѕ", "ABEKMHOPCTXËIÏJSaeopcyxëiïjs"):
        same(cyrillic, source)

    capital = {name: latin(name)[0] for name in "BCHKLMNORTU3"}
    # Small letters are the capitals at the height of x: bars and corners stay.
    lower = {name: _squash(rows, small) for name, rows in capital.items()}
    lower["B"] = _thin_bowls(capital["B"], small, stem)
    lower["3"] = _thin_bowls(capital["3"], small, stem)
    lower["N"] = _resample(capital["N"], small)
    k_small, _ = latin("k")
    lower["K"] = k_small[len(k_small) - small:]
    o_small, _ = latin("o")
    c_small, _ = latin("c")

    for is_small, height, source in ((False, cap, capital), (True, small, lower)):
        def letter(pair, rows, top=None, is_small=is_small):
            put(pair[1] if is_small else pair[0], rows, top)

        ghe = _upside_down(source["L"])
        letter("Гг", ghe)
        letter("Ґґ", face.ghe_upturn(ghe))
        letter("Зз", source["3"])
        i_cyrillic = _mirror(source["N"])
        letter("Ии", i_cyrillic)
        letter("Йй", *face.accent(i_cyrillic, "ŭ" if is_small else "Ŭ", "u" if is_small else "U"))
        letter("Лл", face.el(height))
        letter("Пп", face.pe(height))
        letter("Шш", face.sha(height))
        letter("Цц", *face.tail(face.tse_body(height)))
        letter("Щщ", *face.tail(face.sha(height)))
        letter("Дд", *face.de(height))
        letter("Чч", face.che(height))
        letter("Яя", _mirror(source["R"]))
        arms = _columns(source["K"], stem)
        letter("Жж", face.zhe(arms))
        soft = face.soft_sign(height, source["B"])
        letter("Ьь", soft)
        letter("Ыы", face.yeru(soft))
        letter("Ъъ", face.hard_sign(soft))
        round_c = c_small if is_small else source["C"]
        round_o = o_small if is_small else source["O"]
        letter("Ээ", face.e_reversed(round_c))
        letter("Єє", face.ie_ukrainian(round_c))
        letter("Юю", face.yu(round_o))
        if not is_small:
            put("Б", face.be(soft))
            put("Ф", face.ef(cap - 2 * (stem - 1), source["O"], stem - 1, stem - 1)[0])
            put("У", latin("y")[0])
            put("Ў", *face.accent(latin("y")[0], "Ŭ", "U"))
        else:
            put("в", source["B"])
            put("к", source["K"])
            put("м", source["M"])
            put("н", source["H"])
            put("т", source["T"])
            put("б", latin("6")[0])
            put("ф", *face.ef(small, o_small, cap - small, face.descent))
            y_small, y_top = latin("y")
            marked, _ = face.accent(y_small, "ŭ", "u")
            put("ў", marked, y_top + len(marked) - len(y_small))
    return face.out
