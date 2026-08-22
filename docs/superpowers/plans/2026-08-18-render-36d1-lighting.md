# Rendering 36d-1 — Directional Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Light the cube with a directional light (Lambert + ambient), via reflected `Material` + `DirectionalLight` components and object-space lighting in push constants — no descriptor sets (issue [#45](https://github.com/Evol-Luci/mosaic/issues/45), first of two sub-slices).

**Architecture:** `foundation/math` gains `transform_direction`; `engine/render` gains `Material`/`DirectionalLight`, a normal on `Vertex`, and a `DrawParams` block for `draw`; the `RenderSystem` transforms the world light into each mesh's object space so all per-draw data fits in a push constant; the Vulkan backend adds the normal attribute + a lighting shader.

**Tech Stack:** C++20, Vulkan/GLFW, glslc, Catch2 v3.

## Global Constraints

- **Render boundary:** `Material`/`DirectionalLight`/`RenderSystem`/`Vertex`/`DrawParams` stay in `engine/render` (Vulkan/GLFW-free); `transform_direction` is `foundation/math`. No descriptor sets, no texture, no asset-registry use (all 36d-2).
- **Vertex:** `struct Vertex { Vec3 position; Vec3 normal; }` (drops `color`; `normal` sits at the same offset as the old `color`).
- **Neutral draw:** `struct DrawParams { Mat4 mvp; Vec3 light_direction; Vec3 light_color; Vec3 base_color; };` and `virtual void draw(MeshHandle, const DrawParams&) = 0;`. `light_direction` is in the mesh's **object space**.
- **Object-space light:** `light_direction = normalize(transform_direction(Mat4::inverse_affine(world), light.direction))`.
- **Push constant:** packed `{ mat4 mvp; vec4 light_direction; vec4 light_color; vec4 base_color; }` = 112 bytes (each `Vec3` padded to a `vec4`); stage `VERTEX | FRAGMENT`; within the 128-byte guaranteed minimum.
- **Shading:** `lambert = max(0, dot(normalize(N), -normalize(light_direction)))`; `out = base_color * (ambient + lambert*light_color)`, `ambient = 0.1`.
- **Fallbacks:** no `DirectionalLight` → default `{direction (-0.5,-1,-0.3), color white}`; no `Material` → `base_color {0.8,0.8,0.8}`.
- **Components reflected + registered** (`"Material"`, `"DirectionalLight"`), serializable.
- **C++ standard:** `cxx_std_20`. **Test tags:** `[math]`, `[render]`, `[reflection]`.

**Build & test commands:**
- Configure: `cmake --preset debug`
- Build: `cmake --build --preset debug`
- Unit tests: `./build/debug/tests/mosaic_unit_tests "[math],[render],[reflection]"`
- Full suite: `ctest --preset debug`
- Manual smoke (Tasks 3–4): `./build/debug/app/mosaic run --window`

---

### Task 1: foundation/math — transform_direction

Apply a matrix's 3×3 linear part to a direction (ignore translation). Pure, unit-tested.

**Files:**
- Modify: `foundation/math/mat4.hpp`
- Create: `tests/unit/math/transform_direction_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `Vec3 mosaic::foundation::math::transform_direction(const Mat4& m, const Vec3& v);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/math/transform_direction_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::math::transform_direction;
using Catch::Approx;

TEST_CASE("transform_direction ignores translation", "[math]")
{
    Vec3 r = transform_direction(Mat4::translation(Vec3{3, 4, 5}), Vec3{1, 0, 0});
    REQUIRE(r.x == Approx(1.0f));
    REQUIRE(r.y == Approx(0.0f));
    REQUIRE(r.z == Approx(0.0f));
}

TEST_CASE("transform_direction applies scale (linear part)", "[math]")
{
    Vec3 r = transform_direction(Mat4::scale(Vec3{2, 3, 4}), Vec3{1, 1, 1});
    REQUIRE(r.x == Approx(2.0f));
    REQUIRE(r.y == Approx(3.0f));
    REQUIRE(r.z == Approx(4.0f));
}

