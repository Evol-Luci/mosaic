# UI U2a — FontAtlas Cooked Asset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a cooked `FontAtlas` asset so `mosaic import <project> font.ttf` bakes a glyph atlas + metrics the runtime can load — with nothing rendered yet (rendering is U2b).

**Architecture:** A neutral `FontAtlas` resource (RGBA8 atlas, glyph coverage in the alpha channel, + per-codepoint metrics) is produced at **cook time** by `read_ttf` (stb_truetype, walled in one `.cpp`), serialized by `cook_font`/`uncook_font` (a `"MOSAFNT1"` blob). `import_asset` gains a `.ttf`/`.otf` branch and `load_project_assets` a `"font"` branch — mirroring the existing image (T5) and audio (A3) cook pipelines exactly.

**Tech Stack:** C++20, Catch2, CMake, stb_truetype (single-header, public domain, vendored).

**Spec:** `docs/superpowers/specs/2026-09-08-ui-u2a-font-atlas-asset-design.md`

## Global Constraints

- **Module wall:** stb_truetype appears in **exactly one** translation unit (`app/asset/font_cook.cpp`); no `stbtt_*` / stb include in any header or engine file. Neutral `FontAtlas`/`Glyph` are backend-free.
- **Build convention:** CMake presets into `build/debug/`. Configure (already done): `cmake --preset debug`. Build: `cmake --build --preset debug [--target <t>]`. Test binary: `./build/debug/tests/mosaic_unit_tests "[tag]"` (Catch2 tag filter; comma = OR). Full suite: `ctest --preset debug`. App binary: `./build/debug/app/mosaic`. Do **not** use `cmake --build build` or `./build/app/...`.
- **Cook lib:** `font_cook.cpp` compiles into `mosaic_app` (the static lib that both the `mosaic` executable and `mosaic_unit_tests` link). `${CMAKE_SOURCE_DIR}` is already a public include dir, so `#include "third_party/stb/stb_truetype.h"` and `#include "engine/ui/font_atlas.hpp"` resolve.
- **Cooked blob format (little-endian):** `"MOSAFNT1"` + `f32 pixel_size, ascent, descent, line_height` + `u32 atlas_width, atlas_height, glyph_count` + `glyph_count × { u32 codepoint; f32 u0,v0,u1,v1,advance,x_off,y_off,width,height }` + `atlas_width*atlas_height*4` RGBA8 bytes. Round-trip byte-exact.
- **Bake parameters:** ASCII codepoints **32..126** inclusive (95 glyphs), atlas width **512 px**, 1 px glyph padding, default import `pixel_size = 48.0f`.
- **Test tag:** all these tests use Catch2 tag `[app]` (existing app-test convention), so run with `./build/debug/tests/mosaic_unit_tests "[app]"`. Retry the build once if it fails only at the link step (the ~170 MB test binary occasionally fails to link under memory pressure; a transient link failure is not a code error).

---

### Task 1: Vendor stb + FontAtlas resource + `read_ttf`

**Files:**
- Create: `third_party/stb/stb_truetype.h` (vendored public-domain header)
- Create: `engine/ui/font_atlas.hpp`
- Create: `app/asset/font_cook.hpp`
- Create: `app/asset/font_cook.cpp`
- Create: `tests/fixtures/LiberationSans-Regular.ttf` (+ `tests/fixtures/LiberationSans-License.txt`)
- Create: `tests/unit/app/font_cook_test.cpp`
- Modify: `app/CMakeLists.txt` (add `font_cook.cpp` to `mosaic_app`)
- Modify: `tests/CMakeLists.txt` (add test source + `MOSAIC_TEST_DIR` compile define)

**Interfaces:**
- Produces:
  - `struct mosaic::engine::ui::Glyph { float u0,v0,u1,v1, advance, x_off, y_off, width, height; };`
  - `struct mosaic::engine::ui::FontAtlas { render::Image atlas; float pixel_size, ascent, descent, line_height; std::unordered_map<uint32_t, Glyph> glyphs; };`
  - `std::optional<FontAtlas> mosaic::app::read_ttf(const std::string& bytes, float pixel_size);`

