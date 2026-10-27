#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/utf8.hpp"

using mosaic::engine::ui::decode_utf8;
using mosaic::engine::ui::kReplacementCodepoint;

namespace
{
constexpr std::uint32_t kRep = kReplacementCodepoint;
using CPs = std::vector<std::uint32_t>;
} // namespace

TEST_CASE("decode_utf8 returns nothing for the empty string", "[ui][text][utf8]")
{
    REQUIRE(decode_utf8("").empty());
}

TEST_CASE("decode_utf8 passes ASCII through one codepoint per byte", "[ui][text][utf8]")
{
    // Every byte below 0x80 decodes to itself, control characters included -- rejecting them
    // is layout's job (it skips them) and not the decoder's.
    REQUIRE(decode_utf8("Hi!") == CPs{'H', 'i', '!'});
    REQUIRE(decode_utf8(std::string("\t\r\n")) == CPs{0x09u, 0x0Du, 0x0Au});
    REQUIRE(decode_utf8(std::string("\x7F")) == CPs{0x7Fu});
}

TEST_CASE("decode_utf8 decodes two- three- and four-byte sequences", "[ui][text][utf8]")
{
    // "Cafe" + U+00E9 (C3 A9): four codepoints from five bytes. The bug U11a fixes is that
    // per-byte iteration saw the last two bytes as two unknown characters and dropped both.
    const std::string cafe = "Caf\xC3\xA9";
    REQUIRE(cafe.size() == 5u);
    REQUIRE(decode_utf8(cafe) == CPs{'C', 'a', 'f', 0x00E9u});

    REQUIRE(decode_utf8("\xE2\x82\xAC") == CPs{0x20ACu});          // U+20AC euro sign
    REQUIRE(decode_utf8("\xF0\x9D\x84\x9E") == CPs{0x1D11Eu});     // U+1D11E treble clef
    REQUIRE(decode_utf8("\xF4\x8F\xBF\xBF") == CPs{0x10FFFFu});    // the highest codepoint
}

TEST_CASE("decode_utf8 replaces a sequence truncated by the end of the string", "[ui][text][utf8]")
{
    // The case that reads past the end or loops forever if the bounds are wrong: a lead byte
    // promising continuations that the string does not contain. One replacement each time.
    REQUIRE(decode_utf8("A\xC3") == CPs{'A', kRep});               // 2-byte lead, 0 of 1
    REQUIRE(decode_utf8("A\xE2") == CPs{'A', kRep});               // 3-byte lead, 0 of 2
    REQUIRE(decode_utf8("A\xE2\x82") == CPs{'A', kRep});           // 3-byte lead, 1 of 2
    REQUIRE(decode_utf8("A\xF0\x9D\x84") == CPs{'A', kRep});       // 4-byte lead, 2 of 3
    REQUIRE(decode_utf8("\xC3") == CPs{kRep});                     // the whole string
}

TEST_CASE("decode_utf8 replaces an invalid lead byte and resumes at the next byte", "[ui][text][utf8]")
{
    // 0xFF and 0xF5 can never lead a sequence; 0xC0/0xC1 could only ever lead an overlong.
    REQUIRE(decode_utf8("A\xFF" "B") == CPs{'A', kRep, 'B'});
    REQUIRE(decode_utf8("A\xC0" "B") == CPs{'A', kRep, 'B'});
    REQUIRE(decode_utf8("A\xC1" "B") == CPs{'A', kRep, 'B'});
    // 0xF5 is rejected as a lead; its three would-be continuations are then each a stray
    // continuation byte in lead position, so this is four replacements, not one.
    REQUIRE(decode_utf8("\xF5\x80\x80\x80") == CPs{kRep, kRep, kRep, kRep});
}

TEST_CASE("decode_utf8 replaces a continuation byte in lead position", "[ui][text][utf8]")
{
    REQUIRE(decode_utf8("A\x80" "B") == CPs{'A', kRep, 'B'});
    REQUIRE(decode_utf8("A\xA9" "B") == CPs{'A', kRep, 'B'});      // the tail of U+00E9 alone
    REQUIRE(decode_utf8("A\xBF" "B") == CPs{'A', kRep, 'B'});
}

TEST_CASE("decode_utf8 resumes at a non-continuation byte that interrupts a sequence", "[ui][text][utf8]")
{
    // One replacement for the broken sequence, then the interrupting byte is decoded as the
    // start of a character in its own right -- it is not swallowed.
    REQUIRE(decode_utf8("A\xC3" "B") == CPs{'A', kRep, 'B'});
    REQUIRE(decode_utf8("A\xE2\x82" "B") == CPs{'A', kRep, 'B'});
    // The interrupting byte may itself be a valid lead.
    REQUIRE(decode_utf8("\xE2\x82\xC3\xA9") == CPs{kRep, 0x00E9u});
}

TEST_CASE("decode_utf8 rejects overlong encodings", "[ui][text][utf8]")
{
    // C0 AF and C1 BF are overlong '/' and DEL; both leads are rejected outright, so each
    // yields a replacement for the lead plus one for the orphaned continuation.
    REQUIRE(decode_utf8("\xC0\xAF") == CPs{kRep, kRep});
    // A three- or four-byte lead has to be decoded before the overlong can be seen, so these
    // are a single replacement for the whole sequence.
    REQUIRE(decode_utf8("\xE0\x80\xAF") == CPs{kRep});             // overlong '/'
    REQUIRE(decode_utf8("\xE0\x9F\xBF") == CPs{kRep});             // overlong U+07FF
    REQUIRE(decode_utf8("\xF0\x80\x80\xAF") == CPs{kRep});         // overlong '/'
    REQUIRE(decode_utf8("\xF0\x8F\xBF\xBF") == CPs{kRep});         // overlong U+FFFF
}

TEST_CASE("decode_utf8 rejects surrogates and codepoints above U+10FFFF", "[ui][text][utf8]")
{
    REQUIRE(decode_utf8("\xED\xA0\x80") == CPs{kRep});             // U+D800 high surrogate
    REQUIRE(decode_utf8("\xED\xBF\xBF") == CPs{kRep});             // U+DFFF low surrogate
    REQUIRE(decode_utf8("\xF4\x90\x80\x80") == CPs{kRep});         // U+110000 just over the top
}

TEST_CASE("decode_utf8 decodes a literal U+FFFD as itself", "[ui][text][utf8]")
{
    // EF BF BD is well-formed, so it is not a replacement *for* anything -- but it is
    // indistinguishable in the output from one, which is the accepted v1 cost.
    REQUIRE(decode_utf8("\xEF\xBF\xBD") == CPs{kRep});
}

TEST_CASE("decode_utf8 terminates on a string of nothing but bad bytes", "[ui][text][utf8]")
{
    // Every path advances at least one byte, so a pathological string cannot hang. If this
    // test hangs rather than fails, the cursor advance is the thing to look at.
    const std::string junk(64, '\xFF');
    const auto        cps = decode_utf8(junk);
    REQUIRE(cps.size() == 64u);
    for (const std::uint32_t cp : cps) { REQUIRE(cp == kRep); }

    const std::string trailing = std::string("ok") + std::string(8, '\xF0');
    // Each 0xF0 is a 4-byte lead interrupted by the next 0xF0 (not a continuation), so each
    // of the eight yields exactly one replacement -- the last one by running off the end.
    REQUIRE(decode_utf8(trailing).size() == 10u);
}
