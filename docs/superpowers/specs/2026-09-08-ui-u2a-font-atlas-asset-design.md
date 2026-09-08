# UI U2a — FontAtlas cooked asset — Design

## Goal (Phase 4, UI pillar — slice 2a)

First half of **U2 (text rendering)**: the **font asset**. Add a cooked `FontAtlas`
resource so `mosaic import <project> font.ttf` bakes a glyph atlas + metrics that the
runtime can load — with **nothing rendered yet**. U2b (the next slice) adds the
`UiText` component, string layout, a glyph draw seam, a Vulkan text pipeline, and demo
text on top of this asset.

This mirrors the two existing cook pipelines exactly — **T5** (`.ppm` → cooked
`.image` + `TextureCache`) and **A3** (`.wav` → cooked `.audioclip` + `AudioClip`
resource). Rasterization happens **at cook time** via **stb_truetype**, walled inside
the importer (no stb symbol above the importer, exactly as the WAV parser kept
miniaudio out of the importer). The runtime loads a neutral, backend-free `FontAtlas`.

**Out of scope (→ U2b):** `UiText` component, string→quad layout, the glyph draw
seam, the Vulkan text pipeline, demo text. U2a produces a loadable font asset and
adds no rendering.

## Design

### `engine/ui/font_atlas.hpp` — neutral resource

```cpp
#pragma once

#include <cstdint>
#include <unordered_map>

#include "engine/render/image.hpp"

namespace mosaic::engine::ui
{
// One glyph's placement in the atlas + its layout metrics, in atlas pixels
// (the atlas was baked at FontAtlas::pixel_size). u*/v* are normalized [0,1].
struct Glyph
{
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas UV rect
    float advance = 0;                    // pen advance after this glyph
    float x_off = 0, y_off = 0;           // offset from the pen origin to the quad's top-left
    float width = 0, height = 0;          // quad size
};

// A baked bitmap font: an RGBA8 atlas (white, glyph coverage in the ALPHA channel)
// plus per-codepoint metrics and font-wide vertical metrics. Backend-free; the
// resource an AssetID of type "font" resolves to (like Image / AudioClip).
struct FontAtlas
{
    mosaic::engine::render::Image atlas;      // RGBA8; RGB=255, coverage in alpha
    float pixel_size  = 0;                    // size the atlas was baked at
    float ascent      = 0;                    // baseline-up (px, positive)
    float descent     = 0;                    // baseline-down (px, negative, stb convention)
    float line_height = 0;                    // ascent - descent + line_gap
    std::unordered_map<std::uint32_t, Glyph> glyphs; // keyed by codepoint
};
} // namespace mosaic::engine::ui
```

**Why coverage-in-alpha RGBA8:** U2b can then upload the atlas through the existing
`create_texture(Image)` / `TextureCache` and sample it with a decal-style textured-quad
pipeline — no new single-channel (R8) texture path. Slightly more atlas memory, trivial
for one font page.

### `app/asset/font_cook.hpp` (+ `app/asset/font_cook.cpp`) — cook / uncook

Mirrors `app/asset/audio_cook.hpp`. stb_truetype is included **only** in the `.cpp`
(the single `STB_TRUETYPE_IMPLEMENTATION` translation unit); the header exposes only
neutral types.

```cpp
namespace mosaic::app
{
// Rasterize ASCII 32..126 from a TTF/OTF byte blob at `pixel_size` into a FontAtlas.
// nullopt on an unparseable font. Deterministic for a given (bytes, pixel_size).
std::optional<mosaic::engine::ui::FontAtlas>
read_ttf(const std::string& bytes, float pixel_size);

// Cooked on-disk form (little-endian):
//   "MOSAFNT1"
//   f32 pixel_size, f32 ascent, f32 descent, f32 line_height
//   u32 atlas_width, u32 atlas_height
//   u32 glyph_count
//   glyph_count * { u32 codepoint; f32 u0,v0,u1,v1, advance, x_off, y_off, width, height }
//   atlas_width*atlas_height*4 bytes RGBA8 pixels
std::string cook_font(const mosaic::engine::ui::FontAtlas& font);

// Inverse of cook_font. nullopt on bad magic, truncation, or a size mismatch.
std::optional<mosaic::engine::ui::FontAtlas> uncook_font(const std::string& bytes);
}
```

**`read_ttf` (stb_truetype):**
- `stbtt_InitFont`; `scale = stbtt_ScaleForPixelHeight(&info, pixel_size)`.
- `stbtt_GetFontVMetrics` → `ascent`, `descent`, `line_gap`; scale each; `line_height =
  (ascent - descent + line_gap) * scale`.
- **Fixed atlas width** (512 px); shelf-pack glyphs left→right, wrapping to a new row
  at width overflow, tracking row height; final atlas height = packed rows (rounded up).
  1 px padding between glyphs to avoid bleed.
