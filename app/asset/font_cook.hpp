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
