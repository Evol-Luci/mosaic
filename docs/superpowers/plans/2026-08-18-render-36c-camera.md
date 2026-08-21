# Rendering 36c — Camera + View/Projection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render from a reflected `Camera` component placed via a `Transform` (view = inverse of its WorldTransform), replacing the `RenderSystem`'s hardcoded camera (issue [#44](https://github.com/Evol-Luci/mosaic/issues/44)).

**Architecture:** `foundation/math` gains `inverse_affine`; `engine/render` gains a reflected `Camera` component registered in `builtin_components`; the `RenderSystem` finds the active camera each frame and computes view/projection from it (default fallback if none); the windowed demo spawns a camera entity.

**Tech Stack:** C++20, Vulkan/GLFW (unchanged backend), Catch2 v3.

## Global Constraints

- **Render boundary:** `Camera` + the `RenderSystem` stay in `engine/render` (Vulkan/GLFW-free); `inverse_affine` is `foundation/math`. No change to the neutral `RenderDevice` interface or the Vulkan backend.
- **Namespaces:** `mosaic::engine::render` (Camera, RenderSystem), `mosaic::foundation::math` (inverse_affine), `mosaic::app` (demo).
- **Camera placement:** a camera is an entity with `Transform` (→ `WorldTransform`) + `Camera`; `view = Mat4::inverse_affine(cameraWorldTransform.matrix)`; `projection = Mat4::perspective(cam.fov_y_radians, aspect_, cam.near_z, cam.far_z)`. MVP order `projection * view * meshWorld`.
- **`Camera` IS reflected + serialized** (pure lens data): registered in `register_builtin_components` as `"Camera"`.
- **Active camera:** first entity (deterministic iteration order) with `Camera` + `WorldTransform`. **No active camera → default fallback** view/projection (render, don't blank).
- **`inverse_affine`:** column-major (`m[col*4+row]`); invert the upper-left 3×3 + translation; `|det| < 1e-8` → return identity.
- **C++ standard:** `cxx_std_20`. **Test tags:** `[math]`, `[render]`, `[reflection]`.

**Build & test commands:**
- Configure: `cmake --preset debug`
- Build: `cmake --build --preset debug`
- Unit tests: `./build/debug/tests/mosaic_unit_tests "[math],[render],[reflection]"`
- Full suite: `ctest --preset debug`
- Manual smoke (Task 4): `./build/debug/app/mosaic run --window`

---

### Task 1: foundation/math — inverse_affine

Invert a TRS/affine matrix. Pure, fully unit-tested. Needed by the RenderSystem (Task 3).

**Files:**
- Modify: `foundation/math/mat4.hpp` (add `inverse_affine`)
- Create: `tests/unit/math/inverse_affine_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new.
- Produces: `static Mat4 Mat4::inverse_affine(const Mat4& m);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/math/inverse_affine_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

TEST_CASE("inverse_affine of a pure translation negates it", "[math]")
{
    Mat4 inv = Mat4::inverse_affine(Mat4::translation(Vec3{1, 2, 3}));
    REQUIRE(inv.m[12] == Approx(-1.0f));
    REQUIRE(inv.m[13] == Approx(-2.0f));
    REQUIRE(inv.m[14] == Approx(-3.0f));
    REQUIRE(inv.m[0] == Approx(1.0f)); // linear part identity
    REQUIRE(inv.m[5] == Approx(1.0f));
    REQUIRE(inv.m[10] == Approx(1.0f));
}

TEST_CASE("inverse_affine(m) * m is identity for a TRS matrix", "[math]")
{
    // A non-trivial TRS: translate, rotate 90deg about Y, scale.
    const Quat ry{0.0f, 0.70710678f, 0.0f, 0.70710678f}; // 90deg about Y
    Mat4 m = Mat4::trs(Vec3{2, -3, 5}, ry, Vec3{2, 0.5f, 1.5f});
    Mat4 prod = Mat4::inverse_affine(m) * m;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            const float expected = (row == col) ? 1.0f : 0.0f;
            REQUIRE(prod.m[col * 4 + row] == Approx(expected).margin(1e-5));
        }
    }
}

