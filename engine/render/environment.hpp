#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "engine/render/image.hpp"

namespace mosaic::engine::render
{
// An environment and everything derived from it for image-based lighting.
//
// This lives in engine/ rather than beside the code that BAKES it (app/asset/ibl_bake)
// because RenderSystem resolves one, and the tier rules allow engine -> foundation only.
// Baking is an app-tier concern; the resource type is a rendering one.
//
// One type rather than three assets, deliberately: an AssetID resolves to exactly one
// resource, so splitting these would need a derived id or a sibling metadata entry --
// a concept added to the asset system for no gain.
// How many roughness levels the prefiltered chain holds.
//
// Load-bearing in two places that cannot see each other: both bake sites pass it, and
// cube.frag indexes the chain with `roughness * 4.0`, which is levels - 1. Changing this
// without changing the shader makes every specular reflection wrong by a level -- too
// sharp or too blurry -- with nothing to notice, because the baker is told the count and
// the shader is not. A contract test ties the two together.
inline constexpr std::uint32_t kPrefilteredLevels = 5;

// The base resolution of the prefiltered chain, and of the irradiance cube.
inline constexpr std::uint32_t kPrefilteredBaseSize = 128;
inline constexpr std::uint32_t kIrradianceSize = 32;

struct Environment
{
    std::array<Image, 6>              cubemap;     // the sky, drawn by the skybox pass
    std::array<Image, 6>              irradiance;  // diffuse IBL, cosine-convolved
    std::vector<std::array<Image, 6>> prefiltered; // specular IBL, one level per roughness
};
} // namespace mosaic::engine::render
