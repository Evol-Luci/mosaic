#pragma once

// Shared helpers for the textual shader-contract tests.
//
// CI has no GPU, so the only way to pin a shader-side guard is to read the source and
// assert the guard is in it. Both contract-test files need the same two helpers, and
// when they each carried their own copy they drifted: the tone-map file learned to strip
// comments before searching and the specular file did not, so a guard deleted from
// cube.frag but still described in a comment above it would have passed there. Found in
// review 2026-10-04. One copy, so a lesson learned in one file applies to both.

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace mosaic::tests
{

inline std::string read_shader(const std::string& name)
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/../modules/render_vulkan/shaders/" + name;
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The Vulkan backend's own source, for pinning the C++ half of a GPU-side contract.
//
// Lived in an anonymous namespace in texture_colorspace_contract_test.cpp, which made it
// invisible to every other contract file -- so the next file to need it would either
// duplicate it or file its cases in a test named after an unrelated decision. This header
// already exists because two copies of a helper drifted once; one copy, for the same
// reason.
inline std::string read_backend()
{
    const std::string path =
        std::string(MOSAIC_TEST_DIR) + "/../modules/render_vulkan/vulkan_render_device.cpp";
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Shader source with comments removed, both // and /* */.
//
// A raw text search cannot tell code from prose. The gamma check searches for a literal
// `2.2` and tripped on cube.frag's own comment explaining that a pow(c, 1.0/2.2) would
// double-correct; a test that forbids documenting the decision it enforces is the wrong
// test. The /* */ form is handled too -- not because any shader uses it today, but
// because a stripper that handles one comment syntax and not the other is a guard that
// works until someone reformats a comment.
inline std::string code_only(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    std::size_t i = 0;
    while (i < src.size())
    {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            while (i < src.size() && src[i] != '\n') { ++i; }
        }
        else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) { ++i; }
            i = (i + 1 < src.size()) ? i + 2 : src.size();
        }
        else
        {
            out += src[i];
            ++i;
        }
    }
    return out;
}

// Counts assignments to `name`, however the left-hand side is spelled.
//
// Matching the literal "outColor =" was evadable by exactly the edit the caller guards
// against: `outColor.rgb = vec3(fragNormal);` left the count at 1 with the mapped write
// still present, so the suite stayed green while the attachment received unmapped
// values. Closing that hole with a swizzle skip left the next spelling open --
// `outColor[0] = ...` -- which is the same defect one character along. Both review
// 2026-10-04. So skip a `.swizzle` OR a `[index]` before looking for a single `=`.
inline std::size_t count_assignments(const std::string& code, const std::string& name)
{
    std::size_t writes = 0;
    for (std::size_t i = code.find(name); i != std::string::npos; i = code.find(name, i + 1))
    {
        std::size_t j = i + name.size();
        if (j < code.size() && code[j] == '.')
        {
            ++j;
            while (j < code.size() && std::isalpha(static_cast<unsigned char>(code[j]))) { ++j; }
        }
        else if (j < code.size() && code[j] == '[')
        {
            while (j < code.size() && code[j] != ']') { ++j; }
            if (j < code.size()) { ++j; }
        }
        while (j < code.size() && (code[j] == ' ' || code[j] == '\t')) { ++j; }
        if (j < code.size() && code[j] == '=' && (j + 1 >= code.size() || code[j + 1] != '='))
        {
            ++writes;
        }
    }
    return writes;
}

// Collapses every run of whitespace to one space and trims the ends.
//
// Used to compare a block declared in two stages. Comparing raw bytes sounds stricter
// but is the wrong strictness: it fails when a comment is reworded in one stage only,
// and -- once comments are stripped -- on the trailing spaces the stripping leaves
// behind. What must agree is the sequence of field types and names; how it is laid out
// must not matter.
inline std::string normalize_ws(const std::string& src)
{
    std::string out;
    bool pending_space = false;
    for (char c : src)
    {
        if (std::isspace(static_cast<unsigned char>(c))) { pending_space = !out.empty(); continue; }
        if (pending_space) { out += ' '; pending_space = false; }
        out += c;
    }
    return out;
}

} // namespace mosaic::tests
