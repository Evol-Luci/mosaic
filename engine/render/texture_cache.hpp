#pragma once
#include <cstddef>
#include <functional>
#include <unordered_map>
#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
// Resolves a texture AssetID to a device TextureHandle, uploading via the registry's
// Image at most once per (id, colour space). Nil/missing/failed -> 0.
class TextureCache
{
public:
    TextureHandle get(mosaic::foundation::serialization::AssetID id,
                      mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device,
                      TextureColorSpace space = TextureColorSpace::Srgb)
    {
        if (id == mosaic::foundation::serialization::AssetID{})
        {
            return 0;
        }
        // Keyed by (id, space), not by id alone: one AssetID can legitimately be wanted
        // as both colour and data, and handing back the wrong upload is precisely the
        // distortion the space parameter exists to prevent.
        const Key key{id, space};
        auto it = cache_.find(key);
        if (it != cache_.end())
        {
            return it->second;
        }
        TextureHandle handle = 0;
        if (std::shared_ptr<Image> img = registry.load<Image>(id))
        {
            handle = device.create_texture(*img, space);
        }
        cache_.emplace(key, handle);
        return handle;
    }

private:
    struct Key
    {
        mosaic::foundation::serialization::AssetID id;
        TextureColorSpace space;
        bool operator==(const Key& o) const { return id == o.id && space == o.space; }
    };
    struct KeyHash
    {
        std::size_t operator()(const Key& k) const
        {
            return std::hash<mosaic::foundation::serialization::AssetID>{}(k.id)
                   ^ (static_cast<std::size_t>(k.space) << 1);
        }
    };
    std::unordered_map<Key, TextureHandle, KeyHash> cache_;
};
} // namespace mosaic::engine::render
