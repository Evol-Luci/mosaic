#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/texture_cache.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureCache;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::foundation::serialization::AssetID;

namespace
{
class CountingDevice : public RenderDevice
{
public:
    int texture_uploads = 0;
    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override { return 1; }
    TextureHandle create_texture(const Image& img) override
    {
        if (img.pixels.empty()) return 0;
        return static_cast<TextureHandle>(++texture_uploads);
    }
    void draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};

AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

std::shared_ptr<Image> checker()
{
    auto img = std::make_shared<Image>();
    img->width = 1;
    img->height = 1;
    img->pixels = {255, 255, 255, 255};
    return img;
}
} // namespace

TEST_CASE("TextureCache uploads once and caches by AssetID", "[render]")
{
    AssetRegistry registry;
    const AssetID id = make_id(1);
    registry.register_resource<Image>(AssetMetadata{id, "texture"}, checker());

    CountingDevice device;
    TextureCache cache;

    const TextureHandle h1 = cache.get(id, registry, device);
    const TextureHandle h2 = cache.get(id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h2 == h1);
    REQUIRE(device.texture_uploads == 1); // uploaded once, then cached
}

TEST_CASE("TextureCache returns 0 for a nil or unregistered id", "[render]")
{
    AssetRegistry registry;
    CountingDevice device;
    TextureCache cache;
    REQUIRE(cache.get(AssetID{}, registry, device) == 0);        // nil
    REQUIRE(cache.get(make_id(9), registry, device) == 0);       // unregistered
    REQUIRE(device.texture_uploads == 0);
}
