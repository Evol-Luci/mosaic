#pragma once

#include <unordered_map>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/render_device.hpp"
#include "engine/ui/font_atlas.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::render
{
// Resolves a "font" AssetID to a device TextureHandle by uploading the FontAtlas's
// RGBA8 atlas at most once per id. Nil/missing/failed -> 0. Mirrors TextureCache.
class FontTextureCache
{
public:
    TextureHandle get(mosaic::foundation::serialization::AssetID id,
                      mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device)
    {
        if (id == mosaic::foundation::serialization::AssetID{}) { return 0; }
        auto it = cache_.find(id);
        if (it != cache_.end()) { return it->second; }
        TextureHandle handle = 0;
        if (std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                registry.load<mosaic::engine::ui::FontAtlas>(id))
        {
            handle = device.create_texture(font->atlas);
        }
        cache_.emplace(id, handle);
        return handle;
    }

private:
    std::unordered_map<mosaic::foundation::serialization::AssetID, TextureHandle> cache_;
};
} // namespace mosaic::engine::render