TEST_CASE("transform_direction matches transform_point minus translation", "[math]")
{
    // For an affine matrix, direction transform == point transform of (v) minus point
    // transform of the origin.
    const Mat4 m = Mat4::trs(Vec3{7, -2, 1},
                             mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f},
                             Vec3{1, 1, 1});
    const Vec3 v{0, 0, -1};
    const Vec3 dir = transform_direction(m, v);
    const Vec3 expected = mosaic::foundation::math::transform_point(m, v)
                          - mosaic::foundation::math::transform_point(m, Vec3{0, 0, 0});
    REQUIRE(dir.x == Approx(expected.x).margin(1e-5));
    REQUIRE(dir.y == Approx(expected.y).margin(1e-5));
    REQUIRE(dir.z == Approx(expected.z).margin(1e-5));
}
```

Add `unit/math/transform_direction_test.cpp` to the `mosaic_unit_tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset debug && cmake --build --preset debug`
Expected: FAIL — `transform_direction` not declared.

- [ ] **Step 3: Implement it**

In `foundation/math/mat4.hpp`, after the existing `transform_point` free function, add:

```cpp
// Apply m's upper-left 3x3 (linear part) to a direction vector, ignoring translation.
inline Vec3 transform_direction(const Mat4& m, const Vec3& v)
{
    return Vec3{
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[math]"`
Expected: PASS.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --preset debug` (green).

```bash
git add foundation/math/mat4.hpp tests/CMakeLists.txt tests/unit/math/transform_direction_test.cpp
git commit -m "math: transform_direction (apply a matrix's 3x3 to a direction) (#45)"
```

---

### Task 2: engine/render — lighting model + DrawParams interface

The full engine-side lighting model, fully unit-tested with a fake device. Changes the
`Vertex` and `draw` interface; syncs `VulkanRenderDevice::draw` to the new signature
(pushing only the mvp for now — GPU lighting is Task 3).

**Files:**
- Modify: `engine/render/render_device.hpp` (Vertex normal; DrawParams; draw signature)
- Create: `engine/render/material.hpp`
- Create: `engine/render/directional_light.hpp`
- Modify: `engine/render/cube.hpp` (per-face normals, drop colors)
- Modify: `engine/render/render_system.hpp` (lit traversal)
- Modify: `engine/reflection/builtin_components.hpp` (register Material + DirectionalLight)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (draw signature → DrawParams, push mvp only; vertex attr `offsetof(Vertex, normal)`)
- Modify: `tests/unit/render/render_system_test.cpp` (DrawParams-recording fake device, lit traversal, cube normals)
- Create: `tests/unit/reflection/lighting_reflection_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `transform_direction` (Task 1), `Camera`/`WorldTransform`/`MeshRenderer`.
- Produces:
  - `struct Vertex { Vec3 position; Vec3 normal; };`
  - `struct DrawParams { Mat4 mvp; Vec3 light_direction; Vec3 light_color; Vec3 base_color; };`
  - `RenderDevice::draw(MeshHandle, const DrawParams&)` (pure virtual)
  - `struct Material { Vec3 base_color{0.8,0.8,0.8}; };` (reflected, registered "Material")
  - `struct DirectionalLight { Vec3 direction{0,-1,0}; Vec3 color{1,1,1}; };` (reflected, registered "DirectionalLight")
  - `make_cube_mesh` emits per-face normals.

- [ ] **Step 1: Update the neutral interface (Vertex, DrawParams, draw)**

In `engine/render/render_device.hpp`: change `Vertex` and the `draw` declaration. The
new file body (keep the includes: `<cstdint>`, `<vector>`, `foundation/math/mat4.hpp`,
`foundation/math/vec3.hpp`):

```cpp
struct Color { float r, g, b, a; };

// Unlit vertex was {position, color}; lighting needs a normal.
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;
};

using MeshHandle = std::uint32_t;

// Per-draw parameters. `light_direction` is in the mesh's OBJECT space (the RenderSystem
// transforms the world light per mesh), so the backend needs no model/normal matrix.
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Vec3 light_direction;
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 base_color;
};

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;
    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;
    virtual void end_frame() = 0;
    virtual MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices) = 0;
    virtual void draw(MeshHandle mesh, const DrawParams& params) = 0;
};
```

- [ ] **Step 2: Write the failing tests**

Create `engine/render/material.hpp`, `engine/render/directional_light.hpp` are needed by
the tests — but per TDD write the tests first (they will fail to compile until Steps 4–5).

Replace `tests/unit/render/render_system_test.cpp` with:

```cpp
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"

using mosaic::engine::World;
using mosaic::engine::render::Camera;
using mosaic::engine::render::Color;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Material;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::MeshRenderer;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::RenderSystem;
using mosaic::engine::render::Vertex;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

namespace
{
class FakeRenderDevice : public RenderDevice
{
public:
    struct DrawCall { MeshHandle mesh; DrawParams params; };
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
    void draw(MeshHandle m, const DrawParams& p) override { draws.push_back({m, p}); }
};

World::EntityID add_camera(World& world, Vec3 pos)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(pos)});
    world.add_component<Camera>(e, Camera{});
    return e;
}
} // namespace

TEST_CASE("RenderSystem produces lit DrawParams from Material + DirectionalLight", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 0.9f, 0.8f}});

    // Mesh with a 90-degree-about-Y world rotation, so object-space light differs from world.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{7});
    world.add_component<Material>(mesh, Material{Vec3{0.2f, 0.4f, 0.6f}});

    rs.render(world, device);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(device.draws[0].mesh == 7);
    // base_color from the Material:
    REQUIRE(p.base_color.x == Approx(0.2f));
    REQUIRE(p.base_color.y == Approx(0.4f));
    REQUIRE(p.base_color.z == Approx(0.6f));
    // light_color from the DirectionalLight:
    REQUIRE(p.light_color.x == Approx(1.0f));
    REQUIRE(p.light_color.z == Approx(0.8f));
    // light_direction is the world light transformed into the mesh's object space + normalized.
    const Vec3 expected_obj = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(Mat4::inverse_affine(mesh_world),
                                                      Vec3{0, -1, 0}));
    REQUIRE(p.light_direction.x == Approx(expected_obj.x).margin(1e-5));
    REQUIRE(p.light_direction.y == Approx(expected_obj.y).margin(1e-5));
    REQUIRE(p.light_direction.z == Approx(expected_obj.z).margin(1e-5));
}

TEST_CASE("RenderSystem uses default base color and light when none present", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{5}); // no Material

    rs.render(world, device);
    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.base_color.x == Approx(0.8f)); // default material
    REQUIRE(p.light_color.x == Approx(1.0f)); // default light is white
}

TEST_CASE("RenderSystem skips mesh 0 / missing WorldTransform", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto only_mesh = world.create_entity();
    world.add_component<MeshRenderer>(only_mesh, MeshRenderer{3}); // no WorldTransform

    auto zero = world.create_entity();
    world.add_component<WorldTransform>(zero, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(zero, MeshRenderer{0});

    rs.render(world, device);
    REQUIRE(device.draws.empty());
}

TEST_CASE("make_cube_mesh yields 24 vertices with unit, per-face outward normals", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    mosaic::engine::render::make_cube_mesh(v, i);
    REQUIRE(v.size() == 24);
    REQUIRE(i.size() == 36);
    for (const auto& vert : v)
    {
        const float len2 = vert.normal.x * vert.normal.x + vert.normal.y * vert.normal.y
                           + vert.normal.z * vert.normal.z;
        REQUIRE(len2 == Approx(1.0f)); // unit normal
    }
    // The first face is +X: all four normals are (1,0,0).
    for (int k = 0; k < 4; ++k)
    {
        REQUIRE(v[k].normal.x == Approx(1.0f));
        REQUIRE(v[k].normal.y == Approx(0.0f));
        REQUIRE(v[k].normal.z == Approx(0.0f));
    }
}
```

Create `tests/unit/reflection/lighting_reflection_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::Material;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

TEST_CASE("Material and DirectionalLight are registered builtins", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);
    REQUIRE(types.has("Material"));
    REQUIRE(types.has("DirectionalLight"));
}

TEST_CASE("Material and DirectionalLight round-trip through serialization", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    auto e = src.create_entity();
    src.add_component<Material>(e, Material{Vec3{0.1f, 0.2f, 0.3f}});
    src.add_component<DirectionalLight>(e, DirectionalLight{Vec3{0, -1, 0}, Vec3{0.5f, 0.6f, 0.7f}});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    dst.for_each<Material>([&](World::EntityID, Material& m) {
        REQUIRE(m.base_color.x == Approx(0.1f));
        REQUIRE(m.base_color.z == Approx(0.3f));
    });
    dst.for_each<DirectionalLight>([&](World::EntityID, DirectionalLight& l) {
        REQUIRE(l.color.y == Approx(0.6f));
        REQUIRE(l.direction.y == Approx(-1.0f));
    });
}
```

Add both test sources to `tests/CMakeLists.txt`.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `material.hpp`/`directional_light.hpp` missing; `DrawParams`/`Vertex.normal` undefined; `VulkanRenderDevice` abstract (old `draw` signature).

- [ ] **Step 4: Add the components + cube normals + register**

Create `engine/render/material.hpp`:

```cpp
#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3 base_color{0.8f, 0.8f, 0.8f};
    // 36d-2 adds a texture AssetID.
};
template <class V> void reflect(V& visitor, Material& m) { visitor.member("base_color", m.base_color); }
}
```

Create `engine/render/directional_light.hpp`:

```cpp
#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
// `direction` is the direction the light travels (world space); shading uses -direction.
struct DirectionalLight
{
    mosaic::foundation::math::Vec3 direction{0.0f, -1.0f, 0.0f};
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
};
template <class V> void reflect(V& visitor, DirectionalLight& l)
{
    visitor.member("direction", l.direction);
    visitor.member("color", l.color);
}
}
```

Rewrite `engine/render/cube.hpp` to emit normals (drop colors):

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/render_device.hpp"

namespace mosaic::engine::render
{
// A unit cube centered at the origin (±0.5), 4 vertices per face (24 total) with the
// face's outward normal, 36 indices (2 triangles per face, CCW outward).
inline void make_cube_mesh(std::vector<Vertex>& out_vertices,
                           std::vector<std::uint32_t>& out_indices)
{
    using mosaic::foundation::math::Vec3;
    const float h = 0.5f;
    const Vec3 px{1, 0, 0}, nx{-1, 0, 0}, py{0, 1, 0}, ny{0, -1, 0}, pz{0, 0, 1}, nz{0, 0, -1};

    out_vertices = {
        // +X
        {{h, -h, -h}, px}, {{h, -h, h}, px}, {{h, h, h}, px}, {{h, h, -h}, px},
        // -X
        {{-h, -h, h}, nx}, {{-h, -h, -h}, nx}, {{-h, h, -h}, nx}, {{-h, h, h}, nx},
        // +Y
        {{-h, h, -h}, py}, {{h, h, -h}, py}, {{h, h, h}, py}, {{-h, h, h}, py},
        // -Y
        {{-h, -h, h}, ny}, {{h, -h, h}, ny}, {{h, -h, -h}, ny}, {{-h, -h, -h}, ny},
        // +Z
        {{h, -h, h}, pz}, {{-h, -h, h}, pz}, {{-h, h, h}, pz}, {{h, h, h}, pz},
        // -Z
        {{-h, -h, -h}, nz}, {{h, -h, -h}, nz}, {{h, h, -h}, nz}, {{-h, h, -h}, nz},
    };
    out_indices.clear();
    out_indices.reserve(36);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        const std::uint32_t b = face * 4;
        out_indices.insert(out_indices.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
}
} // namespace mosaic::engine::render
```

In `engine/reflection/builtin_components.hpp`, add includes and registrations:

```cpp
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
```

and after the Camera registration:

```cpp
    types.register_component<mosaic::engine::render::Material>(registry, "Material");
    types.register_component<mosaic::engine::render::DirectionalLight>(registry, "DirectionalLight");
```

- [ ] **Step 5: Rewrite the RenderSystem for lit draws**

Replace `engine/render/render_system.hpp` with (adds Material/DirectionalLight includes,
builds `DrawParams`, object-space light):

```cpp
#pragma once

#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{
// Render-time traversal: finds the active camera + directional light and draws every
// entity with WorldTransform + MeshRenderer, lit (Lambert; light transformed into each
// mesh's object space so the backend needs no model matrix). Fallbacks keep the scene
// lit/rendered when a camera/light/material is absent.
class RenderSystem
{
public:
    void set_aspect(float aspect) { aspect_ = aspect; }

    void render(World& world, RenderDevice& device) const
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;

        Mat4 view;
        Mat4 projection;
        if (!find_active_camera(world, view, projection))
        {
            view = fallback_view();
            projection = Mat4::perspective(kFallbackFovY, aspect_, kFallbackNear, kFallbackFar);
        }

        DirectionalLight light = active_light(world);

        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                if (mr.mesh == 0)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                Vec3 base_color{0.8f, 0.8f, 0.8f};
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                }
                const Vec3 light_obj = mosaic::foundation::math::normalize(
                    mosaic::foundation::math::transform_direction(
                        Mat4::inverse_affine(wt->matrix), light.direction));

                device.draw(mr.mesh, DrawParams{projection * view * wt->matrix, light_obj,
                                                light.color, base_color});
            });
    }

