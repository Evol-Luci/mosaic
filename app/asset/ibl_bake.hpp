#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/render/environment.hpp"
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
// Note the geometry term uses the IBL remap k = alpha / 2, where alpha is roughness
// squared -- the reference writes it as `float a = roughness; k = (a*a)/2`, whose `a` is
// ROUGHNESS. This file's `a` is already alpha, so squaring it again gives alpha^2/2, and
// that is exactly the bug that shipped until the slice-4c review: a silent energy error
// with no visible signature beyond "slightly off". ibl_bake.cpp's own comment at the
// remap carries the measurements. This comment used to describe the BUG as the design,
// which is how a bug comes back.
mosaic::engine::render::Image bake_brdf_lut(std::uint32_t size);

// The two weights of the Fdez-Aguera multiple-scattering reconstruction, which corrects
// the energy single-scatter GGX drops. `specular` multiplies the prefiltered radiance;
// `diffuse` multiplies the irradiance.
//
// THE GROUPING IS THE POINT. The multi-scatter term (FmsEms) is returned inside
// `diffuse`, not `specular`, because many bounces between microfacets randomize
// direction and the escaping energy is diffuse-like. Moving it to `specular` passes
// every furnace test in the suite -- in a uniform environment the two maps are
// identical -- and is wrong in every real scene.
//
// `diffuse_color` is the already-metallic-weighted albedo (`albedo * (1 - metallic)`),
// not the raw base colour. Passing raw albedo gives a dark conductor a diffuse lobe:
// the energy residual zeroes kD on its own only where albedo is 1.
//
// `scale` and `bias` are one texel of bake_brdf_lut, which is NOT modified by this --
// the correction is applied on top, at reconstruction time, because it depends on the
// per-material F0 while the table is indexed by (NdotV, roughness) alone.
struct EnergyCompensation
{
    mosaic::foundation::math::Vec3 specular; // FssEss      -> prefiltered radiance
    mosaic::foundation::math::Vec3 diffuse;  // FmsEms + kD -> irradiance
};

EnergyCompensation energy_compensate(const mosaic::foundation::math::Vec3& f0,
                                     const mosaic::foundation::math::Vec3& diffuse_color,
                                     float scale, float bias);

// Environment lives in engine/render/environment.hpp, not here: RenderSystem resolves one
// and the tier rules allow engine -> foundation only, so an app-tier type could not be
// named there. Baking is an app concern; the resource type is a rendering one.
using Environment = mosaic::engine::render::Environment;

// Cooked form: "MOSAICENV1" + u32 cubemap face size + u32 irradiance face size
// + u32 prefiltered base size + u32 level count + u32 format, then the six cubemap faces,
// the six irradiance faces, and each prefiltered level's six faces.
std::string cook_environment(const Environment& env);

// Inverse of cook_environment. nullopt on a bad magic, an unknown format, a level count
// the payload cannot hold, or any length mismatch.
std::optional<Environment> uncook_environment(const std::string& bytes);

} // namespace mosaic::app
