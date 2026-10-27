#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mosaic::engine::ui
{
// U+FFFD REPLACEMENT CHARACTER. Produced for every malformed byte sequence, and the first
// choice of glyph for a printable codepoint the atlas does not have (see text_layout.hpp).
constexpr std::uint32_t kReplacementCodepoint = 0xFFFDu;

// Decodes `text` as UTF-8 into codepoints. See the U11a slice of the U11 spec.
//
// Malformed input is *replaced, not rejected*: every ill-formed sequence yields exactly one
// kReplacementCodepoint and decoding resumes at the next byte that could start a character,
// so a bad byte can never shorten or lengthen the string unboundedly. Rejected as ill-formed:
// a stray continuation byte (0x80-0xBF in lead position), an invalid lead byte (0xC0, 0xC1,
// 0xF5-0xFF), a sequence truncated by the end of the string or by a non-continuation byte, an
// overlong encoding, a UTF-16 surrogate (U+D800-U+DFFF), and anything above U+10FFFF.
//
// Every path advances the cursor by at least one byte and never reads past `text.size()`,
// which is what makes a truncated trailing sequence terminate instead of hanging.
inline std::vector<std::uint32_t> decode_utf8(const std::string& text)
{
    std::vector<std::uint32_t> out;
    out.reserve(text.size());

    const std::size_t n = text.size();
    std::size_t       i = 0;
    while (i < n)
    {
        const auto b0 = static_cast<unsigned char>(text[i]);

        // How many bytes this lead byte claims, and the codepoint bits it carries.
        int           extra = 0;
        std::uint32_t cp    = 0;
        std::uint32_t lo    = 0; // smallest codepoint legally encodable in this length
        if (b0 < 0x80u) { out.push_back(b0); ++i; continue; }      // ASCII fast path
        else if (b0 >= 0xC2u && b0 <= 0xDFu) { extra = 1; cp = b0 & 0x1Fu; lo = 0x80u; }
        else if (b0 >= 0xE0u && b0 <= 0xEFu) { extra = 2; cp = b0 & 0x0Fu; lo = 0x800u; }
        else if (b0 >= 0xF0u && b0 <= 0xF4u) { extra = 3; cp = b0 & 0x07u; lo = 0x10000u; }
        else
        {
            // 0x80-0xBF: a continuation byte where a lead byte belongs.
            // 0xC0, 0xC1: a 2-byte lead that can only ever encode an overlong ASCII value.
            // 0xF5-0xFF: beyond the U+10FFFF limit, or not a lead byte at all.
            out.push_back(kReplacementCodepoint);
            ++i;
            continue;
        }

        // Gather the continuations. A missing or non-continuation byte ends the sequence
        // early: emit one replacement and resume *at* the offending byte, which is the next
        // byte that could start a character.
        std::size_t j  = i + 1;
        bool        ok = true;
        for (int k = 0; k < extra; ++k)
        {
            if (j >= n) { ok = false; break; }                     // truncated at end of string
            const auto bc = static_cast<unsigned char>(text[j]);
            if ((bc & 0xC0u) != 0x80u) { ok = false; break; }      // not a continuation
            cp = (cp << 6) | (bc & 0x3Fu);
            ++j;
        }
        if (!ok)
        {
            out.push_back(kReplacementCodepoint);
            i = j; // j > i always, since the lead byte itself was consumed
            continue;
        }

        // Well-formedness of the decoded value itself.
        const bool overlong  = cp < lo;
        const bool surrogate = cp >= 0xD800u && cp <= 0xDFFFu;
        const bool too_big   = cp > 0x10FFFFu;
        out.push_back((overlong || surrogate || too_big) ? kReplacementCodepoint : cp);
        i = j;
    }
    return out;
}
} // namespace mosaic::engine::ui
