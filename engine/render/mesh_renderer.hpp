#pragma once

#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::render
{
// Which mesh asset an entity draws. The mesh is referenced by AssetID (nil = none) and
// resolved to a device MeshHandle at render time via MeshCache — mirroring how
// Material.texture references an Image. Reflected, so it serializes with the scene.
struct MeshRenderer
{
    mosaic::foundation::serialization::AssetID mesh{}; // nil = nothing to draw
};

template <class V>
void reflect(V& visitor, MeshRenderer& mr)
{
    visitor.member("mesh", mr.mesh);
}
} // namespace mosaic::engine::render
