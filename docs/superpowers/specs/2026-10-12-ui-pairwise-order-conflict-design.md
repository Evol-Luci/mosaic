# UI pairwise order conflicts — design

**Status:** spec, written 2026-10-12. Authored under standing authorization to
proceed from spec to implementation without an intervening review, so the
morning review covers the spec and the code together.

## Why

`UiSystem` resolves a click by `UiButton::order` (`topmost_hit`). The user
decides what is on top by `UiPanel::order`, which the panel pass sorts by. The
two are independent fields, so they can rank the same pair of buttons
differently — and when they do, **the click lands on a button the user cannot
see**. There is no symptom other than the wrong thing happening.

[#240](https://github.com/Evol-Luci/mosaic/pull/240) shipped a *heuristic* lint
for this: it reports a single entity whose `UiButton::order` differs from the
`UiPanel::order` beside it. That catches the authoring slip the bug grows from,
and its own header says plainly that it is not the real check — a lone button
cannot misroute a click whatever its order says. The real failure is **pairwise
and relative**, and this spec is that check.

Its prerequisite landed with
[#241](https://github.com/Evol-Luci/mosaic/pull/241): before that, only the
panel pass sorted, and `UiImage::order`/`UiText::order` were inert. The notion
of "what the user sees on top" is now well defined for all three.

## What counts as a conflict

For an overlapping pair of buttons A and B, with `UiPanel` components on their
own entities, take:

```
da = A.button.order - B.button.order      // who receives the click
dp = A.panel.order  - B.panel.order       // who appears on top
```

| `da` | `dp` | Verdict |
|---|---|---|
| non-zero | non-zero, opposite sign | **Inverted** — a definite bug |
| non-zero | zero | **Ambiguous** |
| zero | non-zero | **Ambiguous** |
| zero | zero | **not reported** |
| non-zero | non-zero, same sign | consistent |

**Inverted** is the bug in the title: the click provably goes to the button
drawn underneath.

**Ambiguous** is reported because the resolution falls to iteration order over
two *different* sparse sets. `ui_hit.hpp`'s rule 2 already states that the
equal-order tie-break is deterministic but only *usually* agrees with draw
order, because the two sets have independent insertion histories and removal is
swap-and-pop — so **destroying an unrelated button can flip which of these two
receives the click**, with nothing in the scene having changed visually. That is
worth a warning even though no single frame is provably wrong.

**Both-zero is deliberately not reported.** The author has expressed no
preference on either axis; the pair is symmetric, and which one appears on top
is as arbitrary as which one is clicked. Reporting it would fire on every pair
of overlapping default-constructed buttons, which is noise rather than a
finding. Recorded here so the omission reads as a decision.

**No reliance on iteration order anywhere.** The check compares only the four
authored integers. It never asks which entity comes first in a `for_each`,
because that is exactly the fragile property it is warning about; a check built
on it would change its own verdict when an unrelated entity was destroyed.

### Overlap is tested with inclusive edges

```
overlap(a, b) = a.x <= b.x + b.w && b.x <= a.x + a.w
             && a.y <= b.y + b.h && b.y <= a.y + a.h
```

Inclusive on both axes, matching `topmost_hit`, which tests containment with
`>=`/`<=` and therefore treats a pointer exactly on a shared edge as inside
**both** rects. Two buttons that merely touch along an edge can genuinely
misroute a click on that line, so touching counts as overlapping. Exclusive
comparisons here would make the check disagree with the hit test it exists to
police.

## Design

### 1. Extract the button flattening — `engine/ui/ui_button_rects.hpp` (new)

```cpp
inline Anchor button_anchor(int a);                                  // moved
inline std::vector<HitCandidate> button_hit_candidates(World& world); // extracted
```

`UiSystem::candidates` is a private static that flattens every `UiButton` into
a `HitCandidate{e, b.order, resolve_ui_rect(...)}`. The conflict check needs the
**same rects the hit test actually uses** — a second, parallel implementation
would be a check that polices rects nobody clicks on, and it would drift the
first time either side changed.

So the flatten moves to a new header that both use, and `button_anchor` moves
with it (its only current caller is the flatten itself). `ui_system.hpp`
includes the new header and keeps `UiSystem::candidates` as a one-line call, so
no existing call site or test changes meaning.

The new header is deliberately not `ui_hit.hpp`: that file's comment makes a
point of needing "no World, no component storage and no pointer provider" so
`topmost_hit` stays trivially testable, and adding a `World`-dependent function
there would spend that property for nothing.

### 2. The check — added to `engine/ui/ui_order_check.hpp`

```cpp
enum class OrderConflictKind { Inverted, Ambiguous };

struct OrderConflict
{
    EntityID          a, b;        // a is the lower entity value, for a stable key
    OrderConflictKind kind;
    int a_button_order, a_panel_order;  // entity a's own two orders
    int b_button_order, b_panel_order;  // entity b's own two orders
};

std::vector<OrderConflict> find_order_conflicts(World& world);
```

It lives beside `find_order_disagreements` because a reader looking for order
diagnostics should find both in one file, and the two share the subject even
though they share no code. The file goes from ~70 to ~160 lines, which is still
one idea per file.

**Pair ordering is normalized** so a finding has a stable identity: `a` is
whichever entity has the lower `EntityID::value()`, and the four order fields
follow that assignment rather than iteration order. Without the normalization
the same conflict would key differently depending on which entity the outer loop
reached first, and warn twice.

**A button with no `UiPanel` on its own entity is skipped.** Its visual rank is
undefined under the ownership rule (a button owns the UI components on its own
entity), so there is nothing to compare. This is the same deliberate blind spot
the per-entity lint has, and it is where a button overlaying a panel on a
*different* entity falls: still invisible, still recorded in the backlog.

**Cost is O(n²) in buttons**, evaluated once per editor frame. For an authoring
tool that is nothing — every scene in this repository has one button, and 64
buttons is 2,016 pairs of four integer comparisons. No cap, no spatial index: a
guard against a scene size nothing approaches would be machinery defending a
hypothetical. Noted as a non-goal below so a future reader sees it was weighed.

### 3. One de-duplication ledger for both streams — `studio/order_warnings.hpp`

`OrderWarningModel` currently de-duplicates per-entity findings keyed on
`(entity, button_order, panel_order)`, rebuilding its set each round so a
finding that disappears and returns warns again. A pairwise finding has two
entities and four numbers, so it does not fit that key.

Rather than a second near-identical model, the semantics move into a small
ledger keyed on strings:

```cpp
class WarningLedger
{
public:
    struct Entry { std::string key, signature; };

    // The INDICES into `current` that are new: a key absent last round, or
    // present with a different signature. Rebuilds from `current`, so a key that
    // stops being reported is forgotten and warns again if it returns.
    //
    // Indices rather than keys, so a caller maps a result straight back to its
    // own finding (`for (auto i : ledger.emit(entries)) warn(findings[i]);`)
    // instead of keeping a second key-to-finding map of its own.
    std::vector<std::size_t> emit(const std::vector<Entry>& current);
};
```

Each check builds its own `key` (what the finding is *about*) and `signature`
(the numbers that make this instance of it distinct), and formats its own
message. One place holds the rebuild-don't-grow rule that both streams depend
on, which is the subtle part and the part worth not duplicating.

`OrderWarningModel` becomes a thin typed wrapper that builds its key from the
entity and its signature from the two orders, so its behaviour is preserved
exactly — including the drag-burst that was reviewed at the window on 2026-10-12
and deliberately kept. Its tests are rewritten against the new shape, and the
burst's "numbers are part of the identity" property is re-asserted so the
refactor cannot quietly drop it.

**One stale comment gets corrected while we are in the file.** Its "why every
frame at all" note argues from `order` *not* being editable in the Inspector —
which #242 made false three commits later. The reasoning that survives is the
other half it already gives (scripts, plugins, the Script Console, MCP, the
live-link), plus the Inspector itself now.

This is a refactor of shipped code in service of the new feature, in a file the
work already touches. It is not a licence to tidy anything else.

### 4. The warn site — `app/run/editor.cpp`

The existing per-frame block gains a second loop over `find_order_conflicts`,
through the same ledger, logging to the same `ui` channel:

```
[WARN] ui: entities 4294967305 and 4294967306 overlap, but the click goes to the
       one drawn underneath (UiButton orders 5 and 2; UiPanel orders 1 and 7)
```

and for the ambiguous kind, a message naming which axis is tied and that the
outcome can change when an unrelated button is destroyed.

Editor-only, like the existing lint: `mosaic run` never calls it, so a shipped
game never logs authoring diagnostics at its players.

### 5. Both lints stay

They report different things — one entity's two fields drifting apart, versus
two buttons genuinely conflicting — and a scene can legitimately produce both.
Neither suppresses the other, because the per-entity lint's value is catching
the slip *before* a second button exists, which is precisely when the pairwise
check has nothing to say.

## Non-goals

- **Rect overlap across entities.** A button overlaying a panel on a *different*
  entity stays invisible to both lints. Needs a definition of ownership that the
  repository's entity-identity convention does not supply.
- **`UiImage`/`UiText` as the visual half.** Only `UiPanel` ranks a button's
  visual, because only a panel is a filled rect a user reads as "the button".
  Including the others would need a rule for which of several visuals on one
  entity defines the button's apparent depth.
- **A spatial index or a pair-count cap.** O(n²) on a handful of buttons.
- **Fixing anything automatically.** Diagnostic only; no component is written.
- **Collapsing the three anchor clampers.** `to_anchor`, `hierarchy_anchor` and
  `button_anchor` are three copies of the same four-line function in three
  files. Pre-existing, unrelated to this work, and recorded in the backlog
  rather than folded in here.

## Testing

Pure functions over a `World`, so all of it is headless — no device, no ImGui,
no pointer provider. That is why this slice was chosen for unattended work.

**`find_order_conflicts`:**
- Inverted pair reported, with all four orders carried and not transposed.
- Consistent pair (same-sign deltas) silent.
- Non-overlapping pair with inverted orders silent — overlap is the gate.
- Edge-touching pair reported, pinning the inclusive comparison.
- Tied button orders + differing panel orders → `Ambiguous`.
- Tied panel orders + differing button orders → `Ambiguous`.
- Both tied → silent.
- A button with no `UiPanel` → silent, even against a conflicting neighbour.
- Three mutually overlapping conflicting buttons → three findings, not two.
- Pair normalization: the finding's `a` is the lower `EntityID` regardless of
  creation order, asserted by building the same scene both ways.
- Nested buttons under `UiParent` conflict correctly, since rects resolve within
  the parent frame.

**`WarningLedger`:** new key emitted; unchanged repeat silent; changed signature
re-emitted; disappear-and-return re-emitted; two keys independent.

**`OrderWarningModel`:** its existing cases, rewritten, so the refactor is
behaviour-preserving and the kept drag-burst stays asserted.

**Mutation probes**, derived from the code rather than the assertion list:
overlap made exclusive; one axis dropped from the overlap test; sign comparison
inverted; `Ambiguous` collapsed into `Inverted`; both-zero reported; the
no-panel skip removed; pair normalization dropped; the ledger's rebuild turned
into a grow.

## What this cannot verify

Nothing here needs eyes — but nothing here *proves* the engine actually
misroutes the click in the case being warned about. That claim rests on
`topmost_hit`'s existing tests plus the panel pass's sort, both already covered.
A test that drives a real pointer into a conflicting pair and asserts which
button receives the event would close that gap end to end; it is in scope for
the implementation if it falls out cheaply, and recorded as a follow-up if not.