- Per codepoint 32..126: `stbtt_GetCodepointBitmapBox` (skip empties like space —
  record advance only, zero-size quad), `stbtt_MakeCodepointBitmap` rasterizes coverage
  (8-bit) into a scratch buffer; write it into the atlas alpha channel (RGB=255) at the
  packed origin; `stbtt_GetCodepointHMetrics` → `advance` (× scale). `x_off`/`y_off`
  come from the bitmap box; `width`/`height` from the box size; UVs from the packed
  rect ÷ atlas dims.
- Determinism: fixed codepoint order + fixed packing ⇒ bit-identical atlas across runs.

**`cook_font` / `uncook_font`:** straight serialize/deserialize of the struct above;
`uncook_font` guards magic, then that the byte length matches the declared glyph_count
+ pixel count before reading (return `nullopt` otherwise). Round-trip is byte-exact.

### `stb_truetype` dependency

Add via `FetchContent` in the root `CMakeLists.txt`, mirroring the miniaudio single-
header declaration (`nothings/stb`). Only `app/asset/font_cook.cpp` defines
`STB_TRUETYPE_IMPLEMENTATION` and includes `stb_truetype.h`; it links into the target
that owns the importer (the same one that compiles `audio_cook`/`asset_io`). No stb
symbol appears in any engine or header.

### `app/asset/asset_io.hpp` — import + load branches

- **`import_asset`:** add a `.ttf` / `.otf` branch (before the image fallback), baking
  at a fixed default `pixel_size` (48.0f): `read_ttf(bytes, 48.0f)` → `cook_font` →
  write `<stem>.font` + `<stem>.toml` (`type = "font"`). Overwrite-safety and the
  stem-collision refusal are already generic (T5 follow-up), so no new guard.
- **`load_project_assets`:** add a `meta.type == "font"` branch: read `<stem>.font`,
  `uncook_font`, and
  `registry.register_resource<mosaic::engine::ui::FontAtlas>(meta,
   std::make_shared<...>(*font))` — exactly the audio/image branch shape.

### `build_bundle`

No change — it already copies the whole `assets/` tree recursively (T5), so cooked
`.font` + `.toml` travel into a built bundle automatically.

## Testing (headless)

New unit tests under `tests/unit/app/` (+ a vendored font fixture):

- **`read_ttf` metrics** — rasterize the fixture font at 48 px; assert the glyph map
  covers the printable ASCII range (95 codepoints 32..126), `line_height > 0`,
  `ascent > 0`, a known glyph (e.g. `'A'`) has `advance > 0` and non-zero `width`/
  `height`, and `' '` (space) has `advance > 0` with a zero-size quad. Atlas is
  non-empty RGBA8 with the right `width*height*4` pixel count.
- **`cook_font` / `uncook_font` round-trip** — cook the fixture atlas, uncook it, and
  assert bit-identical: same atlas dims + pixel bytes, same `pixel_size`/`ascent`/
  `descent`/`line_height`, and every glyph's metrics equal (iterate the map).
- **`uncook_font` rejects garbage** — bad magic and a truncated blob both return
  `nullopt` (no crash).
- **import → load end-to-end** — `import_asset(project, fixture.ttf)` writes `.font` +
  `.toml` (`type == "font"`); `load_project_assets` registers a resolvable `FontAtlas`
  under the metadata uuid (resolve it back and check a glyph is present).

**Fixture:** vendor a small permissively-licensed TTF under `tests/fixtures/`
(SIL OFL — e.g. a Liberation font, which is present on this machine), committing its
license text alongside. The test references it by a build-time-defined fixture path
(the same mechanism existing app tests use for their data files); confirm how
`tests/unit/app` locates fixtures and reuse it.

## Files

**New**
- `engine/ui/font_atlas.hpp` — neutral `Glyph` / `FontAtlas`.
- `app/asset/font_cook.hpp` — `read_ttf` / `cook_font` / `uncook_font` decls + neutral impls.
- `app/asset/font_cook.cpp` — `STB_TRUETYPE_IMPLEMENTATION` TU + the `read_ttf` body.
- `tests/fixtures/<font>.ttf` (+ its OFL license) — test font.
- `tests/unit/app/font_cook_test.cpp` — metrics + round-trip + reject tests.
- `tests/unit/app/font_asset_test.cpp` — import→load end-to-end (or fold into an
  existing asset-io test file if one already covers import/load).

**Modified**
- `CMakeLists.txt` — `FetchContent` stb; make `stb_truetype.h` available to the
  importer target; ensure `font_cook.cpp` compiles into it.
- `app/asset/asset_io.hpp` — `.ttf`/`.otf` import branch + `"font"` load branch.
- `tests/CMakeLists.txt` — the new test sources (+ fixture path define if needed).

## Non-goals / follow-ups

- **U2b:** `UiText` component, layout, glyph draw seam, Vulkan text pipeline, demo text.
- Later: multi-size / SDF atlases (crisp scaling), Unicode beyond ASCII, kerning pairs,
  multiple font pages, `.otf` CFF coverage verification.
