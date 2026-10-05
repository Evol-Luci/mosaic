#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/mesh_cache.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::Mesh;
using mosaic::engine::render::MeshCache;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::TextureColorSpace;
using mosaic::foundation::serialization::AssetID;

namespace
{
class CountingDevice : public RenderDevice
{
public:
    int mesh_uploads = 0;
    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>& v,
                           const std::vector<std::uint32_t>& i) override
    {
        if (v.empty() || i.empty()) return 0;
        return static_cast<MeshHandle>(++mesh_uploads);
    }
    TextureHandle create_texture(const Image&, TextureColorSpace = TextureColorSpace::Srgb) override { return 0; }
    void draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};

AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

std::shared_ptr<Mesh> triangle()
{
    auto m = std::make_shared<Mesh>();
    m->vertices.resize(3); // contents irrelevant; non-empty is what matters
    m->indices = {0, 1, 2};
    return m;
}
} // namespace

TEST_CASE("MeshCache uploads once and caches by AssetID", "[render]")
{
    AssetRegistry registry;
    const AssetID id = make_id(1);
    registry.register_resource<Mesh>(AssetMetadata{id, "mesh"}, triangle());

    CountingDevice device;
    MeshCache cache;

    const MeshHandle h1 = cache.get(id, registry, device);
    const MeshHandle h2 = cache.get(id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h2 == h1);
    REQUIRE(device.mesh_uploads == 1); // uploaded once, then cached
}

TEST_CASE("MeshCache returns 0 for a nil or unregistered id", "[render]")
{
    AssetRegistry registry;
    CountingDevice device;
    MeshCache cache;
    REQUIRE(cache.get(AssetID{}, registry, device) == 0);  // nil
    REQUIRE(cache.get(make_id(9), registry, device) == 0); // unregistered
    REQUIRE(device.mesh_uploads == 0);
}
