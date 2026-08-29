#pragma once
#include <memory>
#include <unordered_map>
#include "engine/assets/asset_registry.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
// Resolves a mesh AssetID to a device MeshHandle, uploading via the registry's Mesh at
// most once per id. Nil/missing/failed -> 0. Mirrors TextureCache.
class MeshCache
{
public:
    MeshHandle get(mosaic::foundation::serialization::AssetID id,
                   mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device)
    {
        if (id == mosaic::foundation::serialization::AssetID{})
        {
            return 0;
        }
        auto it = cache_.find(id);
        if (it != cache_.end())
        {
            return it->second;
        }
        MeshHandle handle = 0;
        if (std::shared_ptr<Mesh> mesh = registry.load<Mesh>(id))
        {
            handle = device.create_mesh(mesh->vertices, mesh->indices);
        }
        cache_.emplace(id, handle);
        return handle;
    }

private:
    std::unordered_map<mosaic::foundation::serialization::AssetID, MeshHandle> cache_;
};
} // namespace mosaic::engine::render
