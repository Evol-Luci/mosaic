#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "engine/render/image.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::app
{
// Equirectangular-to-cubemap conversion, done on the CPU at import time.
//
// The alternative was converting on the GPU at load, which needs no new asset format --
// but none of the math would then be covered by any test, and CI has no GPU. Face
// orientation is the classic bug in this area: a wrong axis or a flipped V produces a
// rotated or mirrored sky, which reads as a questionable asset rather than a defect.
// Doing it here keeps the error-prone part inside what CI can actually check.
//
// Faces are in Vulkan's layer order throughout: +X, -X, +Y, -Y, +Z, -Z.

// The direction a face's (u, v) texel looks along. u and v run 0..1 across the face;
// the result is normalised, which equirect_to_uv relies on for its acos.
mosaic::foundation::math::Vec3 cube_face_direction(int face, float u, float v);

// Where a direction lands in an equirectangular image.
//   u = atan2(z, x) / 2pi + 0.5      v = acos(y) / pi
// +Y is the TOP of the equirect (v = 0) and -Y the bottom.
void equirect_to_uv(const mosaic::foundation::math::Vec3& dir, float& u, float& v);

// Six square faces of `face_size`, sampled bilinearly from `equirect`. Bilinear rather
// than nearest because even a 1:1 source lands between texels toward face edges, and
// nearest leaves visible stair-stepping along the diagonals. The faces carry the
// source's format.
std::array<mosaic::engine::render::Image, 6> equirect_to_cubemap(
    const mosaic::engine::render::Image& equirect, std::uint32_t face_size);

// Cooked on-disk form: "MOSAICUBE1" + u32 face size + u32 format + six faces.
std::string cook_cubemap(const std::array<mosaic::engine::render::Image, 6>& faces);

// Inverse of cook_cubemap. nullopt on a bad magic, an unknown format, or a payload
// whose length does not match six faces of the stated size.
std::optional<std::array<mosaic::engine::render::Image, 6>> uncook_cubemap(
    const std::string& bytes);
} // namespace mosaic::app
