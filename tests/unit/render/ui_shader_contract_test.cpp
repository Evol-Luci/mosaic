// Pins the two lines that carry a UiImage's sub-region UVs onto the GPU.
//
// CI has no GPU, so both halves are source contracts, the same idiom as
// shader_specular_contract_test.cpp and texture_colorspace_contract_test.cpp.
//
// HONEST LIMIT, stated because this project has been burned by the opposite assumption:
// a textual pin asserts that a line EXISTS, never that it is correct. A previous slice
// shipped a feature that was entirely dead while thirteen green tests stayed green,
// because the pin asserted the presence of the very call that broke it. What these cases
// do buy is the one failure they are shaped for: the UVs arriving and being ignored.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::code_only;
using mosaic::tests::normalize_ws;
using mosaic::tests::read_backend;
using mosaic::tests::read_shader;

TEST_CASE("ui_image.vert maps UVs through the push constant rather than the corner",
          "[render]")
{
    // Before sub-rects, ui_image.vert set `v_uv = c` -- the raw corner, i.e. always the
    // whole texture. That line is the one a sub-rect replaces, and leaving it would make
    // the UVs this slice computes arrive and do nothing: every image would keep rendering
    // the full texture, with the component fields, the cache, the maths and the push
    // constant all correct and all ignored.
    //
    // The same mix() form ui_text.vert already uses, so the two read alike.
    const std::string src = normalize_ws(code_only(read_shader("ui_image.vert")));
    INFO("If this fails, check that v_uv interpolates pc.uv rather than the raw corner.");
    REQUIRE(src.find("v_uv = vec2(mix(pc.uv.x, pc.uv.z, c.x), mix(pc.uv.y, pc.uv.w, c.y));")
            != std::string::npos);
    // And that the push block actually declares it.
    REQUIRE(src.find("vec4 uv;") != std::string::npos);
    // The raw-corner assignment must be GONE, not merely shadowed by a later line.
    REQUIRE(src.find("v_uv = c;") == std::string::npos);
}

TEST_CASE("the ui-image push constant copies all four UV floats", "[render]")
{
    const std::string src = normalize_ws(code_only(read_backend()));
    REQUIRE(src.find("pc.uv[0] = q.u0;") != std::string::npos);
    REQUIRE(src.find("pc.uv[1] = q.v0;") != std::string::npos);
    REQUIRE(src.find("pc.uv[2] = q.u1;") != std::string::npos);
    REQUIRE(src.find("pc.uv[3] = q.v1;") != std::string::npos);
    // The size assertion lives in the backend as a static_assert, so it cannot drift from
    // the struct. This pins that the declared size moved WITH the new field: 32 would mean
    // the shader's 48-byte block disagrees with the pipeline layout's range, which is the
    // one class of mistake here that Vulkan validation does report.
    REQUIRE(src.find("static_assert(sizeof(UiImagePush) == 48,") != std::string::npos);
}
