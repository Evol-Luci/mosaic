#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"   // read_ttf (baked fixture)
#include "app/run/scene_io.hpp"      // load_scene_file
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/text_layout.hpp"
#include "engine/ui/utf8.hpp"        // kReplacementCodepoint

// U11b -- multi-line and word-wrap. See
// docs/superpowers/specs/2026-10-27-u11b-wrap-plan.md, whose ORACLE is the first test case
// below: each line of a block must lay out in X exactly as if that line had been the whole
// single-line string. That is the per-line restatement of U11a's whole-string invariant, and
// it is one assertion rather than two because it is only satisfiable when the measure pass
// and the place pass agree PER LINE -- a line measured at one width and placed at another is
// anchored at `centre - w_measured/2` and inked over `w_placed`, so its glyphs move.

using Catch::Approx;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::FontAtlas;
using mosaic::engine::ui::layout_text;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::TextQuad;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

namespace
{
FontAtlas wrap_fixture_font()
{
    const auto bytes = mosaic::app::load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(bytes.has_value());
    const auto font = mosaic::app::read_ttf(*bytes, 48.0f);
    REQUIRE(font.has_value());
    return *font;
}

const Vec3 kWhite{1, 1, 1};
constexpr float kHeight = 0.1f;
constexpr float kAspect = 1.7777778f;

// The advance of one codepoint, in atlas pixels, straight from the atlas -- so a test can
// pick a wrap width in terms of "three A's but not four" without depending on the layout
// code it is about to exercise.
float advance_px(const FontAtlas& font, char c)
{
    const auto it = font.glyphs.find(static_cast<std::uint32_t>(c));
    REQUIRE(it != font.glyphs.end());
    return it->second.advance;
}

// `advance_px` atlas pixels expressed as a wrap_width, i.e. a fraction of `frame`'s width,
// for the fixed kHeight/kAspect the tests here use. Inverts layout_text's px -> wrap_width
// conversion exactly: max_advance = (wrap_width * frame.w) / sx, with
// sx = (height * frame.h / line_height) / aspect.
//
// THE FRAME'S HEIGHT IS IN THIS AND IT IS EASY TO FORGET -- a first draft dropped it, which
// made a nested-frame wrap width 1/frame.h too large so the text never wrapped at all. The
// oracle caught it, which is the first thing the oracle caught.
float wrap_fraction(const FontAtlas& font, float px, const Rect& frame = Rect{0, 0, 1, 1})
{
    const float sx = ((kHeight * frame.h) / font.line_height) / kAspect;
    return (px * sx) / frame.w;
}

// How many distinct baseline rows the quads occupy -- i.e. how many lines actually got ink.
std::size_t ink_rows(const std::vector<TextQuad>& quads)
{
    std::vector<float> ys;
    for (const auto& q : quads)
    {
        bool seen = false;
        for (const float y : ys) { if (y == q.rect.y) { seen = true; break; } }
        if (!seen) { ys.push_back(q.rect.y); }
    }
    return ys.size();
}

// Every anchor, so the horizontal band (left / centre / right) is exercised in all three
// values rather than only the one a single anchor happens to pick.
const Anchor kAllAnchors[] = {Anchor::TopLeft,    Anchor::Top,    Anchor::TopRight,
                              Anchor::Left,       Anchor::Center, Anchor::Right,
                              Anchor::BottomLeft, Anchor::Bottom, Anchor::BottomRight};
} // namespace