- [ ] **Step 1: Vendor stb_truetype + the font fixture**

```bash
mkdir -p third_party/stb tests/fixtures
curl -fsSL -o third_party/stb/stb_truetype.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
cp /usr/share/fonts/liberation/LiberationSans-Regular.ttf tests/fixtures/LiberationSans-Regular.ttf
```

Verify the header is the real thing (has the implementation + public-domain notice):

```bash
grep -c "STB_TRUETYPE_IMPLEMENTATION\|stbtt_InitFont\|public domain" third_party/stb/stb_truetype.h
```
Expected: a non-zero count (all three strings present). Then create `tests/fixtures/LiberationSans-License.txt` containing the SIL Open Font License 1.1 header note:

```text
LiberationSans-Regular.ttf — Liberation Sans, © Red Hat, Inc.
Licensed under the SIL Open Font License, Version 1.1.
Full license: https://scripts.sil.org/OFL
Bundled here solely as a unit-test fixture for the font-atlas cooker.
```

- [ ] **Step 2: Write the failing test**

Create `tests/unit/app/font_cook_test.cpp`:

```cpp
#include <optional>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"
#include "app/run/scene_io.hpp" // load_scene_file

using Catch::Approx;
using mosaic::app::read_ttf;
using mosaic::engine::ui::FontAtlas;

namespace
{
std::string fixture_ttf()
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf";
    const auto bytes = mosaic::app::load_scene_file(path);
    REQUIRE(bytes.has_value());
    return *bytes;
}
} // namespace

TEST_CASE("read_ttf bakes ASCII glyph metrics from a TTF", "[app]")
{
    const std::optional<FontAtlas> font = read_ttf(fixture_ttf(), 48.0f);
    REQUIRE(font.has_value());

    // Baked at the requested size, with sane vertical metrics.
    REQUIRE(font->pixel_size == Approx(48.0f));
    REQUIRE(font->ascent > 0.0f);
    REQUIRE(font->descent < 0.0f);          // stb convention: descent is negative
    REQUIRE(font->line_height > font->ascent);

    // All printable ASCII 32..126 present (95 glyphs).
    REQUIRE(font->glyphs.size() == 95);

    // 'A' is a real glyph with a positive advance and a non-empty quad.
    const auto a = font->glyphs.find(static_cast<std::uint32_t>('A'));
    REQUIRE(a != font->glyphs.end());
    REQUIRE(a->second.advance > 0.0f);
    REQUIRE(a->second.width > 0.0f);
    REQUIRE(a->second.height > 0.0f);
    REQUIRE(a->second.u1 > a->second.u0);
    REQUIRE(a->second.v1 > a->second.v0);

    // Space advances the pen but has an empty quad.
    const auto sp = font->glyphs.find(static_cast<std::uint32_t>(' '));
    REQUIRE(sp != font->glyphs.end());
    REQUIRE(sp->second.advance > 0.0f);
    REQUIRE(sp->second.width == Approx(0.0f));
    REQUIRE(sp->second.height == Approx(0.0f));

    // RGBA8 atlas of the declared size.
    REQUIRE(font->atlas.width == 512u);
    REQUIRE(font->atlas.height > 0u);
    REQUIRE(font->atlas.pixels.size() ==
            static_cast<std::size_t>(font->atlas.width) * font->atlas.height * 4u);
}
```

Add to `tests/CMakeLists.txt` in the `add_executable(mosaic_unit_tests ...)` source list (near the other `unit/app/*` entries):

```cmake
  unit/app/font_cook_test.cpp
```

And define the fixture path — add right after the existing
`target_include_directories(mosaic_unit_tests ...)` line:

