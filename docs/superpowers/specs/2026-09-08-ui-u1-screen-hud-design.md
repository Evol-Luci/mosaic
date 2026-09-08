# UI U1 — screen-space HUD foundation — Design

## Goal (Phase 4, UI pillar — slice 1)

Start the **UI** pillar: a runtime **game UI** that ships in the built game, distinct
from the editor's existing ImGui dev-tools (`init_ui`/`begin_ui_frame`/`end_ui_frame`).

U1 delivers the foundation: **anchored, resolution-independent colored panels**
composited over the finished 3D scene as a HUD. A reflected `UiPanel` component holds
an anchor + normalized position/size + color/alpha + draw order; `RenderSystem`
resolves each panel to a normalized screen rect and submits it through a new
`draw_ui` seam; a Vulkan screen-space quad pipeline draws the alpha-blended quads
over everything (swapchain + offscreen-viewport variants).

**Deliberately out of scope for U1** (each a later slice — see *Pillar decomposition*):
text/glyphs, images/sprites, input/interaction, hierarchy/layout containers,
world-space panels.

Same GPU caveat as the VFX slices: headless geometry/wiring + build +
`mosaic run --window` exit 0 is what the sandbox verifies; **the on-screen visual is
the maintainer's hardware check** (GPU output is not observable in the sandbox).

## Pillar decomposition (the full arc; only U1 is built in this slice)

- **U1 — Screen-space HUD foundation** ← this slice. Anchored colored panels.
- **U2 — Text / glyph rendering.** A font atlas + a text draw seam; a reflected
  `UiText` component (string, font size, color, anchor). The first slice that needs a
  glyph pipeline and a bitmap/SDF atlas.
- **U3 — Images / sprites.** Textured HUD panels that resolve an `AssetID` through the
  existing `TextureCache` (mirrors the VFX decal path); adds `texture` to the panel
  surface.
- **U4 — Interaction.** An input hit-testing seam: pointer position → rect hit-test →
  hover/press/click, delivered to scripts via the existing `on_event` bus (a
  `'ui'`-style script event). Introduces the first stateful `UiSystem`.
- **U5 — Layout & hierarchy.** Parent/child rects, containers (row/column/grid),
  stacking, and aspect-correct sizing (needs the target pixel size, deferred from U1);
  world-space UI panels positioned by a `WorldTransform` (like decals) also land here.

## Design

### `engine/ui/ui_rect.hpp` — neutral layout model (pure)

```cpp
namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// Which screen reference point an element is anchored to. Origin is top-left,
// x right, y down — matching normalized screen space [0,1].
enum class Anchor
{
    TopLeft = 0, Top, TopRight,
    Left,        Center, Right,
    BottomLeft,  Bottom, BottomRight
};

// A resolved rectangle in normalized screen space: origin top-left, x/y/w/h in [0,1].
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

// Resolve an anchored element to a normalized screen Rect.
//   size : element size as a fraction of the screen (w = size.x, h = size.y).
//   pos  : normalized offset FROM the anchor point, toward the screen interior
//          (i.e. pos {0,0} places the element flush against its anchor).
// The element's own reference corner is chosen to match the anchor, so an element
// anchored TopRight grows left/down from the top-right, BottomLeft grows right/up
// from the bottom-left, Center is centered on the middle, etc.
Rect resolve_rect(Anchor anchor, Vec2 pos, Vec2 size);
}
```

`resolve_rect` is a **pure** function of its arguments — it needs no pixel size, so it
is fully resolution-independent and trivially headless-testable. The math per axis:

- Anchor's horizontal band is Left / Center / Right (columns 0/1/2 of the enum),
  vertical band is Top / Center / Bottom (rows 0/1/2).
- **Left/Top:** `rect.x = pos.x` (offset inward from the left/top edge).
- **Center:** `rect.x = 0.5 - size.x/2 + pos.x` (centered, `pos` nudges).
- **Right/Bottom:** `rect.x = 1 - size.x - pos.x` (offset inward from the right/bottom
  edge, so `pos` still means "toward the interior").
- Width/height are always `size.x`/`size.y`.

**Documented limitation (fixed in U5):** because sizing is a pure fraction of each
screen dimension, a panel authored as a square (`size.x == size.y`) is only visually
square at a 1:1 aspect. Aspect-correct sizing needs the target pixel dimensions and is
deferred to U5.

### `engine/ui/ui_panel.hpp` — reflected component

```cpp
struct UiPanel
{
    int   anchor = 0;                 // cast to ui::Anchor (0 = TopLeft); default corner
    Vec2  pos    = Vec2{0, 0};        // normalized offset from the anchor, toward interior
    Vec2  size   = Vec2{0.2f, 0.1f};  // fraction of the screen (w, h)
    Vec3  color  = Vec3{1, 1, 1};     // panel fill color
    float alpha  = 1.0f;              // 0 = invisible, 1 = opaque
    int   order  = 0;                 // draw/stacking order; higher draws on top
};
```

Reflected + registered as `"UiPanel"` (serializes with the scene, shows in the
Inspector, add/remove via the generic Inspector control). `anchor`/`order` are `int`
(the reflection surface has no enum type; the render path casts `anchor` to
`ui::Anchor`, clamping out-of-range to `TopLeft`). A normal component, so a **script
can animate `alpha`** (fade a HUD element) or `pos` (slide it) via the existing
component-mutation path.

### `engine/render/render_device.hpp` — new seam

```cpp
struct UiQuad
{
    mosaic::engine::ui::Rect       rect;             // normalized screen rect (top-left origin)
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};
```