TEST_CASE("inverse_affine of a singular linear part returns identity", "[math]")
{
    Mat4 zero_scale = Mat4::scale(Vec3{0, 0, 0}); // non-invertible 3x3
    Mat4 inv = Mat4::inverse_affine(zero_scale);
    REQUIRE(inv.m[0] == Approx(1.0f));
    REQUIRE(inv.m[5] == Approx(1.0f));
    REQUIRE(inv.m[10] == Approx(1.0f));
    REQUIRE(inv.m[15] == Approx(1.0f));
}
```

Add `unit/math/inverse_affine_test.cpp` to the `add_executable(mosaic_unit_tests ...)` list in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset debug && cmake --build --preset debug`
Expected: FAIL — `inverse_affine` not declared.

- [ ] **Step 3: Implement inverse_affine**

In `foundation/math/mat4.hpp`, declare inside `struct Mat4` (next to `look_at`):

```cpp
    static Mat4 inverse_affine(const Mat4& m);
```

And define it out-of-line after `operator*`:

```cpp
inline Mat4 Mat4::inverse_affine(const Mat4& m)
{
    // Upper-left 3x3 (column-major within the 4x4): element(row,col) = m[col*4+row].
    const float a = m.m[0], b = m.m[4], c = m.m[8];
    const float d = m.m[1], e = m.m[5], f = m.m[9];
    const float g = m.m[2], h = m.m[6], i = m.m[10];

    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det > -1e-8f && det < 1e-8f)
    {
        return identity(); // singular linear part (e.g. zero scale)
    }
    const float inv_det = 1.0f / det;

    // Inverse of the 3x3 (adjugate / det), as inv(row,col):
    const float i00 = (e * i - f * h) * inv_det;
    const float i01 = (c * h - b * i) * inv_det;
    const float i02 = (b * f - c * e) * inv_det;
    const float i10 = (f * g - d * i) * inv_det;
    const float i11 = (a * i - c * g) * inv_det;
    const float i12 = (c * d - a * f) * inv_det;
    const float i20 = (d * h - e * g) * inv_det;
    const float i21 = (b * g - a * h) * inv_det;
    const float i22 = (a * e - b * d) * inv_det;

    Mat4 out = identity();
    // Store the inverse 3x3 column-major: out.m[col*4+row] = inv(row,col).
    out.m[0] = i00; out.m[4] = i01; out.m[8] = i02;
    out.m[1] = i10; out.m[5] = i11; out.m[9] = i12;
    out.m[2] = i20; out.m[6] = i21; out.m[10] = i22;

    // Translation: t' = -inv3x3 * t, where t = (m[12], m[13], m[14]).
    const float tx = m.m[12], ty = m.m[13], tz = m.m[14];
    out.m[12] = -(i00 * tx + i01 * ty + i02 * tz);
    out.m[13] = -(i10 * tx + i11 * ty + i12 * tz);
    out.m[14] = -(i20 * tx + i21 * ty + i22 * tz);
    return out;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[math]"`
Expected: PASS — new cases + existing `[math]` green.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --preset debug` (green).

```bash
git add foundation/math/mat4.hpp tests/CMakeLists.txt tests/unit/math/inverse_affine_test.cpp
git commit -m "math: Mat4::inverse_affine (invert a TRS matrix) (#44)"
```

---

### Task 2: engine/render — Camera component + registration + reflection round-trip

A reflected `Camera` (pure lens), registered in `builtin_components`, serializable.

**Files:**
- Create: `engine/render/camera.hpp`
- Modify: `engine/reflection/builtin_components.hpp` (register `Camera`)
- Create: `tests/unit/reflection/camera_reflection_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ComponentReflection`, `TypeRegistry`, scene `save_to_json`/`load_from_json`.
- Produces:
  - `struct mosaic::engine::render::Camera { float fov_y_radians = 1.0472f; float near_z = 0.1f; float far_z = 100.0f; };` (reflected)
  - `register_builtin_components` also registers `"Camera"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/camera_reflection_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/camera.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::Camera;
using mosaic::foundation::reflection::TypeRegistry;
using Catch::Approx;

TEST_CASE("Camera is registered as a builtin component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);
    REQUIRE(types.has("Camera"));
}

TEST_CASE("a Camera round-trips through serialization with its fields", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    const World::EntityID e = src.create_entity();
    src.add_component<Camera>(e, Camera{1.2f, 0.5f, 250.0f}); // non-default fov/near/far

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);

    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    int count = 0;
    dst.for_each<Camera>(
        [&](World::EntityID, Camera& c)
        {
            ++count;
            REQUIRE(c.fov_y_radians == Approx(1.2f));
            REQUIRE(c.near_z == Approx(0.5f));
            REQUIRE(c.far_z == Approx(250.0f));
        });
    REQUIRE(count == 1);
}
```

Add `unit/reflection/camera_reflection_test.cpp` to the `mosaic_unit_tests` sources.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `engine/render/camera.hpp` not found and `types.has("Camera")` false.

- [ ] **Step 3: Write the Camera header**

Create `engine/render/camera.hpp`:

```cpp
#pragma once