private:
    static constexpr float kFallbackFovY = 1.0472f;
    static constexpr float kFallbackNear = 0.1f;
    static constexpr float kFallbackFar = 100.0f;

    static mosaic::foundation::math::Mat4 fallback_view()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        return Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    }

    // First DirectionalLight, else a default.
    DirectionalLight active_light(World& world) const
    {
        DirectionalLight light{mosaic::foundation::math::Vec3{-0.5f, -1.0f, -0.3f},
                               mosaic::foundation::math::Vec3{1.0f, 1.0f, 1.0f}};
        bool found = false;
        world.for_each<DirectionalLight>(
            [&](mosaic::foundation::EntityID, DirectionalLight& l)
            {
                if (!found)
                {
                    light = l;
                    found = true;
                }
            });
        return light;
    }

    bool find_active_camera(World& world, mosaic::foundation::math::Mat4& view,
                            mosaic::foundation::math::Mat4& projection) const
    {
        using mosaic::foundation::math::Mat4;
        bool found = false;
        world.for_each<Camera>(
            [&](mosaic::foundation::EntityID e, Camera& cam)
            {
                if (found)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                view = Mat4::inverse_affine(wt->matrix);
                projection = Mat4::perspective(cam.fov_y_radians, aspect_, cam.near_z, cam.far_z);
                found = true;
            });
        return found;
    }

    float aspect_ = 16.0f / 9.0f;
};
} // namespace mosaic::engine::render
```

- [ ] **Step 6: Sync the Vulkan draw signature (mvp-only push for now)**

In `modules/render_vulkan/vulkan_render_device.cpp`:

1. Change the `draw` override signature and push only the mvp (GPU lighting is Task 3):

```cpp
    void draw(MeshHandle mesh, const mosaic::engine::render::DrawParams& params) override
    {
        if (frame_skipped_ || mesh == 0 || mesh > meshes_.size())
        {
            return;
        }
        const MeshBuffers& mb = meshes_[mesh - 1];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &mb.vbuf, &offset);
        vkCmdBindIndexBuffer(cmd_[frame_], mb.ibuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd_[frame_], pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(mosaic::foundation::math::Mat4), params.mvp.m.data());
        vkCmdDrawIndexed(cmd_[frame_], mb.index_count, 1, 0, 0, 0);
    }