// ---------------------------------------------------------------------------- THE ORACLE
TEST_CASE("layout_text lays each line out in X as if that line were the whole string",
          "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    const Rect      frame{0.17f, 0.23f, 0.41f, 0.59f};

    struct Block
    {
        const char*                     text;
        float                           wrap_px_words; // 0 = no wrap; else "A"s that fit
        std::vector<std::string>        lines;
    };
    // Two shapes: explicit newlines with no wrap, and a wrap with no newlines. Both have a
    // SHORT line beside a LONG one, which is the only configuration in which anchoring a
    // line against the block's width differs from anchoring it against its own.
    const float a3_5 = 3.5f * advance_px(font, 'A') + advance_px(font, ' ');
    const std::vector<Block> blocks = {
        {"I\nWWWWWW",        0.0f,  {"I", "WWWWWW"}},
        {"WWWWWW\nI",        0.0f,  {"WWWWWW", "I"}},
        {"AA\nWWWWWW\nI",    0.0f,  {"AA", "WWWWWW", "I"}},
        {"AAA W",            a3_5,  {"AAA", "W"}},
    };

    for (const Block& b : blocks)
    {
        const float wrap =
            (b.wrap_px_words > 0.0f) ? wrap_fraction(font, b.wrap_px_words, frame) : 0.0f;
        for (const Anchor anchor : kAllAnchors)
        {
            const auto block = layout_text(font, b.text, anchor, Vec2{0.02f, 0.03f}, kHeight,
                                           kAspect, kWhite, 1.0f, frame, wrap);
            // The block must be exactly the concatenation, line by line, of each line laid
            // out alone -- in X. Y legitimately differs: line i sits i line-heights lower,
            // and a centred or bottom-anchored box moves when the line count changes.
            std::size_t at = 0;
            for (const std::string& line : b.lines)
            {
                const auto alone = layout_text(font, line, anchor, Vec2{0.02f, 0.03f}, kHeight,
                                               kAspect, kWhite, 1.0f, frame);
                REQUIRE(at + alone.size() <= block.size());
                for (std::size_t i = 0; i < alone.size(); ++i)
                {
                    REQUIRE(block[at + i].rect.x == Approx(alone[i].rect.x).margin(1e-7));
                    REQUIRE(block[at + i].rect.w == Approx(alone[i].rect.w).margin(1e-7));
                    REQUIRE(block[at + i].u0 == alone[i].u0); // same glyph, so same atlas cell
                }
                at += alone.size();
            }
            REQUIRE(at == block.size()); // no extra, no missing glyphs
        }
    }
}

// ------------------------------------------------------------------- GATE 1: never mid-char
TEST_CASE("layout_text breaks a long unspaced run between codepoints, never inside one",
          "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    // The fixture font provides no U+FFFD (see the resolve_glyph tests in
    // text_layout_test.cpp), so a printable codepoint absent from the atlas resolves to '?'.
    // That is what lets the two strings below be compared: they must resolve to the SAME
    // glyph sequence.
    REQUIRE(font.glyphs.find(mosaic::engine::ui::kReplacementCodepoint) == font.glyphs.end());

    std::string multi; // U+00C9 LATIN CAPITAL E WITH ACUTE, 8 times: 8 codepoints, 16 bytes
    for (int i = 0; i < 8; ++i) { multi += "\xC3\x89"; }
    const std::string ascii = "????????"; // the same 8 glyphs, one byte each
    REQUIRE(multi.size() == 16);
    REQUIRE(ascii.size() == 8);

    const float wrap = wrap_fraction(font, 3.5f * advance_px(font, '?'));
    const auto  qm = layout_text(font, multi, Anchor::TopLeft, Vec2{0, 0}, kHeight, kAspect,
                                 kWhite, 1.0f, Rect{0, 0, 1, 1}, wrap);
    const auto  qa = layout_text(font, ascii, Anchor::TopLeft, Vec2{0, 0}, kHeight, kAspect,
                                 kWhite, 1.0f, Rect{0, 0, 1, 1}, wrap);

    // 8 codepoints, three per line -> 3 lines and 8 quads. A per-BYTE wrap would see 16
    // units and give 16 quads over 6 lines, so both numbers are load-bearing.
    REQUIRE(qm.size() == 8);
    REQUIRE(ink_rows(qm) == 3);
    // And byte-for-byte the same as the single-byte string that resolves to the same glyphs:
    // a break inside a character would shift the rest of the line.
    REQUIRE(qm.size() == qa.size());
    for (std::size_t i = 0; i < qm.size(); ++i)
    {
        REQUIRE(qm[i].rect.x == qa[i].rect.x);
        REQUIRE(qm[i].rect.y == qa[i].rect.y);
        REQUIRE(qm[i].rect.w == qa[i].rect.w);
        REQUIRE(qm[i].rect.h == qa[i].rect.h);
    }
}

