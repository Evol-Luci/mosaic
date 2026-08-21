#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/cube.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::render::Color;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::MeshRenderer;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::RenderSystem;
using mosaic::engine::render::Vertex;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::Mat4;
using Catch::Approx;

namespace
{
// Records draw calls; the seed of a future headless/null backend.
class FakeRenderDevice : public RenderDevice
{
public:
    struct DrawCall { MeshHandle mesh; Mat4 mvp; };
    std::vector<DrawCall> draws;
    MeshHandle            next = 1;

    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>& v,
                           const std::vector<std::uint32_t>& i) override
    {
        return (v.empty() || i.empty()) ? 0 : next++;
    }
    void draw(MeshHandle m, const Mat4& mvp) override { draws.push_back({m, mvp}); }
};
} // namespace

TEST_CASE("RenderSystem draws each entity with WorldTransform + MeshRenderer", "[render]")
{
    World  world;
    FakeRenderDevice device;
    RenderSystem rs;

    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(e, MeshRenderer{7});

    rs.render(world, device);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.draws[0].mesh == 7);
    const Mat4 expected = rs.projection() * rs.view(); // world == identity
    for (int k = 0; k < 16; ++k)
    {
        REQUIRE(device.draws[0].mvp.m[k] == Approx(expected.m[k]));
    }
}

TEST_CASE("RenderSystem skips entities missing a component or with mesh 0", "[render]")
{
    World  world;
    FakeRenderDevice device;
    RenderSystem rs;

    auto only_transform = world.create_entity();
    world.add_component<WorldTransform>(only_transform, WorldTransform{Mat4::identity()});

    auto only_mesh = world.create_entity();
    world.add_component<MeshRenderer>(only_mesh, MeshRenderer{3});

    auto zero_mesh = world.create_entity();
    world.add_component<WorldTransform>(zero_mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(zero_mesh, MeshRenderer{0});

    rs.render(world, device);
    REQUIRE(device.draws.empty());
}

TEST_CASE("make_cube_mesh yields 24 vertices, 36 in-range indices", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    mosaic::engine::render::make_cube_mesh(v, i);
    REQUIRE(v.size() == 24);
    REQUIRE(i.size() == 36);
    for (auto idx : i)
    {
        REQUIRE(idx < 24u);
    }
    for (const auto& vert : v)
    {
        REQUIRE(std::abs(vert.position.x) == Approx(0.5f));
        REQUIRE(std::abs(vert.position.y) == Approx(0.5f));
        REQUIRE(std::abs(vert.position.z) == Approx(0.5f));
    }
}

TEST_CASE("create_mesh returns 0 for empty input (fake device)", "[render]")
{
    FakeRenderDevice device;
    REQUIRE(device.create_mesh({}, {}) == 0);
}
