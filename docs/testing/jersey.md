# Jersey replacement study

Status: proposal to replace Jersey, recorded during acceptance on 2026-11-18.
Tiny5 Regular is a candidate for a trial, not an approved production replacement.
Cyrillic support is mandatory; candidates without it are excluded.

The owner observed that the Air app's cramped labels and bottom spacing depend
on the selected font. Fix the app's layout for both current fonts; investigate
a replacement for Jersey separately. Do not change the system font as part of
the Air layout correction.

Compare Jersey and possible replacements with Departure Mono as a reference:

- Render the same English and Russian screens, including Air's icons, labels,
  values and classifications. Check capitals, baselines, line spacing and the
  actual visible bounds, not just nominal font sizes.
- Check legibility on the physical 400×300 panel at every supported text size.
  Include digits, punctuation, units and long Cyrillic labels.
- Separate layout mistakes from font-metric or rasterization problems. Record
  whether correcting the existing metrics is sufficient before replacing it.
- Check Cyrillic coverage, redistribution license, generated asset size and
  compatibility with existing font selections and SDK size identifiers.

Produce side-by-side previews and a short recommendation. A font replacement
needs its own implementation and device review; this note does not select one.

## Initial comparison, 2026-11-18

Tiny5 Regular is the strongest candidate for a separate implementation trial.
Its downloaded font contains all 66 Russian letters, including Yo, and all 115
codepoints in the current Cyrillic pack. It is proportional and uses a native
pixel grid. It looks more angular than Jersey, especially at large sizes.

Handjet also covers the complete pack, but its small monochrome letters are too
thin in this comparison. Tiny5 Duo is too wide for the current small labels.
PixelCode Bold covers Russian but lacks 19 extended Cyrillic/number-sign characters
from the existing pack and is monospaced. Neither is recommended as a replacement.

The checked Tiny5, Handjet and PixelCode repositories provide SIL OFL 1.1 licenses.
Retain their copyright and license notices if distributing font assets. Check the
chosen exact file and any naming restrictions again before integration.

Sources: [Tiny5](https://github.com/Gissio/font_Tiny5),
[Tiny5 license](https://github.com/Gissio/font_Tiny5/blob/main/OFL.txt),
[Handjet](https://github.com/rosettatype/handjet),
[PixelCode](https://github.com/qwerasd205/PixelCode).

The comparison used the project's monochrome rasterizer with matched capital
heights. These are controlled font specimens, not application or panel screenshots:
[Air layout](jersey/air-comparison.png), [sizes](jersey/sizes-comparison.png).
Tiny5's generated ASCII plus Cyrillic trial has 210 glyphs and 1,037 bitmap bytes;
that is not a final firmware-size measurement. No production font was changed.

Next: trial Tiny5 in real application previews, check all six SDK sizes and the
physical panel, then decide whether to replace Jersey. Preserve existing font
settings and the SDK's size identifiers if proceeding.
