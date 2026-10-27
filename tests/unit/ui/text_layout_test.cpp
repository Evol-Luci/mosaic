#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"   // read_ttf (baked fixture)
#include "app/run/scene_io.hpp"      // load_scene_file
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/text_layout.hpp"
#include "engine/ui/utf8.hpp"   // kReplacementCodepoint

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

TEST_CASE("layout_text draws nothing at all for a control character", "[ui][text]")
{
    // This case used to be "layout_text ignores codepoints absent from the atlas" and pinned
    // the pre-U11a behaviour that EVERY unknown codepoint was dropped. U11a deliberately
    // changed that: a printable unknown now gets a replacement glyph (see the next case).
    // What survives is the control-character half -- a settled decision, because a visible
    // mark where the author meant whitespace would be worse than dropping it.
    const FontAtlas font = fixture_font();
    const auto with_tab = layout_text(font, "A\tB", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(with_tab.size() == 2); // only A and B

    // It advances nothing either, so the tab is invisible to the measure pass as well: the
    // quads are identical to the ones "AB" produces.
    const auto plain = layout_text(font, "AB", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(plain.size() == with_tab.size());
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        REQUIRE(with_tab[i].rect.x == plain[i].rect.x);
        REQUIRE(with_tab[i].rect.w == plain[i].rect.w);
    }

    // A string of nothing but control characters lays out to nothing, as does the empty one.
    REQUIRE(layout_text(font, "\t\r\n\x01\x7F", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f).empty());
    REQUIRE(layout_text(font, "", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f).empty());
}

TEST_CASE("layout_text draws a replacement glyph for a printable codepoint absent from the atlas", "[ui][text]")
{
    const FontAtlas font = fixture_font();

    // THE MEASUREMENT this slice rests on: the bundled font (which is byte-identical to this
    // fixture) has no U+FFFD glyph, so the cook bakes none and the resolution order falls
    // through to '?'. If a future font or a widened cook changes that, THIS assertion is the
    // one that should fail first and tell you to update the two below it.
    REQUIRE(font.glyphs.find(mosaic::engine::ui::kReplacementCodepoint) == font.glyphs.end());
    REQUIRE(font.glyphs.size() == 95u); // printable ASCII only: no U+FFFD was added

    // "Caf" + U+00E9 is five bytes and four codepoints. Pre-U11a this produced THREE quads:
    // both bytes of the e-acute were dropped, by the place pass and the measure pass alike.
    const auto quads = layout_text(font, "Caf\xC3\xA9", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f,
                                   kWhite, 1.0f);
    REQUIRE(quads.size() == 4);

    // The fourth quad is the fallback glyph, which for this font is '?'.
    const auto qm = font.glyphs.find(static_cast<std::uint32_t>('?'));
    REQUIRE(qm != font.glyphs.end());
    REQUIRE(quads[3].u0 == qm->second.u0);
    REQUIRE(quads[3].v0 == qm->second.v0);
    REQUIRE(quads[3].u1 == qm->second.u1);
    REQUIRE(quads[3].v1 == qm->second.v1);
}

TEST_CASE("layout_text measures the replacement glyph as well as placing it", "[ui][text]")
{
    // The measure/place agreement the spec's gate asks for made observable. Anchored Centre,
    // the box is positioned from the measured total advance, so if the measure pass skipped
    // the replacement while the place pass drew it, every glyph would shift left by half a
    // '?' and this comparison would fail -- the count assertion alone would not catch it.
    const FontAtlas font = fixture_font();
    const auto with_accent = layout_text(font, "Caf\xC3\xA9", Anchor::Center, Vec2{0, 0}, 0.1f,
                                         1.0f, kWhite, 1.0f);
    const auto with_qmark  = layout_text(font, "Caf?", Anchor::Center, Vec2{0, 0}, 0.1f,
                                         1.0f, kWhite, 1.0f);
    REQUIRE(with_accent.size() == 4);
    REQUIRE(with_qmark.size() == with_accent.size());
    for (std::size_t i = 0; i < with_qmark.size(); ++i)
    {
        REQUIRE(with_accent[i].rect.x == with_qmark[i].rect.x);
        REQUIRE(with_accent[i].rect.y == with_qmark[i].rect.y);
        REQUIRE(with_accent[i].rect.w == with_qmark[i].rect.w);
        REQUIRE(with_accent[i].rect.h == with_qmark[i].rect.h);
    }
}

TEST_CASE("layout_text gives a malformed sequence one replacement and does not hang", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto one = [&](const std::string& t) {
        return layout_text(font, t, Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f).size();
    };
    REQUIRE(one("A\xC3") == 2);          // truncated at the end of the string: 'A' + one mark
    REQUIRE(one("A\xFF" "B") == 3);      // invalid lead byte between two letters
    REQUIRE(one("A\xA9" "B") == 3);      // a continuation byte where a lead belongs
    REQUIRE(one("\xED\xA0\x80") == 1); // a surrogate: rejected, one mark
    REQUIRE(one(std::string(32, '\xFF')) == 32); // terminates; one mark per bad byte
}

TEST_CASE("layout_text is a byte-for-byte no-op for an ASCII string", "[ui][text]")
{
    // The regression U11a could introduce and that nobody would see by eye, since every label
    // authored today is ASCII. These literals were dumped from layout_text BEFORE the UTF-8
    // decode landed (%.9g, which round-trips a float exactly) and are compared with ==, not
    // Approx: any change in the arithmetic at all should fail here.
    const FontAtlas font = fixture_font();
    const auto quads = layout_text(font, "Mosaic HUD 123!", Anchor::TopLeft, Vec2{0.1f, 0.2f},
                                   0.1f, 1.7777778f, Vec3{0.2f, 0.4f, 0.6f}, 0.75f);
    struct Golden { float x, y, w, h, u0, v0, u1, v1; };
    static const Golden kGolden[] = {
        {0.103415608f, 0.218004256f, 0.0341560543f, 0.0607218742f, 0.771484375f, 0.257485032f, 0.830078125f, 0.437125742f},
        {0.14188695f,  0.230148628f, 0.0250477716f, 0.0506015606f, 0.2734375f,   0.742514968f, 0.31640625f,  0.89221555f},
        {0.169092372f, 0.230148628f, 0.0216321666f, 0.0506015606f, 0.4296875f,   0.742514968f, 0.466796875f, 0.89221555f},
        {0.193550974f, 0.230148628f, 0.0261863079f, 0.0506015606f, 0.755859375f, 0.491017967f, 0.80078125f,  0.640718579f},
        {0.221894935f, 0.213956133f, 0.00569267571f,0.0647699982f, 0.0859375f,   0.742514968f, 0.095703125f, 0.934131742f},
        {0.231624231f, 0.230148628f, 0.0227707028f, 0.0506015606f, 0.845703125f, 0.491017967f, 0.884765625f, 0.640718579f},
        {0.271950662f, 0.218004256f, 0.0284633767f, 0.0607218742f, 0.576171875f, 0.257485032f, 0.625f,       0.437125742f},
        {0.307277083f, 0.218004256f, 0.0284633767f, 0.0627459362f, 0.22265625f,  0.491017967f, 0.271484375f, 0.676646709f},
        {0.342603505f, 0.218004256f, 0.0296019129f, 0.0607218742f, 0.37109375f,  0.257485032f, 0.421875f,    0.437125742f},
        {0.391520679f, 0.218004256f, 0.0216321666f, 0.0607218742f, 0.533203125f, 0.00598802418f, 0.5703125f, 0.185628742f},
        {0.417587548f, 0.218004256f, 0.0227707028f, 0.0607218742f, 0.572265625f, 0.00598802418f, 0.611328125f, 0.185628742f},
        {0.443654418f, 0.218004256f, 0.0250477716f, 0.0627459362f, 0.61328125f,  0.00598802418f, 0.65625f,   0.191616774f},
        {0.473136902f, 0.218004256f, 0.00683121057f,0.0607218742f, 0.001953125f, 0.00598802418f, 0.013671875f, 0.185628742f},
    };
    REQUIRE(quads.size() == 13); // 15 codepoints; the two spaces have no ink
    for (std::size_t i = 0; i < quads.size(); ++i)
    {
        REQUIRE(quads[i].rect.x == kGolden[i].x);
        REQUIRE(quads[i].rect.y == kGolden[i].y);
        REQUIRE(quads[i].rect.w == kGolden[i].w);
        REQUIRE(quads[i].rect.h == kGolden[i].h);
        REQUIRE(quads[i].u0 == kGolden[i].u0);
        REQUIRE(quads[i].v0 == kGolden[i].v0);
        REQUIRE(quads[i].u1 == kGolden[i].u1);
        REQUIRE(quads[i].v1 == kGolden[i].v1);
        REQUIRE(quads[i].color.x == 0.2f);
        REQUIRE(quads[i].color.y == 0.4f);
        REQUIRE(quads[i].color.z == 0.6f);
        REQUIRE(quads[i].alpha == 0.75f);
    }
}

TEST_CASE("layout_text offsets glyph quads into a non-screen frame", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const mosaic::engine::ui::Rect frame{0.5f, 0.5f, 0.5f, 0.5f};
    const auto full  = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    const auto inset = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f, frame);
    REQUIRE(!inset.empty());
    // The frame's origin is (0.5,0.5), so the inset glyph sits to the lower-right of the
    // screen-space one and is half the width.
    REQUIRE(inset[0].rect.x > full[0].rect.x);
    REQUIRE(inset[0].rect.w == Approx(full[0].rect.w * 0.5f));
}
