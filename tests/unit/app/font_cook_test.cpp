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

TEST_CASE("read_ttf bakes U+FFFD only when the font provides it", "[app]")
{
    // U11a adds U+FFFD to the cooked set as a one-codepoint addition beside 32..126, gated on
    // stbtt_FindGlyphIndex. MEASURED for the bundled font (assets/fonts/DemoSans.ttf, which is
    // byte-identical to this fixture -- same md5): FindGlyphIndex(U+FFFD) returns 0, so the
    // font does NOT provide it, nothing extra is baked, and layout_text's resolution order
    // falls through to '?'. This asserts that measured outcome.
    //
    // The gate must be the glyph index, not the bitmap box: stb resolves a missing codepoint
    // to glyph 0 (.notdef), and in this font .notdef is a drawn 24x30 box with a real advance.
    // A bbox-based gate would therefore bake .notdef as if it were U+FFFD.
    const std::optional<FontAtlas> font = read_ttf(fixture_ttf(), 48.0f);
    REQUIRE(font.has_value());
    REQUIRE(font->glyphs.size() == 95);  // 32..126 and nothing else
    REQUIRE(font->glyphs.find(0xFFFDu) == font->glyphs.end());
}

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