namespace mosaic::engine::render
{

// The camera "lens". Placement comes from the entity's Transform (view = inverse of
// its WorldTransform). Pure data — reflected and serializable (unlike MeshRenderer,
// whose value is a runtime device handle).
struct Camera
{
    float fov_y_radians = 1.0472f; // 60 degrees
    float near_z = 0.1f;
    float far_z = 100.0f;
};

template <class V>
void reflect(V& visitor, Camera& c)
{
    visitor.member("fov_y_radians", c.fov_y_radians);
    visitor.member("near_z", c.near_z);
    visitor.member("far_z", c.far_z);
}

} // namespace mosaic::engine::render
```

- [ ] **Step 4: Register Camera in builtin_components**

In `engine/reflection/builtin_components.hpp`, add the include and the registration:

```cpp
#include "engine/render/camera.hpp"
```

and, replacing the `// future: Camera, Mesh, ...` comment:

```cpp
    types.register_component<mosaic::engine::render::Camera>(registry, "Camera");
    // future: Mesh, Light, ...
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS — Camera registered + round-trips; existing `[reflection]` green.

- [ ] **Step 6: Full suite + commit**

Run: `ctest --preset debug` (green).

```bash
git add engine/render/camera.hpp engine/reflection/builtin_components.hpp tests/CMakeLists.txt tests/unit/reflection/camera_reflection_test.cpp
git commit -m "render 36c: reflected Camera component, registered in builtin_components (#44)"
```

---

### Task 3: RenderSystem — camera-driven view/projection

Compute view/projection each frame from the active camera; fall back to a default when
none. Rewrite the RenderSystem tests (they asserted the removed fixed-camera accessors).

**Files:**
- Modify: `engine/render/render_system.hpp`
- Modify: `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `Camera` (Task 2), `Mat4::inverse_affine`/`perspective` (Task 1/36b), `WorldTransform`, `MeshRenderer`.
- Produces: `RenderSystem` with `render(World&, RenderDevice&) const` (camera-driven) + `set_aspect(float)`. `view()`/`projection()` accessors are removed (matrices are now per-frame).

- [ ] **Step 1: Rewrite the RenderSystem tests**

Replace the contents of `tests/unit/render/render_system_test.cpp` with:

```cpp
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"

using mosaic::engine::World;
using mosaic::engine::render::Camera;
using mosaic::engine::render::Color;
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

// Adds a camera entity at `pos` (identity rotation) with the given lens.
World::EntityID add_camera(World& world, Vec3 pos, Camera lens)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(pos)});
    world.add_component<Camera>(e, lens);
    return e;
}

// Adds a mesh entity at world matrix `wm` with handle `h`.
World::EntityID add_mesh(World& world, const Mat4& wm, MeshHandle h)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{wm});
    world.add_component<MeshRenderer>(e, MeshRenderer{h});
    return e;
}
} // namespace

TEST_CASE("RenderSystem renders meshes from the active camera", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    rs.set_aspect(16.0f / 9.0f);

    const Camera lens{1.0472f, 0.1f, 100.0f};
    add_camera(world, Vec3{0, 0, 4}, lens);
    add_mesh(world, Mat4::identity(), 7);

    rs.render(world, device);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.draws[0].mesh == 7);

    const Mat4 view = Mat4::inverse_affine(Mat4::translation(Vec3{0, 0, 4}));
    const Mat4 proj = Mat4::perspective(lens.fov_y_radians, 16.0f / 9.0f, lens.near_z, lens.far_z);
    const Mat4 expected = proj * view; // mesh world == identity
    for (int k = 0; k < 16; ++k)
    {
        REQUIRE(device.draws[0].mvp.m[k] == Approx(expected.m[k]).margin(1e-5));
    }
    // Camera at (0,0,4) => view is translation (0,0,-4).
    REQUIRE(view.m[14] == Approx(-4.0f));
}

TEST_CASE("RenderSystem still renders meshes when no camera exists (fallback)", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;

    add_mesh(world, Mat4::identity(), 3);
    add_mesh(world, Mat4::identity(), 4);

    rs.render(world, device); // no camera entity -> default fallback
    REQUIRE(device.draws.size() == 2);
}

TEST_CASE("RenderSystem uses the first-found camera when several exist", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    // Two cameras with distinct fov; the first-created should win.
    add_camera(world, Vec3{0, 0, 4}, Camera{0.5f, 0.1f, 100.0f});
    add_camera(world, Vec3{0, 0, 8}, Camera{1.4f, 0.1f, 100.0f});
    add_mesh(world, Mat4::identity(), 9);

    rs.render(world, device);
    REQUIRE(device.draws.size() == 1);

    const Mat4 view = Mat4::inverse_affine(Mat4::translation(Vec3{0, 0, 4}));
    const Mat4 proj = Mat4::perspective(0.5f, 1.0f, 0.1f, 100.0f);
    const Mat4 expected = proj * view;
    for (int k = 0; k < 16; ++k)
    {
        REQUIRE(device.draws[0].mvp.m[k] == Approx(expected.m[k]).margin(1e-5));
    }
}

TEST_CASE("RenderSystem skips entities missing a component or with mesh 0", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4}, Camera{});

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
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — the new tests reference camera-driven behavior the current fixed-camera `RenderSystem` doesn't have (and `view()`/`projection()` may still exist); build/asserts fail.

- [ ] **Step 3: Rewrite the RenderSystem**

Replace `engine/render/render_system.hpp` with:

```cpp
#pragma once