TEST_CASE("layout_text still places one codepoint on a line too narrow for any glyph",
          "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    // A wrap width a tenth of one glyph: nothing fits, and the forced break must still
    // advance or the line loop never terminates.
    const float wrap  = wrap_fraction(font, 0.1f * advance_px(font, 'A'));
    const auto  quads = layout_text(font, "ABC", Anchor::TopLeft, Vec2{0, 0}, kHeight, kAspect,
                                    kWhite, 1.0f, Rect{0, 0, 1, 1}, wrap);
    REQUIRE(quads.size() == 3);
    REQUIRE(ink_rows(quads) == 3); // one codepoint per line, none dropped
}

// --------------------------------------------- GATE 2/3: newlines, and lines x line_height
TEST_CASE("layout_text steps one line height down per newline", "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    const auto quads = layout_text(font, "A\nA\nA", Anchor::TopLeft, Vec2{0, 0}, kHeight,
                                   kAspect, kWhite, 1.0f);
    REQUIRE(quads.size() == 3);
    // Same glyph on every line, so the quad's y_off contribution is identical and the delta
    // is the line advance itself. One line height = height * frame.h = kHeight on the screen.
    REQUIRE(quads[1].rect.y - quads[0].rect.y == Approx(kHeight));
    REQUIRE(quads[2].rect.y - quads[1].rect.y == Approx(kHeight));
    REQUIRE(quads[0].rect.x == Approx(quads[1].rect.x)); // left-anchored: same pen origin
}

TEST_CASE("layout_text sizes the anchored block at lines x line height", "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    // Bottom-anchored, so the block's LOW edge is pinned and its height is observable: an
    // n-line block must start (n-1) line heights above a one-line block.
    auto at = [&](const char* t)
    {
        return layout_text(font, t, Anchor::BottomLeft, Vec2{0.0f, 0.1f}, kHeight, kAspect,
                           kWhite, 1.0f);
    };
    const auto one   = at("A");
    const auto two   = at("A\nA");
    const auto three = at("A\nA\nA");
    REQUIRE(one.size() == 1);
    REQUIRE(two.size() == 2);
    REQUIRE(three.size() == 3);
    REQUIRE(two[0].rect.y == Approx(one[0].rect.y - kHeight));
    REQUIRE(two[1].rect.y == Approx(one[0].rect.y));
    REQUIRE(three[0].rect.y == Approx(one[0].rect.y - 2.0f * kHeight));
    REQUIRE(three[2].rect.y == Approx(one[0].rect.y));

    // A TRAILING newline opens a line too -- "A\n" is two lines, the second empty -- so the
    // box is two lines tall and the ink moves up by one.
    const auto trailing = at("A\n");
    REQUIRE(trailing.size() == 1);
    REQUIRE(trailing[0].rect.y == Approx(one[0].rect.y - kHeight));
}

TEST_CASE("layout_text sizes a wrapped block at its line count too", "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    const float     wrap = wrap_fraction(font, 3.5f * advance_px(font, 'A')
                                                   + advance_px(font, ' '));
    const auto wrapped = layout_text(font, "AAA AAA", Anchor::BottomLeft, Vec2{0.0f, 0.1f},
                                     kHeight, kAspect, kWhite, 1.0f, Rect{0, 0, 1, 1}, wrap);
    const auto newlined = layout_text(font, "AAA\nAAA", Anchor::BottomLeft, Vec2{0.0f, 0.1f},
                                      kHeight, kAspect, kWhite, 1.0f);
    REQUIRE(wrapped.size() == 6);
    REQUIRE(ink_rows(wrapped) == 2);
    // A soft break DROPS the space it broke at, so the wrap is indistinguishable from the
    // explicit newline -- if the space survived onto the second line, "AAA" would start one
    // space further right.
    REQUIRE(wrapped.size() == newlined.size());
    for (std::size_t i = 0; i < wrapped.size(); ++i)
    {
        REQUIRE(wrapped[i].rect.x == newlined[i].rect.x);
        REQUIRE(wrapped[i].rect.y == newlined[i].rect.y);
    }
}

