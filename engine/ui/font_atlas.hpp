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