`RenderDevice` gains a default-no-op
`virtual void draw_ui(const std::vector<UiQuad>&) {}` — like `draw_decals` /
`draw_screen_effect`, so headless devices and test fakes need not implement it.

### `engine/render/render_system.hpp` — final HUD pass (topmost)

At the very end of `render` — **after** the screen-effect overlay, so the HUD is the
topmost layer and stays visible over a fade/vignette — collect `UiPanel`s:

```cpp
// Collect (order, quad) pairs so UiQuad itself stays a pure draw record.
std::vector<std::pair<int, UiQuad>> pending;
world.for_each<UiPanel>([&](EntityID, UiPanel& p) {
    if (p.alpha <= 0.0f) { return; }                 // skip fully transparent
    Anchor a = to_anchor(p.anchor);                  // clamp out-of-range -> TopLeft
    pending.push_back({ p.order,
        UiQuad{ resolve_rect(a, p.pos, p.size), p.color, p.alpha } });
});
std::stable_sort(pending.begin(), pending.end(),
                 [](const auto& l, const auto& r) { return l.first < r.first; });
std::vector<UiQuad> quads;
quads.reserve(pending.size());
for (auto& [order, q] : pending) { quads.push_back(q); }
if (!quads.empty()) { device.draw_ui(quads); }
```

`order` is carried in a parallel pair only for the sort, so `UiQuad` stays a pure draw
record (no `order` field crosses the device seam). The observable contract is "lower
`order` first, stable within equal `order`". No dedicated stateful system in U1:
layout is a pure function resolved at the render call site, matching the
`ScreenEffect`/`Decal` precedent. A stateful `UiSystem` arrives in U4/U5 when
interaction and hierarchy need per-frame state.

### `modules/render_vulkan` — screen-space quad pipeline

A new alpha-blended pipeline (`ui.vert`/`ui.frag`) modeled on the decal /
screen-effect pipelines:

- **No vertex buffer.** The vertex shader generates a unit quad from
  `gl_VertexIndex` (6 verts) and maps it through the quad's normalized `Rect` into NDC:
  `ndc.x = (rect.x + u * rect.w) * 2 - 1`,
  `ndc.y = 1 - (rect.y + v * rect.h) * 2` (top-left origin → NDC y-up).
- The `Rect` + `color` + `alpha` arrive as **push constants**, one draw per quad
  (U1 has few HUD elements; instancing/batching is a later optimization).
- `ui.frag` outputs `vec4(color, alpha)`; standard src-alpha / one-minus-src-alpha
  blending; depth test/write **off** (pure overlay).
- **Swapchain + offscreen-viewport variants** (two pipeline handles against the two
  render passes, exactly like the existing overlays) so the HUD appears in both
  `mosaic run --window` and the editor viewport panel.
- `draw_ui` is a no-op before the first `begin_frame`/pass, like the sibling seams.

Shaders are compiled to SPIR-V through the existing shader-build step used by the
other `.vert`/`.frag` pairs.

### `app/run/demo_scene.hpp` — demo HUD panel

Add one HUD entity to the default scene: a semi-transparent bar anchored `TopLeft`
(e.g. `pos {0.02, 0.02}`, `size {0.25, 0.06}`, a translucent color, `alpha ~0.6`). It
serializes and survives `save → mosaic build → mosaic run`, and proves the end-to-end
path (component → render system → Vulkan) on hardware.

## Testing (headless; GPU visual is maintainer-verified)

New unit tests under `tests/unit/`:

- **`resolve_rect` per anchor** — all 9 anchors with a representative `pos`/`size`,
  asserting the resolved `Rect` for each (corners flush, center centered, `pos`
  offsets toward the interior on every edge). Covers the core layout math.
- **`UiPanel` reflection** — reflect → `to_value` → construct round-trip (all fields,
  including `Vec2 pos`/`size`), and registration under the name `"UiPanel"`.
- **`RenderSystem` HUD pass** — a `FakeRenderDevice` capturing `draw_ui`: N panels
  produce N quads with the expected resolved rects/colors/alpha; a fully transparent
  panel (`alpha <= 0`) is skipped; quads are `order`-sorted (stable within equal
  `order`); no panels → `draw_ui` not called.
- **Default-scene counts** — update the four app tests that assert the default-scene
  entity count (the demo HUD panel adds one entity) and register `UiPanel` wherever
  the default scene's components are registered, per the project's
  default-scene-count convention.

Build + `mosaic run --window` exits 0 with no Vulkan validation errors is the
integration check; the on-screen HUD is the maintainer's hardware verification.

## Files

**New**
- `engine/ui/ui_rect.hpp` — `Anchor`, `Rect`, `resolve_rect`.
- `engine/ui/ui_panel.hpp` — reflected `UiPanel`.
- `modules/render_vulkan/shaders/ui.vert`, `ui.frag`.
- `tests/unit/ui/…` — layout + reflection tests (render-system + default-scene tests
  extend existing files).

**Modified**
- `engine/render/render_device.hpp` — `UiQuad` + `draw_ui` seam.
- `engine/render/render_system.hpp` — final HUD pass.
- `modules/render_vulkan/…` — the screen-space quad pipeline (swapchain + viewport),
  `draw_ui` implementation, shader-build wiring.
- `app/run/demo_scene.hpp` — demo HUD panel + `UiPanel` registration.
- `engine/CMakeLists.txt` (+ any test CMake) — the new `engine/ui` headers/tests.
- The default-scene count tests + component-registration sites.

## Non-goals / follow-ups (documented)

Text (U2), images/sprites (U3), input & interaction (U4), hierarchy/layout containers
and world-space panels + aspect-correct sizing (U5). Batching/instancing the quad
draws is a later optimization once HUDs grow large.