```cmake
target_compile_definitions(mosaic_unit_tests PRIVATE
  MOSAIC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
```

(`${CMAKE_CURRENT_SOURCE_DIR}` in `tests/CMakeLists.txt` is the `tests/` dir, so the
fixture resolves to `tests/fixtures/LiberationSans-Regular.ttf`.)

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `app/asset/font_cook.hpp` not found / `engine/ui/font_atlas.hpp` not found.

- [ ] **Step 4: Write the implementation**

Create `engine/ui/font_atlas.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <unordered_map>

#include "engine/render/image.hpp"

namespace mosaic::engine::ui
{
// One glyph's atlas placement (u*/v* normalized [0,1]) + layout metrics in atlas
// pixels (the atlas was baked at FontAtlas::pixel_size).
struct Glyph
{
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float advance = 0;
    float x_off = 0, y_off = 0; // pen-origin -> quad top-left
    float width = 0, height = 0;
};

// A baked bitmap font: an RGBA8 atlas (white; glyph coverage in ALPHA) + per-codepoint
// metrics + font-wide vertical metrics. Backend-free (the resource a "font" AssetID
// resolves to, like Image / AudioClip).
struct FontAtlas
{
    mosaic::engine::render::Image atlas;
    float pixel_size  = 0;
    float ascent      = 0;
    float descent     = 0; // negative (stb convention)
    float line_height = 0;
    std::unordered_map<std::uint32_t, Glyph> glyphs;
};
} // namespace mosaic::engine::ui
```

Create `app/asset/font_cook.hpp`:

```cpp
#pragma once

#include <optional>
#include <string>

#include "engine/ui/font_atlas.hpp"

namespace mosaic::app
{
// Rasterize ASCII 32..126 from a TTF/OTF byte blob at `pixel_size` into a FontAtlas.
// nullopt on an unparseable font. Deterministic for a given (bytes, pixel_size).
std::optional<mosaic::engine::ui::FontAtlas>
read_ttf(const std::string& bytes, float pixel_size);

// Serialize a FontAtlas to the "MOSAFNT1" cooked blob (see the plan's Global
// Constraints for the byte layout). Defined in this header (no stb needed).
std::string cook_font(const mosaic::engine::ui::FontAtlas& font);

// Inverse of cook_font. nullopt on bad magic, truncation, or a size mismatch.
std::optional<mosaic::engine::ui::FontAtlas> uncook_font(const std::string& bytes);
} // namespace mosaic::app
```

Create `app/asset/font_cook.cpp` (the sole stb TU + `read_ttf`):