```

(Match the existing bind/draw calls in the current `draw` — only the signature + the
push source change. If the current `draw` differs in local names, keep its body and just
swap `mvp` → `params.mvp`.)

2. Change the vertex attribute at `location = 1` from color to normal:

```cpp
        attrs[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));
```

- [ ] **Step 7: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[render],[reflection]"`
Expected: PASS — lit-DrawParams traversal, fallbacks, skip, cube normals, and the
Material/DirectionalLight round-trip all green. (The windowed cube renders unlit / normal-as-color
until Task 3.)

- [ ] **Step 8: Full suite + commit**

Run: `ctest --preset debug` (green).

```bash
git add engine/render/render_device.hpp engine/render/material.hpp engine/render/directional_light.hpp engine/render/cube.hpp engine/render/render_system.hpp engine/reflection/builtin_components.hpp modules/render_vulkan/vulkan_render_device.cpp tests/unit/render/render_system_test.cpp tests/unit/reflection/lighting_reflection_test.cpp tests/CMakeLists.txt
git commit -m "render 36d-1: Material/DirectionalLight, Vertex normal, DrawParams, lit RenderSystem (#45)"
```

---

### Task 3: Vulkan lighting — normal attribute, push block, Lambert shader

Make the backend actually shade: enlarge the push constant to the full `DrawParams`, push
all fields, and rewrite the shaders for Lambert + ambient. **GPU code, no unit tests** —
verified by warning-clean build + smoke.

