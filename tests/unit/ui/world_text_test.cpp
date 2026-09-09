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
