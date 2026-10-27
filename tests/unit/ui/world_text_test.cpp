#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"   // read_ttf (baked fixture)
#include "app/run/scene_io.hpp"      // load_scene_file
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/world_text.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::ui::build_world_text;
using mosaic::engine::ui::FontAtlas;
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
} // namespace

TEST_CASE("build_world_text emits camera-facing glyph triangles centered on the anchor", "[ui][world]")
{
    const FontAtlas font = fixture_font();
    const Vec3 anchor{0.0f, 0.0f, 5.0f};
    const auto verts = build_world_text(font, "AB", anchor, Vec3{1, 0, 0}, Vec3{0, 1, 0},
                                        1.0f, Vec3{1, 1, 1}, 1.0f);
    REQUIRE(verts.size() == 12); // 2 glyphs * 6 verts

    float min_x = 1e9f, max_x = -1e9f;
    for (const auto& v : verts)
    {
        REQUIRE(v.position.z == Approx(5.0f)); // cam_right=+X, cam_up=+Y -> z unchanged
        min_x = std::min(min_x, v.position.x);
        max_x = std::max(max_x, v.position.x);
    }
    REQUIRE(min_x < 0.0f); // centered on anchor.x = 0
    REQUIRE(max_x > 0.0f);
}

TEST_CASE("build_world_text returns nothing for empty text", "[ui][world]")
{
    const FontAtlas font = fixture_font();
    const auto verts = build_world_text(font, "", Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0},
                                        1.0f, Vec3{1, 1, 1}, 1.0f);
    REQUIRE(verts.empty());
}

// Added by U11b, and it pins a FACT rather than the arithmetic around it.
//
// U11b gave layout_text a trailing defaulted `wrap_width`, so world_text.hpp is untouched in
// that diff -- and its behaviour still changed, because '\n' became a line break
// UNCONDITIONALLY: `break_lines` splits at '\n' whatever the wrap width is, and the wrap flag
// gates only the width-based break. Before U11b, '\n' was < 0x20, so `resolve_glyph` returned
// nullptr and the place loop dropped it: genuinely one line. A defaulted parameter is exactly
// the shape of change a diff cannot show you reaching an untouched caller, which is why this
// is asserted here and not left to the reader.
//
// WHAT THIS DOES NOT ASSERT, deliberately: where the lines sit. `build_world_text` centres
// vertically on `cy0 = 0.5f`, "a local box ~1 line tall", so a multi-line block hangs below
// its anchor by half the extra height -- which is wrong, is recorded in the backlog, and must
// not be pinned by a test while it is still waiting on a design decision (does the billboard
// centre on the block or on the first line?). Pinning arithmetic already known to be wrong
// only makes it harder to fix.
TEST_CASE("build_world_text breaks a newline into a second line of glyphs", "[ui][world]")
{
    const FontAtlas font = fixture_font();
    auto rows = [&](const char* text)
    {
        const auto verts = build_world_text(font, text, Vec3{0, 0, 0}, Vec3{1, 0, 0},
                                            Vec3{0, 1, 0}, 1.0f, Vec3{1, 1, 1}, 1.0f);
        std::vector<float> ys; // distinct vertex Y rows: 2 per line of quads
        for (const auto& v : verts)
        {
            bool seen = false;
            for (const float y : ys) { if (y == v.position.y) { seen = true; break; } }
            if (!seen) { ys.push_back(v.position.y); }
        }
        return ys.size();
    };
    REQUIRE(rows("AB") == 2);     // one line: a top edge and a bottom edge
    REQUIRE(rows("A\nB") == 4);   // two lines, stacked: two edges each
}
