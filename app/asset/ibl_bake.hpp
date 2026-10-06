#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/render/image.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::app
{
// Image-based lighting products, convolved from an environment cubemap on the CPU.
//
// On the CPU, and at import, for one reason above all: it makes the FURNACE TEST
// possible. A furnace test puts a surface in a uniform white environment, where a
// correct energy-conserving model returns exactly the input radiance. There is no GPU in
// CI, so had these integrals been shader code -- the conventional choice -- nothing could
// have checked them. PBR slice 2's spec recorded as its honest limit that no test there
// could show the BRDF correct, and named this as what would.
//
// Faces are in Vulkan's layer order throughout: +X, -X, +Y, -Y, +Z, -Z.

// Nearest-texel sample of a cubemap along `dir`. Nearest rather than bilinear is
// deliberate: these integrals take thousands of samples per output texel, so filtering
// costs a great deal and buys almost nothing once averaged.
mosaic::foundation::math::Vec3 sample_cubemap(
    const std::array<mosaic::engine::render::Image, 6>& faces,
    const mosaic::foundation::math::Vec3& dir);

// Diffuse irradiance: for each output texel, the cosine-weighted integral of incoming
// radiance over the hemisphere about that direction, divided by pi.
//
// 32 is the conventional size and is ample -- irradiance is a hemisphere average, so
// detail beyond a few texels per face is destroyed by the integral itself.
std::array<mosaic::engine::render::Image, 6> bake_irradiance(
    const std::array<mosaic::engine::render::Image, 6>& env, std::uint32_t size);

// Specular prefilter: level `m` holds the environment convolved with the GGX lobe at
// roughness m/(levels-1), so five levels are roughness 0, .25, .5, .75, 1 and the shader
// indexes them with roughness * 4 through textureLod.
//
// Level 0 is a RESAMPLE, not a convolution: roughness 0 is a mirror whose lobe is a delta
// function, so convolving it is an expensive way to reproduce the input. That also
// removes the costliest level, since cost falls with resolution thereafter.
std::vector<std::array<mosaic::engine::render::Image, 6>> bake_prefiltered(
    const std::array<mosaic::engine::render::Image, 6>& env, std::uint32_t base_size,
    std::uint32_t levels);
// The split-sum BRDF lookup table: red = the scale applied to F0, green = the bias added
// to it, indexed by (NdotV, roughness) across a `size` x `size` table.
//
// Deliberately NOT an asset. The integral is of the GGX BRDF over roughness and viewing
// angle and does not depend on the environment at all, so cooking it beside one would
// bake an identical copy into every environment. It costs milliseconds at startup.
//
// Note the geometry term uses the IBL remap k = alpha^2 / 2, NOT the direct-lighting
// k = alpha / 2 that cube.frag uses. The two look identical and using the wrong one is a
// silent energy error with no visible signature beyond "slightly off".
mosaic::engine::render::Image bake_brdf_lut(std::uint32_t size);

} // namespace mosaic::app