TEST_CASE("layout_text drops the whole run of spaces a soft break happened at",
          "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    const float     wrap = wrap_fraction(font, 3.5f * advance_px(font, 'A')
                                                   + advance_px(font, ' '));
    auto lay = [&](const char* t)
    {
        return layout_text(font, t, Anchor::TopLeft, Vec2{0, 0}, kHeight, kAspect, kWhite,
                           1.0f, Rect{0, 0, 1, 1}, wrap);
    };
    const auto one_space    = lay("AAA AAA");
    const auto three_spaces = lay("AAA   AAA");
    REQUIRE(one_space.size() == 6);
    REQUIRE(three_spaces.size() == 6);
    for (std::size_t i = 0; i < one_space.size(); ++i)
    {
        REQUIRE(three_spaces[i].rect.x == one_space[i].rect.x);
        REQUIRE(three_spaces[i].rect.y == one_space[i].rect.y);
    }
}

TEST_CASE("layout_text treats a carriage return before a newline as one break",
          "[ui][text][wrap]")
{
    const FontAtlas font = wrap_fixture_font();
    // '\r' keeps U11a's silent control-character skip, so CRLF is one break, not two.
    const auto crlf = layout_text(font, "A\r\nA", Anchor::TopLeft, Vec2{0, 0}, kHeight,
                                  kAspect, kWhite, 1.0f);
    const auto lf   = layout_text(font, "A\nA", Anchor::TopLeft, Vec2{0, 0}, kHeight,
                                  kAspect, kWhite, 1.0f);
    REQUIRE(crlf.size() == 2);
    REQUIRE(lf.size() == 2);
    for (std::size_t i = 0; i < crlf.size(); ++i)
    {
        REQUIRE(crlf[i].rect.x == lf[i].rect.x);
        REQUIRE(crlf[i].rect.y == lf[i].rect.y);
    }
}

TEST_CASE("layout_text ignores a wrap width of zero or less", "[ui][text][wrap]")
{
    const FontAtlas   font = wrap_fixture_font();
    const std::string lots = "AAAA AAAA AAAA AAAA AAAA AAAA";
    const auto none = layout_text(font, lots, Anchor::TopLeft, Vec2{0, 0}, kHeight, kAspect,
                                  kWhite, 1.0f);
    for (const float w : {0.0f, -0.5f})
    {
        const auto q = layout_text(font, lots, Anchor::TopLeft, Vec2{0, 0}, kHeight, kAspect,
                                   kWhite, 1.0f, Rect{0, 0, 1, 1}, w);
        REQUIRE(q.size() == none.size());
        REQUIRE(ink_rows(q) == 1);
        for (std::size_t i = 0; i < q.size(); ++i) { REQUIRE(q[i].rect.x == none[i].rect.x); }
    }
}

