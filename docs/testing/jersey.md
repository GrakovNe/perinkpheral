# Jersey replacement study

Status: planned. Reported during acceptance on 2026-11-18.

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