**Files:**
- Modify: `modules/render_vulkan/shaders/cube.vert`
- Modify: `modules/render_vulkan/shaders/cube.frag`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (push range + push struct)

**Interfaces:**
- Consumes: `DrawParams` (Task 2).
- Produces: the lit Vulkan pipeline (no public API change).

- [ ] **Step 1: Rewrite the shaders**

`modules/render_vulkan/shaders/cube.vert`:

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction; // object space, .xyz
    vec4 light_color;     // .xyz
    vec4 base_color;      // .xyz
} pc;
layout(location = 0) out vec3 fragNormal;
void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = inNormal; // object space; light is provided in object space too
}
```

`modules/render_vulkan/shaders/cube.frag`:

```glsl
#version 450
layout(location = 0) in vec3 fragNormal;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
} pc;
layout(location = 0) out vec4 outColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l));
    float ambient = 0.1;
    vec3 rgb = pc.base_color.xyz * (ambient + lambert * pc.light_color.xyz);
    outColor = vec4(rgb, 1.0);
}
```

- [ ] **Step 2: Enlarge the push constant + push all fields**

In `modules/render_vulkan/vulkan_render_device.cpp`:

1. Add a packed push-constant struct near the top (after the includes / `kFramesInFlight`):

```cpp
struct PushConstants
{
    float mvp[16];
    float light_direction[4]; // vec3 + pad
    float light_color[4];
    float base_color[4];
};
static_assert(sizeof(PushConstants) == 112, "push constant layout must match the shader");
```

2. Change the push-constant range (currently `stageFlags = VERTEX_BIT`, `size = sizeof(Mat4)`):

```cpp
        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0;
        push.size = sizeof(PushConstants);