// ------------------------------------------------------------ GATE 5: today's labels, exactly
TEST_CASE("layout_text leaves a single-line unwrapped label bit-identical after U11b",
          "[ui][text][wrap]")
{
    // THE REGRESSION THIS SLICE COULD INTRODUCE AND THAT NOBODY WOULD SEE BY EYE: every label
    // authored today is single-line with no wrap width, so U11b must be a no-op for all of
    // them. These literals were DUMPED FROM THE PRE-U11b BINARY at %.9g (which round-trips a
    // float exactly) and are compared with ==, not Approx. They therefore predate the change,
    // and passing them is evidence rather than an assertion.
    //
    // The four cases cover both of the strings actually authored in
    // app/run/demo_scene.hpp ("HP" and "MOSAIC", at their authored anchor/pos/height), both
    // the screen frame and a nested frame, and all three horizontal anchor bands -- the band
    // is the new per-line alignment factor, so a band that is never exercised is a band whose
    // zero offset is unproven.
    const FontAtlas font = wrap_fixture_font();
    const Rect      kScreen{0.0f, 0.0f, 1.0f, 1.0f};
    const Rect      kSub{0.17f, 0.23f, 0.41f, 0.59f};
    struct Golden { float x, y, w, h, u0, v0, u1, v1; };

    auto check = [&](const char* label, const std::vector<TextQuad>& q,
                     const Golden* gold, std::size_t n)
    {
        INFO("golden case: " << label);
        REQUIRE(q.size() == n);
        for (std::size_t i = 0; i < n; ++i)
        {
            INFO("quad " << i);
            REQUIRE(q[i].rect.x == gold[i].x);
            REQUIRE(q[i].rect.y == gold[i].y);
            REQUIRE(q[i].rect.w == gold[i].w);
            REQUIRE(q[i].rect.h == gold[i].h);
            REQUIRE(q[i].u0 == gold[i].u0);
            REQUIRE(q[i].v0 == gold[i].v0);
            REQUIRE(q[i].u1 == gold[i].u1);
            REQUIRE(q[i].v1 == gold[i].v1);
        }
    };
    const Vec3  kDim{0.2f, 0.4f, 0.6f};
    const float kA = 1.7777778f;

    // "HP", as demo_scene.hpp authors it, inside a nested frame (band 0 = left).
    static const Golden kHpSub[] = {
        {0.200576037f, 0.371612549f, 0.0839669555f, 0.179129496f, 0.576171875f, 0.257485032f, 0.625f, 0.437125742f},
        {0.304789007f, 0.371612549f, 0.0806082785f, 0.179129496f, 0.943359375f, 0.257485032f, 0.990234375f, 0.437125742f},
    };
    check("HP/TopLeft/nested", layout_text(font, "HP", Anchor::TopLeft, Vec2{0.05f, 0.15f},
                                           0.5f, kA, kDim, 0.75f, kSub),
          kHpSub, 2);

    // "MOSAIC", as demo_scene.hpp authors its height, centred on the screen (band 1).
    static const Golden kMosaicCenter[] = {
        {0.444158435f, 0.480802506f, 0.0204936285f, 0.0364331193f, 0.771484375f, 0.257485032f, 0.830078125f, 0.437125742f},
        {0.467924356f, 0.480802506f, 0.0204936285f, 0.0376475565f, 0.8828125f, 0.257485032f, 0.94140625f, 0.443113774f},
        {0.490070879f, 0.480802506f, 0.0177611448f, 0.0376475565f, 0.1171875f, 0.491017967f, 0.16796875f, 0.676646709f},
        {0.508964181f, 0.480802506f, 0.0198105089f, 0.0364331193f, 0.20703125f, 0.257485032f, 0.263671875f, 0.437125742f},
        {0.530589998f, 0.480802506f, 0.00341560482f, 0.0364331193f, 0.626953125f, 0.257485032f, 0.63671875f, 0.437125742f},
        {0.538061321f, 0.480802506f, 0.0191273876f, 0.0376475565f, 0.314453125f, 0.257485032f, 0.369140625f, 0.443113774f},
    };
    check("MOSAIC/Center/screen", layout_text(font, "MOSAIC", Anchor::Center, Vec2{0.0f, 0.0f},
                                              0.06f, kA, kDim, 0.75f, kScreen),
          kMosaicCenter, 6);

    // Bottom-right in a nested frame (band 2), which is the case the new box_h would move if
    // `lines x height` were ever not exactly `height` for one line.
    static const Golden kMosaicBr[] = {
        {0.504697859f, 0.779173553f, 0.0120912418f, 0.0214955416f, 0.771484375f, 0.257485032f, 0.830078125f, 0.437125742f},
        {0.518719733f, 0.779173553f, 0.0120912418f, 0.0222120583f, 0.8828125f, 0.257485032f, 0.94140625f, 0.443113774f},
        {0.531786144f, 0.779173553f, 0.0104790768f, 0.0222120583f, 0.1171875f, 0.491017967f, 0.16796875f, 0.676646709f},
        {0.542933166f, 0.779173553f, 0.0116882008f, 0.0214955416f, 0.20703125f, 0.257485032f, 0.263671875f, 0.437125742f},
        {0.555692375f, 0.779173553f, 0.00201520696f, 0.0214955416f, 0.626953125f, 0.257485032f, 0.63671875f, 0.437125742f},
        {0.560100436f, 0.779173553f, 0.0112851588f, 0.0222120583f, 0.314453125f, 0.257485032f, 0.369140625f, 0.443113774f},
    };
    check("MOSAIC/BottomRight/nested",
          layout_text(font, "MOSAIC", Anchor::BottomRight, Vec2{0.02f, 0.02f}, 0.06f, kA,
                      kDim, 0.75f, kSub),
          kMosaicBr, 6);

    // A mixed-case string with spaces and digits, right-anchored in a nested frame.
    static const Golden kHudRight[] = {
        {0.314861089f, 0.6241225f, 0.0201520678f, 0.0358259007f, 0.771484375f, 0.257485032f, 0.830078125f, 0.437125742f},
        {0.337559193f, 0.631287694f, 0.0147781838f, 0.029854916f, 0.2734375f, 0.742514968f, 0.31640625f, 0.89221555f},
        {0.353610396f, 0.631287694f, 0.0127629768f, 0.029854916f, 0.4296875f, 0.742514968f, 0.466796875f, 0.89221555f},
        {0.368040979f, 0.631287694f, 0.0154499188f, 0.029854916f, 0.755859375f, 0.491017967f, 0.80078125f, 0.640718579f},
        {0.384763896f, 0.621734142f, 0.00335867796f, 0.0382142924f, 0.0859375f, 0.742514968f, 0.095703125f, 0.934131742f},
        {0.390504211f, 0.631287694f, 0.0134347118f, 0.029854916f, 0.845703125f, 0.491017967f, 0.884765625f, 0.640718579f},
        {0.414296806f, 0.6241225f, 0.0167933907f, 0.0358259007f, 0.576171875f, 0.257485032f, 0.625f, 0.437125742f},
        {0.435139388f, 0.6241225f, 0.0167933907f, 0.0370200947f, 0.22265625f, 0.491017967f, 0.271484375f, 0.676646709f},
        {0.45598197f, 0.6241225f, 0.0174651258f, 0.0358259007f, 0.37109375f, 0.257485032f, 0.421875f, 0.437125742f},
        {0.484843105f, 0.6241225f, 0.0127629768f, 0.0358259007f, 0.533203125f, 0.00598802418f, 0.5703125f, 0.185628742f},
        {0.500222564f, 0.6241225f, 0.0134347118f, 0.0358259007f, 0.572265625f, 0.00598802418f, 0.611328125f, 0.185628742f},
        {0.515602052f, 0.6241225f, 0.0147781838f, 0.0370200947f, 0.61328125f, 0.00598802418f, 0.65625f, 0.191616774f},
        {0.532996714f, 0.6241225f, 0.00403041393f, 0.0358259007f, 0.001953125f, 0.00598802418f, 0.013671875f, 0.185628742f},
    };
    check("Mosaic HUD 123!/Right/nested",
          layout_text(font, "Mosaic HUD 123!", Anchor::Right, Vec2{0.1f, 0.2f}, 0.1f, kA,
                      kDim, 0.75f, kSub),
          kHudRight, 13);
}
