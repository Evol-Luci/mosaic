# U11 — text quality: decomposition

**Date:** 2026-10-26
**Status:** decomposition. Two ordered slices: **U11a → U11b**. SDF is explicitly not in it.
**Source items:** the 🔴 `U11 text quality` row added by the
[2026-10-26 tier re-derivation](../../record/2026-10.md#the-2026-10-26-tier-re-derivation-the-ui-pillars-next-group-is-the-question),
and the `U11` group in the [UI pillar decomposition](2026-10-07-ui-pillar-decomposition.md).

Taken in parallel with [U9](2026-10-26-u9-clipping-scroll-decomposition.md) rather than after
it, at the human's request, because the two share no files: U9 lives in
`engine/render/` + `modules/render_vulkan/`, U11 in `engine/ui/text_layout.hpp` +
`app/asset/font_cook.cpp`.

---

## The defect, stated precisely

`layout_text` (`engine/ui/text_layout.hpp`) iterates its `std::string` as `unsigned char`
and `continue`s on any value absent from the atlas — in **both** the measure pass (`:36-41`)
and the place pass (`:51-67`). The atlas is cooked for **codepoints 32-126 only**
(`app/asset/font_cook.cpp:25`, `kFirst = 32, kLast = 126`).

So a UTF-8 character arrives as two or more separate bytes, every one of them outside
32-126, and **every one is dropped**. A label authored as `"Café"` renders `"Caf"`, and
because the measure pass drops them too, the string is *re-centred* as if it had always been
three characters. There is no warning, no fallback glyph, and no log line.

**This is characterised, not accidental.** `tests/unit/ui/text_layout_test.cpp:77`,
*"layout_text ignores codepoints absent from the atlas"*, asserts `quads.size() == 2` for
`"A\tB"`. **Changing that test is in scope and intended** — a delegate must not treat it as a
regression to preserve. What the new behaviour keeps from it is the control-character half:
see the settled decisions.

---

## Why U11a before U11b

Word-wrap searches for break points over measured advances. If the measurement still runs
per *byte*, a wrap point can land **inside** a multi-byte character, which is a worse failure
than the one being fixed: the first half of a character on one line and the second on the
next, each dropped for being unknown. Decoding has to be correct before anything searches
over the result. The order is not a preference.

---

## Settled decisions

**Control characters keep today's silent skip; printable codepoints get a fallback.** A
fallback glyph for `\t` or `\r` would be worse than dropping it — it would put a visible
mark where the author meant whitespace or nothing. So: codepoints below `0x20` plus `0x7F`
(C0 and DEL) continue silently, exactly as now. Everything else absent from the atlas
renders the replacement glyph.

**The replacement glyph resolves in this order, at layout time:** `U+FFFD` if the atlas has
it, else `'?'` (0x3F, which every 32-126 atlas has), else skip. The order matters because the
first option is the unambiguous one and the second is merely available — `'?'` as a fallback
is indistinguishable from an authored `'?'`, which is a known cost, accepted for v1 because
the alternative is no fallback at all on an atlas that lacks `U+FFFD`.

**Cooking `U+FFFD` is in U11a's scope**, as a one-codepoint addition beside the 32-126 range
— not a general widening. **But it is conditional on the font providing it:** if
`stbtt_FindGlyphIndex` reports no glyph, nothing is baked and the resolution order above
falls through to `'?'`. Whether the bundled font has it is a **measurement, not an
assumption** — report which branch this repo's font actually takes.

**Widening the cooked range beyond ASCII + `U+FFFD` is NOT in U11.** Latin-1, let alone
anything larger, changes the atlas's pixel budget and the cooked asset for every project.
That is an asset-pipeline decision with its own size measurement; the backlog carries it.

**Decoding is UTF-8 only, and malformed input is replaced, not rejected.** An invalid byte
sequence yields one replacement glyph and decoding resumes at the next byte that can start a
character. Never read past the end of the string, and never loop forever on a bad byte — a
truncated sequence at the end of a string is the case that does both if the bounds are wrong.

**`layout_text` keeps its signature where it can.** `world_text.hpp:25` and
`render_system.hpp:614` are its only non-test callers; adding a parameter means touching both
and that is acceptable, but not gratuitously.

---

## The slices

### U11a — UTF-8 decoding, and a visible fallback for a missing printable glyph

**Shape.** Decode the `std::string` to codepoints once, and run both the measure pass and the
place pass over *codepoints*. Resolve a missing printable codepoint to the replacement glyph
per the order above. Add `U+FFFD` to the cook when the font provides it.

**Gate.** `"Café"` produces **four** glyph quads, not three, and the fourth is the
replacement (or `é` once the range is widened — but the range is NOT widened here, so it is
the replacement). A malformed sequence produces exactly one replacement and does not hang.
Control characters still produce nothing. The measure pass and the place pass agree, which is
observable as the string staying centred: an anchored-Centre string of N codepoints must have
the same total advance in both passes.

**The decisive mutation.** Revert the decode to per-byte iteration, keep the new tests, and
watch them go red — then report **which** assertion failed. I expect the quad-count assertion
on `"Café"`; if something else fails first, that is worth knowing, because it would mean the
count assertion is not the load-bearing one.

**Also required:** a test that **nothing authored today changes appearance.** Every existing
label is ASCII, so U11a must be a no-op for all of them — `layout_text` over an ASCII string
must produce byte-identical quads before and after. That is the regression this slice can
introduce and it is the one nobody would notice by eye.

### U11b — multi-line and word-wrap

**Shape.** Honour `\n`, and wrap at an authored maximum width, breaking on spaces. The
measure pass computes per-line widths so anchoring still resolves per line rather than over
the whole block.

**Gate.** A string with no space longer than the max width does not wrap mid-character (the
U11a dependency, made observable); `\n` produces two lines whose anchors resolve
independently; a wrapped block's height is `lines × line_height`.

**Depends on** U11a.

---

## Non-goals, each recorded in the backlog in the same turn as this spec

- **SDF and multi-size atlases.** The expensive half: a different atlas generation pipeline, a
  different fragment shader, and a decision about whether it replaces or coexists with the
  bitmap path. Wants its own decomposition, as the pillar spec already says.
- **Kerning.** Per-pair advances; the atlas stores per-glyph advances only.
- **Widening the cooked codepoint range** beyond ASCII + `U+FFFD`.
- **Bidirectional text and RTL scripts.**
- **Grapheme clusters and combining marks.** One codepoint is one glyph in U11a; a combining
  accent will render as its own mark beside the base character rather than over it.
- **UAX #14 line breaking.** U11b breaks on spaces, not on the Unicode algorithm.
- **Tab stops.** `\t` keeps the silent skip U11a preserves.

---

## What belongs to the human

Merging; the eye check that a replacement glyph looks like an error rather than like content;
and any ruling where a slice overturns a decision above **with evidence**.
