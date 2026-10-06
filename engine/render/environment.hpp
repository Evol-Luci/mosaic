#pragma once
#include <array>
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
struct Environment
{
    std::array<Image, 6>              cubemap;     // the sky, drawn by the skybox pass
    std::array<Image, 6>              irradiance;  // diffuse IBL, cosine-convolved
    std::vector<std::array<Image, 6>> prefiltered; // specular IBL, one level per roughness
};
} // namespace mosaic::engine::render