```cpp
#include "app/asset/font_cook.hpp"

#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"

namespace mosaic::app
{
std::optional<mosaic::engine::ui::FontAtlas>
read_ttf(const std::string& bytes, float pixel_size)
{
    using mosaic::engine::ui::FontAtlas;
    using mosaic::engine::ui::Glyph;

    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    stbtt_fontinfo info;
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || stbtt_InitFont(&info, data, offset) == 0) { return std::nullopt; }

    const float scale = stbtt_ScaleForPixelHeight(&info, pixel_size);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);

    constexpr std::uint32_t kFirst = 32, kLast = 126;
    constexpr int kAtlasW = 512, kPad = 1;

    // Pass 1: metrics + shelf placement; compute atlas height.
    struct Placed { std::uint32_t cp; int gw, gh, px, py, x0, y0; float advance; };
    std::vector<Placed> placed;
    int cx = kPad, cy = kPad, row_h = 0;
    for (std::uint32_t cp = kFirst; cp <= kLast; ++cp)
    {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &adv, &lsb);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        int px = 0, py = 0;
        if (gw > 0 && gh > 0)
        {
            if (cx + gw + kPad > kAtlasW) { cx = kPad; cy += row_h + kPad; row_h = 0; }
            px = cx; py = cy;
            cx += gw + kPad;
            if (gh > row_h) { row_h = gh; }
        }
        placed.push_back(Placed{cp, gw, gh, px, py, x0, y0, adv * scale});
    }
    const int atlas_h = cy + row_h + kPad;
    if (atlas_h <= 0) { return std::nullopt; }

    FontAtlas font;
    font.pixel_size  = pixel_size;
    font.ascent      = asc * scale;
    font.descent     = desc * scale;
    font.line_height = (asc - desc + gap) * scale;
    font.atlas.width  = static_cast<std::uint32_t>(kAtlasW);
    font.atlas.height = static_cast<std::uint32_t>(atlas_h);
    font.atlas.pixels.assign(static_cast<std::size_t>(kAtlasW) * atlas_h * 4u, 0);
    // White everywhere; coverage goes in alpha.
    for (std::size_t i = 0; i < font.atlas.pixels.size(); i += 4)
    {
        font.atlas.pixels[i + 0] = 255;
        font.atlas.pixels[i + 1] = 255;
        font.atlas.pixels[i + 2] = 255;
        // alpha stays 0 until a glyph writes it
    }

    // Pass 2: rasterize coverage into the atlas alpha; fill the glyph map.
    for (const Placed& p : placed)
    {
        Glyph g;
        g.advance = p.advance;
        if (p.gw > 0 && p.gh > 0)
        {
            std::vector<unsigned char> cov(static_cast<std::size_t>(p.gw) * p.gh);
            stbtt_MakeCodepointBitmap(&info, cov.data(), p.gw, p.gh, p.gw, scale, scale,
                                      static_cast<int>(p.cp));
            for (int y = 0; y < p.gh; ++y)
            {
                for (int x = 0; x < p.gw; ++x)
                {
                    const std::size_t dst =
                        (static_cast<std::size_t>(p.py + y) * kAtlasW + (p.px + x)) * 4u + 3u;
                    font.atlas.pixels[dst] = cov[static_cast<std::size_t>(y) * p.gw + x];
                }
            }
            g.u0 = static_cast<float>(p.px) / kAtlasW;
            g.v0 = static_cast<float>(p.py) / atlas_h;
            g.u1 = static_cast<float>(p.px + p.gw) / kAtlasW;
            g.v1 = static_cast<float>(p.py + p.gh) / atlas_h;
            g.x_off  = static_cast<float>(p.x0);
            g.y_off  = static_cast<float>(p.y0);
            g.width  = static_cast<float>(p.gw);
            g.height = static_cast<float>(p.gh);
        }
        font.glyphs.emplace(p.cp, g);
    }
    return font;
}
} // namespace mosaic::app
```

> `cook_font`/`uncook_font` are added in Task 2 (they need no stb). This task leaves
> them undeclared-but-used? No — `font_cook.hpp` declares all three now, but only
> `read_ttf` is defined. `cook_font`/`uncook_font` are unused in Task 1's test, so the
> link succeeds (unused declarations don't force a definition). Task 2 defines them.

Add `font_cook.cpp` to `mosaic_app` in `app/CMakeLists.txt`:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/build.cpp
  run/import.cpp
  asset/font_cook.cpp
)
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "read_ttf bakes ASCII glyph metrics from a TTF" 2>&1 | tail -8`
Expected: PASS (1 test case).

- [ ] **Step 6: Commit**

```bash
git add third_party/stb/stb_truetype.h engine/ui/font_atlas.hpp \
        app/asset/font_cook.hpp app/asset/font_cook.cpp app/CMakeLists.txt \
        tests/fixtures/LiberationSans-Regular.ttf tests/fixtures/LiberationSans-License.txt \
        tests/unit/app/font_cook_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): FontAtlas resource + read_ttf glyph baker (stb_truetype, cook-time)"