```

3. Replace the `draw` push (from Task 2's mvp-only) with the full block:

```cpp
    void draw(MeshHandle mesh, const mosaic::engine::render::DrawParams& params) override
    {
        if (frame_skipped_ || mesh == 0 || mesh > meshes_.size())
        {
            return;
        }
        const MeshBuffers& mb = meshes_[mesh - 1];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &mb.vbuf, &offset);
        vkCmdBindIndexBuffer(cmd_[frame_], mb.ibuf, 0, VK_INDEX_TYPE_UINT32);

        PushConstants pc{};
        std::memcpy(pc.mvp, params.mvp.m.data(), sizeof(pc.mvp));
        pc.light_direction[0] = params.light_direction.x;
        pc.light_direction[1] = params.light_direction.y;
        pc.light_direction[2] = params.light_direction.z;
        pc.light_color[0] = params.light_color.x;
        pc.light_color[1] = params.light_color.y;
        pc.light_color[2] = params.light_color.z;
        pc.base_color[0] = params.base_color.x;
        pc.base_color[1] = params.base_color.y;
        pc.base_color[2] = params.base_color.z;
        vkCmdPushConstants(cmd_[frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd_[frame_], mb.index_count, 1, 0, 0, 0);
    }
```

Ensure `<cstring>` is included for `std::memcpy` (it was added in 36a).

- [ ] **Step 3: Build (no unit tests — GPU) + full suite**

Run: `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`
Expected: compiles warning-clean, glslc recompiles both shaders, full suite green (201-ish + the 36d-1 cases). The `static_assert` guarantees the push layout matches the shader.

- [ ] **Step 4: Commit**

```bash
git add modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag modules/render_vulkan/vulkan_render_device.cpp
git commit -m "render 36d-1: Vulkan normal attribute + Lambert lighting shader + push block (#45)"
```

---

### Task 4: App — light + material in the demo

**Files:**
- Modify: `app/main.cpp`

**Interfaces:**
- Consumes: `DirectionalLight`, `Material` (Task 2).
- Produces: the lit demo (smoke-verified).

- [ ] **Step 1: Add a light entity + a material on the cube**

In `app/main.cpp`, add includes:

```cpp
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
```

In the `if (options.window)` block, after the cube entity is created, give the cube a
`Material` and spawn a `DirectionalLight` entity:

```cpp
        world.add_component<render::Material>(cube_entity, render::Material{Vec3{0.2F, 0.5F, 0.9F}});

        const auto light_entity = world.create_entity();
        world.add_component<render::DirectionalLight>(light_entity,
            render::DirectionalLight{Vec3{-0.5F, -1.0F, -0.3F}, Vec3{1.0F, 1.0F, 1.0F}});