#include "engine/render/camera.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{
// Render-time traversal (NOT a World tick System): each frame it finds the active
// camera (first entity with Camera + WorldTransform), builds view = inverse of the
// camera's world matrix and a perspective projection from its lens, and draws every
// entity with WorldTransform + MeshRenderer. With no camera it falls back to a default
// so the scene still renders. The app calls render() in on_frame between clear() and
// end_frame().
class RenderSystem
{
public:
    // Set the viewport aspect ratio (width/height); used when building the projection.
    void set_aspect(float aspect) { aspect_ = aspect; }

    void render(World& world, RenderDevice& device) const
    {
        using mosaic::foundation::math::Mat4;

        Mat4 view;
        Mat4 projection;
        if (!find_active_camera(world, view, projection))
        {
            view = fallback_view();
            projection = mosaic::foundation::math::Mat4::perspective(kFallbackFovY, aspect_,
                                                                     kFallbackNear, kFallbackFar);
        }

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
                device.draw(mr.mesh, projection * view * wt->matrix);
            });
    }

private:
    static constexpr float kFallbackFovY = 1.0472f; // 60 degrees
    static constexpr float kFallbackNear = 0.1f;
    static constexpr float kFallbackFar = 100.0f;

    // Default camera used only when the world has no Camera entity.
    static mosaic::foundation::math::Mat4 fallback_view()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        return Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f},
                             Vec3{0.0f, 1.0f, 0.0f});
    }

    // First entity with Camera + WorldTransform sets view/projection; returns false if
    // none. Deterministic (component iteration order).
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

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[render]"`
Expected: PASS — the camera-driven, fallback, first-found, skip, and cube cases green.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --preset debug` (green). `mosaic run --window` still renders the cube via the fallback (no camera entity spawned until Task 4).

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "render 36c: RenderSystem derives view/projection from the active Camera (#44)"
```

---

### Task 4: App — spawn a Camera entity in the demo

Give the windowed demo a real camera entity so it renders through the `Camera`
component instead of the fallback.

**Files:**
- Modify: `app/main.cpp` (spawn a camera entity; scope `SpinSystem` to the cube)

**Interfaces:**
- Consumes: `Camera` (Task 2), `RenderSystem` (Task 3), `Transform`/`WorldTransform`, `MeshRenderer`, `Mat4::translation`.
- Produces: the camera-driven demo (verified by smoke; not unit-tested).