```

---

### Task 2: `cook_font` / `uncook_font` round-trip

**Files:**
- Modify: `app/asset/font_cook.hpp` (define `cook_font`/`uncook_font` inline)
- Modify: `tests/unit/app/font_cook_test.cpp` (round-trip + reject tests)

**Interfaces:**
- Consumes: `read_ttf`, `FontAtlas` (Task 1).
- Produces: `std::string cook_font(const FontAtlas&)`, `std::optional<FontAtlas> uncook_font(const std::string&)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/app/font_cook_test.cpp`:

```cpp
#include "engine/ui/font_atlas.hpp"

using mosaic::app::cook_font;
using mosaic::app::uncook_font;

TEST_CASE("cook_font / uncook_font round-trip is bit-identical", "[app]")
{
    const std::optional<FontAtlas> font = read_ttf(fixture_ttf(), 48.0f);
    REQUIRE(font.has_value());

    const std::string blob = cook_font(*font);
    const std::optional<FontAtlas> back = uncook_font(blob);
    REQUIRE(back.has_value());

    REQUIRE(back->pixel_size == font->pixel_size);
    REQUIRE(back->ascent == font->ascent);
    REQUIRE(back->descent == font->descent);
    REQUIRE(back->line_height == font->line_height);
    REQUIRE(back->atlas.width == font->atlas.width);
    REQUIRE(back->atlas.height == font->atlas.height);
    REQUIRE(back->atlas.pixels == font->atlas.pixels); // exact bytes
    REQUIRE(back->glyphs.size() == font->glyphs.size());
    for (const auto& [cp, g] : font->glyphs)
    {
        const auto it = back->glyphs.find(cp);
        REQUIRE(it != back->glyphs.end());
        REQUIRE(it->second.u0 == g.u0);
        REQUIRE(it->second.v1 == g.v1);
        REQUIRE(it->second.advance == g.advance);
        REQUIRE(it->second.x_off == g.x_off);
        REQUIRE(it->second.y_off == g.y_off);
        REQUIRE(it->second.width == g.width);
        REQUIRE(it->second.height == g.height);
    }
}