```

(The `render` alias and `Vec3` already exist in the windowed block. The light entity has
no `Transform`, so `SpinSystem` — scoped to `MeshRenderer` entities — leaves it alone.)

- [ ] **Step 2: Build + full suite**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: PASS — full suite green; `mosaic` builds.

- [ ] **Step 3: Manual smoke (requires display + GPU)**

Run: `./build/debug/app/mosaic run --window`
Expected: the spinning cube is **shaded by the directional light** — faces toward the
light are bright blue, faces away are dim (ambient), and the shading sweeps across faces
as it spins. Resizing keeps rendering; closing exits 0. Headless unaffected:
`./build/debug/app/mosaic run --ticks 5` → exit 0.

- [ ] **Step 4: Commit**

```bash
git add app/main.cpp
git commit -m "render 36d-1: windowed demo adds a directional light + material (#45)"
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-08-18-render-36d1-lighting-design.md`):

| Spec item | Task |
| --- | --- |
| `transform_direction` | Task 1 |
| `Vertex` gains normal, drops color | Task 2 |
| `Material` / `DirectionalLight` reflected + registered | Task 2 |
| `DrawParams` + `draw` interface change | Task 2 (engine) + Task 3 (full push) |
| Cube per-face normals | Task 2 |
| RenderSystem lit traversal + object-space light + fallbacks | Task 2 |
| Vulkan normal attribute + 112-byte push + Lambert shader | Task 3 |
| No descriptor sets / no texture / no asset registry | Tasks 2–3 (push-constant only) |
| Demo light + material | Task 4 |
| transform_direction, lit traversal, cube normals unit-tested; reflection round-trip | Tasks 1, 2 |

No spec item is unmapped.

**Placeholder scan:** Tasks 1, 2, 4 carry complete code. Task 3 is GPU code (shaders + push) shown in full; the `static_assert` pins the push layout to the shader. No TBD/vague steps.

**Type consistency:** `Vertex{position, normal}`, `DrawParams{mvp, light_direction, light_color, base_color}`, `draw(MeshHandle, const DrawParams&)`, `Material{base_color}`, `DirectionalLight{direction, color}`, `transform_direction`, and the `PushConstants` (112 bytes matching the shader's `mat4 + 3×vec4`) are named/sized identically across tasks and tests. Task 2's mvp-only Vulkan push is superseded by Task 3's full push (same `draw` signature). Namespaces: `mosaic::engine::render`, `mosaic::foundation::math`, `mosaic::app`.