- [ ] **Step 1: Scope SpinSystem to mesh entities (so it doesn't spin the camera)**

The demo now has **two** entities with a `Transform` (the cube and the new camera).
`SpinSystem` currently rotates *every* `Transform`, which would spin the camera (garbling
the view) and double-accumulate `angle_`. Change `SpinSystem::update` in `app/main.cpp`
to advance the angle once per tick and rotate only entities that also have a
`MeshRenderer` (i.e. the cube):

```cpp
    void update(mosaic::engine::World& world, float dt) override
    {
        namespace m = mosaic::foundation::math;
        angle_ += dt * 0.9F;
        const float      half = angle_ * 0.5F;
        const m::Quat    spin{0.0F, std::sin(half), 0.0F, std::cos(half)};
        world.for_each<mosaic::engine::transform::Transform>(
            [&](mosaic::foundation::EntityID e, mosaic::engine::transform::Transform& t)
            {
                if (!world.has_component<mosaic::engine::render::MeshRenderer>(e))
                {
                    return; // only spin meshes (the cube), not the camera
                }
                t.rotation = spin;
            });
    }
```

Ensure `#include "engine/render/mesh_renderer.hpp"` is present at the top of
`app/main.cpp` (it was added in 36b).

- [ ] **Step 2: Spawn the camera entity**

In `app/main.cpp`, add the include:

```cpp
#include "engine/render/camera.hpp"
```

Inside the `if (options.window)` block, after the cube entity is created and before
building `deps.on_frame`, add a camera entity placed to view the cube:

```cpp
        // Camera entity: placed at (0,0,4) looking down -Z at the cube at the origin.
        // Its Transform → WorldTransform (via TransformSystem) drives the view; the
        // Camera component is the lens. Replaces the RenderSystem's fallback camera.
        const auto camera_entity = world.create_entity();
        world.add_component<transform::Transform>(camera_entity,
            transform::Transform{Vec3{0.0F, 0.0F, 4.0F}, mosaic::foundation::math::Quat::identity(),
                                 Vec3{1.0F, 1.0F, 1.0F}, {}});
        world.add_component<transform::WorldTransform>(camera_entity,
            transform::WorldTransform{mosaic::foundation::math::Mat4::translation(Vec3{0.0F, 0.0F, 4.0F})});
        world.add_component<render::Camera>(camera_entity, render::Camera{});
```

(`Vec3` and the `transform`/`render` namespace aliases already exist in the windowed
block from 36b. `Transform`'s fields are `{position, rotation, scale, parent}`; the
initial `WorldTransform` is set so frame 0 has a valid view before `TransformSystem`
runs. `Quat::identity()` keeps the camera un-rotated, looking straight down −Z.)

- [ ] **Step 3: Build + full suite**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: PASS — full suite green; `mosaic` builds.

- [ ] **Step 4: Manual smoke (requires display + GPU)**

Run: `./build/debug/app/mosaic run --window`
Expected: a window shows the spinning cube framed by the camera at `(0,0,4)` (the cube
centered, viewed head-on down −Z) — the **camera does not spin**; resizing keeps the
correct aspect; closing exits 0. Headless unaffected: `./build/debug/app/mosaic run
--ticks 5` → exit 0.

- [ ] **Step 5: Commit**

```bash
git add app/main.cpp
git commit -m "render 36c: windowed demo renders through a Camera entity (#44)"
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-08-18-render-36c-camera-design.md`):

| Spec item | Task |
| --- | --- |
| `inverse_affine` (invert TRS, singular guard) | Task 1 |
| Reflected `Camera` component (fov/near/far) | Task 2 |
| Registered in `builtin_components` ("Camera"), serializable | Task 2 |
| `RenderSystem` view = inverse_affine(cameraWorld), projection from lens | Task 3 |
| Active camera = first-found; default fallback when none | Task 3 |
| MVP order projection*view*meshWorld; skip mesh==0 / missing WorldTransform | Task 3 |
| Demo spawns a camera entity | Task 4 |
| inverse_affine + camera-driven traversal unit-tested (fake device) | Tasks 1, 3 |
| Camera reflection round-trip tested | Task 2 |
| Render boundary (engine/render Vulkan-free; math in foundation) | Tasks 1–4 |

No spec item is unmapped.

**Placeholder scan:** all four tasks carry complete code + exact commands; no TBD/vague steps. Task 4 is app wiring verified by smoke (no unit test — GPU/window), consistent with 36a/36b.

**Type consistency:** `Camera{fov_y_radians, near_z, far_z}`, `Mat4::inverse_affine`, `RenderSystem::{render, set_aspect}` (view()/projection() intentionally removed and the 36b test rewritten in Task 3), `find_active_camera`, and the `register_component<Camera>(registry,"Camera")` call are named identically across tasks and tests. The fake `RenderDevice` implements the full 36b interface (`begin_frame`/`clear`/`end_frame`/`create_mesh`/`draw`). Namespaces: `mosaic::engine::render`, `mosaic::foundation::math`, `mosaic::app`.