TEST_CASE("uncook_font rejects garbage and truncation", "[app]")
{
    REQUIRE_FALSE(uncook_font("NOTAFONTblob").has_value());          // bad magic
    const std::optional<FontAtlas> font = read_ttf(fixture_ttf(), 48.0f);
    REQUIRE(font.has_value());
    const std::string good = cook_font(*font);
    REQUIRE_FALSE(uncook_font(good.substr(0, good.size() - 10)).has_value()); // truncated pixels
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — link error, `cook_font`/`uncook_font` undefined.

- [ ] **Step 3: Write the implementation**

Add to `app/asset/font_cook.hpp` — includes + inline definitions (no stb):

```cpp
#include <cstdint>
#include <cstring>
```

```cpp
namespace detail_font
{
inline void put_u32(std::string& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}
inline void put_f32(std::string& out, float f)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    put_u32(out, bits);
}
inline std::uint32_t get_u32(const std::string& in, std::size_t off)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(in[off])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 3])) << 24);
}
inline float get_f32(const std::string& in, std::size_t off)
{
    const std::uint32_t bits = get_u32(in, off);
    float f = 0;
    std::memcpy(&f, &bits, 4);
    return f;
}
} // namespace detail_font

inline std::string cook_font(const mosaic::engine::ui::FontAtlas& font)
{
    using namespace detail_font;
    std::string out;
    out += "MOSAFNT1";
    put_f32(out, font.pixel_size);
    put_f32(out, font.ascent);
    put_f32(out, font.descent);
    put_f32(out, font.line_height);
    put_u32(out, font.atlas.width);
    put_u32(out, font.atlas.height);
    put_u32(out, static_cast<std::uint32_t>(font.glyphs.size()));
    for (const auto& [cp, g] : font.glyphs)
    {
        put_u32(out, cp);
        put_f32(out, g.u0); put_f32(out, g.v0); put_f32(out, g.u1); put_f32(out, g.v1);
        put_f32(out, g.advance); put_f32(out, g.x_off); put_f32(out, g.y_off);
        put_f32(out, g.width); put_f32(out, g.height);
    }
    out.append(reinterpret_cast<const char*>(font.atlas.pixels.data()),
               font.atlas.pixels.size());
    return out;
}

inline std::optional<mosaic::engine::ui::FontAtlas> uncook_font(const std::string& bytes)
{
    using namespace detail_font;
    // magic(8) + 4 f32(16) + 3 u32(12) = 36-byte header minimum.
    if (bytes.size() < 36 || bytes.compare(0, 8, "MOSAFNT1") != 0) { return std::nullopt; }
    mosaic::engine::ui::FontAtlas font;
    std::size_t off = 8;
    font.pixel_size  = get_f32(bytes, off); off += 4;
    font.ascent      = get_f32(bytes, off); off += 4;
    font.descent     = get_f32(bytes, off); off += 4;
    font.line_height = get_f32(bytes, off); off += 4;
    font.atlas.width  = get_u32(bytes, off); off += 4;
    font.atlas.height = get_u32(bytes, off); off += 4;
    const std::uint32_t glyph_count = get_u32(bytes, off); off += 4;

    constexpr std::size_t kGlyphBytes = 4 + 9 * 4; // u32 cp + 9 f32
    const std::size_t px_bytes =
        static_cast<std::size_t>(font.atlas.width) * font.atlas.height * 4u;
    if (bytes.size() != off + glyph_count * kGlyphBytes + px_bytes) { return std::nullopt; }

    for (std::uint32_t i = 0; i < glyph_count; ++i)
    {
        const std::uint32_t cp = get_u32(bytes, off); off += 4;
        mosaic::engine::ui::Glyph g;
        g.u0 = get_f32(bytes, off); off += 4; g.v0 = get_f32(bytes, off); off += 4;
        g.u1 = get_f32(bytes, off); off += 4; g.v1 = get_f32(bytes, off); off += 4;
        g.advance = get_f32(bytes, off); off += 4;
        g.x_off = get_f32(bytes, off); off += 4; g.y_off = get_f32(bytes, off); off += 4;
        g.width = get_f32(bytes, off); off += 4; g.height = get_f32(bytes, off); off += 4;
        font.glyphs.emplace(cp, g);
    }
    font.atlas.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off), bytes.end());
    return font;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[app]" 2>&1 | tail -6`
Expected: all `[app]` tests PASS (incl. the two new font round-trip/reject cases).

- [ ] **Step 5: Commit**

```bash
git add app/asset/font_cook.hpp tests/unit/app/font_cook_test.cpp
git commit -m "feat(ui): cook_font/uncook_font MOSAFNT1 blob (byte-exact round-trip)"
```

---

### Task 3: import `.ttf` + load `"font"` branches

**Files:**
- Modify: `app/asset/asset_io.hpp` (`.ttf`/`.otf` import branch + `"font"` load branch)
- Create: `tests/unit/app/font_asset_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `read_ttf`, `cook_font`, `uncook_font`, `FontAtlas` (Tasks 1–2); the existing `import_asset` / `load_project_assets` in `asset_io.hpp`.
- Produces: `mosaic import <proj> font.ttf` → a `"font"` asset; `load_project_assets` registers a resolvable `FontAtlas`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/app/font_asset_test.cpp` (modeled on `tests/unit/app/audio_asset_test.cpp` — reuse its temp-dir + project-scaffold pattern):

```cpp
#include <filesystem>
#include <optional>
#include <string>

#include <unistd.h> // ::getpid

#include <catch2/catch_test_macros.hpp>

#include "app/asset/asset_io.hpp"
#include "app/asset/font_cook.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/font_atlas.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::create_project;
using mosaic::app::import_asset;
using mosaic::app::load_project_assets;
using mosaic::app::load_scene_file;
using mosaic::app::save_scene_file;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::ui::FontAtlas;
using mosaic::foundation::reflection::TypeRegistry;

TEST_CASE("import_asset cooks a .ttf into a font asset, and load registers a FontAtlas", "[app]")
{
    namespace fs = std::filesystem;

    // Scaffold a project (create_project needs a ComponentReflection, like audio_asset_test).
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    const std::string root = (fs::temp_directory_path() /
        ("mosaic_font_" + std::to_string(::getpid()))).string();
    fs::remove_all(root);
    REQUIRE(create_project(root, types));

    // Write the fixture TTF as the import source (outside the project tree).
    const std::string src = root + "_glyphs.ttf";
    const auto ttf = load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(ttf.has_value());
    REQUIRE(save_scene_file(src, *ttf));

    const std::optional<mosaic::foundation::serialization::AssetID> id =
        import_asset(root, src);
    REQUIRE(id.has_value());

    // The cooked blob + metadata exist under assets/ (source stem is the fixture file name).
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(root) / "assets" / (stem + ".font")));
    REQUIRE(fs::exists(fs::path(root) / "assets" / (stem + ".toml")));

    // Load registers a resolvable FontAtlas under the import id (resolver is load<T>).
    AssetRegistry registry;
    const int n = load_project_assets(registry, (fs::path(root) / "assets").string());
    REQUIRE(n >= 1);
    REQUIRE(registry.contains(*id));
    const std::shared_ptr<FontAtlas> font = registry.load<FontAtlas>(*id);
    REQUIRE(font != nullptr);
    REQUIRE(font->glyphs.size() == 95);

    fs::remove_all(root);
    fs::remove(src);
}
```

> Ground truth for the scaffold/resolve pattern is `tests/unit/app/audio_asset_test.cpp`
> (`create_project(dir, types)`, `registry.contains(id)`, `registry.load<T>(id)`). If any
> signature differs, match that file verbatim.

Add to `tests/CMakeLists.txt`:

```cmake
  unit/app/font_asset_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `import_asset` treats `.ttf` as the PPM fallback (rejects it → `nullopt`, so the `REQUIRE(id.has_value())` fails); no `.font` is written and `registry.load<FontAtlas>` would return null.

- [ ] **Step 3: Write the implementation**

In `app/asset/asset_io.hpp`, add the include near the other cook includes:

```cpp
#include "app/asset/font_cook.hpp"
```

Add a `.ttf`/`.otf` branch in `import_asset` — after the `.wav` branch, before the
`else` (PPM) fallback:

```cpp
    else if (ext == ".ttf" || ext == ".otf")
    {
        const std::optional<mosaic::engine::ui::FontAtlas> font = read_ttf(*src, 48.0f);
        if (!font)
        {
            mosaic::foundation::log::error("import", "not a supported font (TTF/OTF): {}", source_file);
            return std::nullopt;
        }
        blob_bytes = cook_font(*font);
        blob_ext   = ".font";
        asset_type = "font";
    }
```

Add a `"font"` branch in `load_project_assets` — after the `"audio"` branch, before the
image fallback:

```cpp
        if (meta.type == "font")
        {
            const fs::path font_path =
                entry.path().parent_path() / (entry.path().stem().string() + ".font");
            const std::optional<std::string> blob = load_scene_file(font_path.string());
            if (!blob) { mosaic::foundation::log::warn("import", "missing cooked font {}", font_path.string()); continue; }
            const std::optional<mosaic::engine::ui::FontAtlas> font = uncook_font(*blob);
            if (!font) { mosaic::foundation::log::warn("import", "corrupt cooked font {}", font_path.string()); continue; }
            registry.register_resource<mosaic::engine::ui::FontAtlas>(
                meta, std::make_shared<mosaic::engine::ui::FontAtlas>(*font));
            ++count;
            continue;
        }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[app]" 2>&1 | tail -6`
Expected: all `[app]` tests PASS, including the import→load font case.

- [ ] **Step 5: Commit**

```bash
git add app/asset/asset_io.hpp tests/unit/app/font_asset_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): import .ttf -> cooked .font asset; load registers a FontAtlas"
```

---

### Task 4: Verify full suite + update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Run the full suite + a real import round-trip**

```bash
cmake --build --preset debug 2>&1 | tail -5
ctest --preset debug 2>&1 | tail -4
# Real end-to-end: import the fixture font into a throwaway project.
./build/debug/app/mosaic create /tmp/mosaic_u2a_demo Demo 2>&1 | tail -2
./build/debug/app/mosaic import /tmp/mosaic_u2a_demo tests/fixtures/LiberationSans-Regular.ttf 2>&1 | tail -3
ls /tmp/mosaic_u2a_demo/assets/ ; rm -rf /tmp/mosaic_u2a_demo
```
Expected: full suite passes; the CLI import prints success and `assets/` contains
`LiberationSans-Regular.font` + `.toml`.

- [ ] **Step 2: Update the handoff**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (date `2026-09-08`) with a "UI — U2a (FontAtlas cooked asset) DONE" summary: `mosaic import font.ttf` bakes a glyph atlas + metrics (stb_truetype at cook time, walled in `font_cook.cpp`); neutral `FontAtlas` (RGBA8, coverage in alpha) registered as a `"font"` asset; `UiText` + text rendering is U2b.
2. In the UI-decomposition line, mark **U2a done** and note U2b (UiText + layout + glyph draw seam + Vulkan text pipeline + demo) is next; link the U2a spec + this plan.

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs: mark UI U2a (FontAtlas cooked asset) done; U2b is next"
```

---

## Self-Review

**Spec coverage:**
- `engine/ui/font_atlas.hpp` neutral `FontAtlas`/`Glyph` → Task 1. ✓
- `read_ttf` (stb, ASCII 32–126, shelf pack, coverage-in-alpha) → Task 1. ✓
- stb_truetype dependency, single TU, no stb above the wall → Task 1 (vendored header, `font_cook.cpp` only). ✓
- `cook_font`/`uncook_font` MOSAFNT1 byte-exact round-trip + reject → Task 2. ✓
- `import_asset` `.ttf`/`.otf` branch → Task 3. ✓
- `load_project_assets` `"font"` branch registering `FontAtlas` → Task 3. ✓
- `build_bundle` unchanged (already recursive-copies `assets/`) → no task needed (documented). ✓
- Tests: read_ttf metrics (Task 1), cook/uncook round-trip + reject (Task 2), import→load end-to-end (Task 3); fixture + `MOSAIC_TEST_DIR` (Task 1). ✓

**Deviation from spec (noted):** the spec left the stb dependency as "FetchContent mirroring miniaudio." The plan instead **vendors the single public-domain `stb_truetype.h`** into `third_party/stb/`. Rationale: stb has no release tags to pin cleanly, and a committed header is frozen → fully reproducible with no network at build time. Same module-wall outcome (one TU includes it). Deliberate improvement.

**Placeholder scan:** no TBD/TODO; every code step has real code. Task 1 vendoring uses a concrete `curl` from the canonical URL + a grep verification. Task 3's test sketch is backed by an explicit "copy audio_asset_test's verbatim pattern" instruction (the ground-truth file exists in-repo).

**Type consistency:** `FontAtlas{Image atlas; float pixel_size,ascent,descent,line_height; unordered_map<uint32_t,Glyph> glyphs}` and `Glyph{u0,v0,u1,v1,advance,x_off,y_off,width,height}` are identical across Tasks 1–3. `read_ttf(const std::string&, float) -> optional<FontAtlas>`, `cook_font(const FontAtlas&) -> string`, `uncook_font(const string&) -> optional<FontAtlas>` consistent. Asset type string `"font"` and blob ext `.font` consistent (Tasks 1/3). Magic `"MOSAFNT1"` consistent (Global Constraints + Task 2).
