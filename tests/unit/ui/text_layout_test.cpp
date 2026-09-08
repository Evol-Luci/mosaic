#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"   // read_ttf (baked fixture)
#include "app/run/scene_io.hpp"      // load_scene_file
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/text_layout.hpp"

using Catch::Approx;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::FontAtlas;
using mosaic::engine::ui::layout_text;
using mosaic::engine::ui::TextQuad;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

namespace
{
FontAtlas fixture_font()
{
    const auto bytes = mosaic::app::load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(bytes.has_value());
    const auto font = mosaic::app::read_ttf(*bytes, 48.0f);
    REQUIRE(font.has_value());
    return *font;
}
const Vec3 kWhite{1, 1, 1};
} // namespace

TEST_CASE("layout_text emits a quad per visible glyph, left to right", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto quads = layout_text(font, "AB", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f,
                                   kWhite, 1.0f);
    REQUIRE(quads.size() == 2);            // 'A' and 'B' both have ink
    REQUIRE(quads[1].rect.x > quads[0].rect.x); // B is to the right of A
    REQUIRE(quads[0].rect.w > 0.0f);
    REQUIRE(quads[0].rect.h > 0.0f);
    REQUIRE(quads[0].u1 > quads[0].u0);
    REQUIRE(quads[0].color.x == Approx(1.0f));
    REQUIRE(quads[0].alpha == Approx(1.0f));
}

TEST_CASE("layout_text skips a space's empty quad but still advances", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto ab  = layout_text(font, "AB",  Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    const auto asb = layout_text(font, "A B", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(asb.size() == 2);                       // space emits no quad
    REQUIRE(asb[1].rect.x > ab[1].rect.x);          // the space pushed B further right
}

TEST_CASE("layout_text narrows glyphs in X as aspect grows", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto wide = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 2.0f, kWhite, 1.0f);
    const auto sq   = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(wide[0].rect.w == Approx(sq[0].rect.w / 2.0f)); // sx = scale/aspect
    REQUIRE(wide[0].rect.h == Approx(sq[0].rect.h));        // height unaffected by aspect
}

TEST_CASE("layout_text right-anchors the measured box against the right edge", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto quads = layout_text(font, "AB", Anchor::TopRight, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(!quads.empty());
    // The last glyph's right edge sits at/inside the screen's right edge.
    const auto& last = quads.back();
    REQUIRE(last.rect.x + last.rect.w <= Approx(1.0f).margin(0.02f));
    REQUIRE(last.rect.x + last.rect.w > 0.85f);
}

TEST_CASE("layout_text ignores codepoints absent from the atlas", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    // '\t' (0x09) is outside ASCII 32..126, so it is not in the atlas.
    const auto quads = layout_text(font, "A\tB", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(quads.size() == 2); // only A and B
}
