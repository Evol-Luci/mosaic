# Rendering 36b — Mesh Draw at WorldTransform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw a mesh (a spinning unlit cube) at its `WorldTransform` under a fixed camera via a `RenderSystem`, on the neutral `RenderDevice` foundation from 36a (issue [#43](https://github.com/Evol-Luci/mosaic/issues/43)).

**Architecture:** `foundation/math` gains `perspective`/`look_at`; `engine/render` gains `Vertex`/`MeshHandle`/`MeshRenderer`, a cube generator, and a render-time `RenderSystem` (traversal tested with a fake `RenderDevice`); the neutral `RenderDevice` gains `create_mesh`/`draw`; the Vulkan backend implements them with a graphics pipeline, `glslc`-compiled SPIR-V shaders, and a depth buffer; the app draws a spinning cube in `--window` mode.

**Tech Stack:** C++20, Vulkan, GLFW, `glslc` (build-time SPIR-V), Catch2 v3.

## Global Constraints

- **Render boundary:** no Vulkan/GLFW in `engine/`/`foundation/`/`app` except `modules/render_vulkan/` and the one `app/main.cpp` factory/GLFW site. `engine/render` is Vulkan-free; `MeshHandle` is a plain `std::uint32_t`.
- **Namespaces:** neutral render `mosaic::engine::render`; math `mosaic::foundation::math`; backend `mosaic::render::vk`.
- **Camera math:** right-handed, Y-up, column-major (`m[col*4+row]`); `perspective` uses Vulkan/D3D depth range **Z ∈ [0,1]**; the Vulkan backend flips Y via a **negative-height viewport** (so `foundation/math` stays backend-neutral).
- **MVP order:** `mvp = projection * view * world.matrix`.
- **`MeshRenderer` is runtime-only** — not reflected, not serialized (like `WorldTransform`).
- **`create_mesh`** returns `0` on empty input; **`draw`** with handle `0`/unknown is a no-op; `RenderSystem` skips entities lacking `WorldTransform` or `MeshRenderer` or with `mesh==0`.
- **Shaders** compile at build time with `glslc` (found via `find_program(... REQUIRED)`); no runtime shader compiler, no new third-party dep.
- **Depth:** `D32_SFLOAT` attachment in the recreatable swapchain group; render pass clears color+depth; pipeline enables depth test/write (`LESS`).
- **C++ standard:** `cxx_std_20`. **Test tags:** `[math]`, `[render]`.

**Build & test commands:**
- Configure: `cmake --preset debug`
- Build: `cmake --build --preset debug`
- Unit tests: `./build/debug/tests/mosaic_unit_tests "[math],[render]"`
- Full suite: `ctest --preset debug`
- Manual smoke (Tasks 3–4): `./build/debug/app/mosaic run --window`

---

### Task 1: foundation/math — perspective, look_at, Vec3 ops

Camera matrices the `RenderSystem` (and 36c) need. Pure, fully unit-tested.

**Files:**
- Modify: `foundation/math/vec3.hpp` (add `dot`/`cross`/`length`/`normalize`)
- Modify: `foundation/math/mat4.hpp` (add `perspective`/`look_at`)
- Create: `tests/unit/math/projection_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: nothing new.
- Produces (namespace `mosaic::foundation::math`):
  - `float dot(const Vec3&, const Vec3&);`
  - `Vec3 cross(const Vec3&, const Vec3&);`
  - `float length(const Vec3&);`
  - `Vec3 normalize(const Vec3&);`
  - `static Mat4 Mat4::perspective(float fovy_radians, float aspect, float near_z, float far_z);`
  - `static Mat4 Mat4::look_at(const Vec3& eye, const Vec3& center, const Vec3& up);`

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/math/projection_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::math::look_at;   // ADL also works; explicit for clarity
using Catch::Approx;

TEST_CASE("Vec3 dot/cross/normalize", "[math]")
{
    REQUIRE(mosaic::foundation::math::dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == Approx(32.0f));
    Vec3 c = mosaic::foundation::math::cross(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    REQUIRE(c.x == Approx(0.0f));
    REQUIRE(c.y == Approx(0.0f));
    REQUIRE(c.z == Approx(1.0f));
    Vec3 n = mosaic::foundation::math::normalize(Vec3{0, 3, 0});
    REQUIRE(n.y == Approx(1.0f));
    REQUIRE(mosaic::foundation::math::length(Vec3{3, 4, 0}) == Approx(5.0f));
}

TEST_CASE("perspective has the expected column-major entries (RH, Z in [0,1])", "[math]")
{
    // fovy = 90deg => tan(45deg) = 1; aspect 1; near 1, far 101
    const float fovy = 1.57079633f; // pi/2
    Mat4 p = Mat4::perspective(fovy, 1.0f, 1.0f, 101.0f);
    REQUIRE(p.m[0] == Approx(1.0f));                    // 1/(aspect*tanHalf)
    REQUIRE(p.m[5] == Approx(1.0f));                    // 1/tanHalf
    REQUIRE(p.m[10] == Approx(101.0f / (1.0f - 101.0f))); // far/(near-far)
    REQUIRE(p.m[11] == Approx(-1.0f));
    REQUIRE(p.m[14] == Approx(-(101.0f * 1.0f) / (101.0f - 1.0f)));
    REQUIRE(p.m[15] == Approx(0.0f)); // perspective is not affine
}

TEST_CASE("look_at maps the eye to the origin and looks down -Z", "[math]")
{
    Mat4 v = Mat4::look_at(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    // The world origin sits 5 units in front of the camera => view-space (0,0,-5).
    Vec3 origin_in_view = mosaic::foundation::math::transform_point(v, Vec3{0, 0, 0});
    REQUIRE(origin_in_view.x == Approx(0.0f));
    REQUIRE(origin_in_view.y == Approx(0.0f));
    REQUIRE(origin_in_view.z == Approx(-5.0f));
    // The eye maps to the view-space origin.
    Vec3 eye_in_view = mosaic::foundation::math::transform_point(v, Vec3{0, 0, 5});
    REQUIRE(eye_in_view.x == Approx(0.0f));
    REQUIRE(eye_in_view.y == Approx(0.0f));
    REQUIRE(eye_in_view.z == Approx(0.0f));
}
```

Add `unit/math/projection_test.cpp` to the `add_executable(mosaic_unit_tests ...)` list in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset debug && cmake --build --preset debug`
Expected: FAIL — `dot`/`cross`/`perspective`/`look_at` not declared.

- [ ] **Step 3: Add the Vec3 helpers**

In `foundation/math/vec3.hpp`, add `#include <cmath>` at the top, and before the closing namespace add:

```cpp
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v)
{
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}
```

- [ ] **Step 4: Add the Mat4 factories**

In `foundation/math/mat4.hpp`, add `#include <cmath>` at the top. Declare inside `struct Mat4` (next to `trs`):

```cpp
    static Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z);
    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up);
```

And define them out-of-line after `operator*` (they need `Vec3` helpers, already available):

```cpp
inline Mat4 Mat4::perspective(float fovy_radians, float aspect, float near_z, float far_z)
{
    const float tan_half = std::tan(fovy_radians * 0.5f);
    Mat4 out; // all-zero
    out.m[0] = 1.0f / (aspect * tan_half);                  // [0][0]
    out.m[5] = 1.0f / tan_half;                             // [1][1]
    out.m[10] = far_z / (near_z - far_z);                   // [2][2]
    out.m[11] = -1.0f;                                      // [2][3]
    out.m[14] = -(far_z * near_z) / (far_z - near_z);       // [3][2]
    return out;
}

inline Mat4 Mat4::look_at(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 out = identity();
    out.m[0] = s.x;  out.m[4] = s.y;  out.m[8] = s.z;   // row 0 = right
    out.m[1] = u.x;  out.m[5] = u.y;  out.m[9] = u.z;   // row 1 = up
    out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z; // row 2 = -forward
    out.m[12] = -dot(s, eye);
    out.m[13] = -dot(u, eye);
    out.m[14] = dot(f, eye);
    return out;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[math]"`
Expected: PASS — new cases plus existing `[math]` green.

- [ ] **Step 6: Full suite + commit**

Run: `ctest --preset debug` (green).

```bash
git add foundation/math/vec3.hpp foundation/math/mat4.hpp tests/CMakeLists.txt tests/unit/math/projection_test.cpp
git commit -m "math: Vec3 dot/cross/normalize + Mat4 perspective/look_at (RH, Z[0,1]) (#43)"
```

---

### Task 2: engine/render — Vertex, MeshRenderer, cube, RenderSystem; extend RenderDevice

The engine-side render logic, fully tested with a fake device. Extends the neutral
interface and keeps `VulkanRenderDevice` compiling via throwing stubs (Task 3 fills
them).

**Files:**
- Modify: `engine/render/render_device.hpp` (add `Vertex`, `MeshHandle`, `create_mesh`, `draw`)
- Create: `engine/render/mesh_renderer.hpp`
- Create: `engine/render/cube.hpp`
- Create: `engine/render/render_system.hpp`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (throwing stubs for the two new methods)
- Create: `tests/unit/render/render_system_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: Task 1 math; `mosaic::engine::World`; `mosaic::engine::transform::WorldTransform`.
- Produces (namespace `mosaic::engine::render`):
  - `struct Vertex { math::Vec3 position; math::Vec3 color; };`
  - `using MeshHandle = std::uint32_t;`
  - `RenderDevice::create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) -> MeshHandle` (pure virtual)
  - `RenderDevice::draw(MeshHandle, const math::Mat4&)` (pure virtual)
  - `struct MeshRenderer { MeshHandle mesh = 0; };`
  - `void make_cube_mesh(std::vector<Vertex>& out_v, std::vector<std::uint32_t>& out_i);`
  - `class RenderSystem { RenderSystem(); void set_aspect(float); void render(World&, RenderDevice&) const; const Mat4& view() const; const Mat4& projection() const; };`

- [ ] **Step 1: Extend the neutral interface**

In `engine/render/render_device.hpp`, add includes and the new members. Full file:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

struct Color { float r, g, b, a; };

// Unlit vertex: position + per-vertex color.
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 color;
};

using MeshHandle = std::uint32_t; // opaque device mesh id; 0 == invalid

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;
    virtual void end_frame() = 0;

    // Upload a mesh; returns an opaque handle (0 on empty input).
    virtual MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices) = 0;

    // Record one indexed draw of `mesh` transformed by `mvp`, between begin/end_frame.
    virtual void draw(MeshHandle mesh, const mosaic::foundation::math::Mat4& mvp) = 0;
};

} // namespace mosaic::engine::render
```

- [ ] **Step 2: Write the failing RenderSystem/cube tests**

Create `tests/unit/render/render_system_test.cpp`:

```cpp
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
```

Add `unit/render/render_system_test.cpp` to the `mosaic_unit_tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `engine/render/render_system.hpp` / `cube.hpp` / `mesh_renderer.hpp` not found, and `VulkanRenderDevice` is abstract (missing `create_mesh`/`draw`).

- [ ] **Step 4: Add the engine render headers**

Create `engine/render/mesh_renderer.hpp`:

```cpp
#pragma once

#include "engine/render/render_device.hpp" // MeshHandle

namespace mosaic::engine::render
{
// Runtime component: which uploaded mesh an entity draws. NOT reflected/serialized
// (the handle is a per-run device id); the serializable asset-referenced form is 36d.
struct MeshRenderer
{
    MeshHandle mesh = 0;
};
} // namespace mosaic::engine::render
```

Create `engine/render/cube.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/render_device.hpp"

namespace mosaic::engine::render
{
// A unit cube centered at the origin (±0.5), 4 vertices per face (24 total) so each
// face carries a flat color, 36 indices (2 triangles per face, CCW outward).
inline void make_cube_mesh(std::vector<Vertex>& out_vertices,
                           std::vector<std::uint32_t>& out_indices)
{
    using mosaic::foundation::math::Vec3;
    const Vec3 red{1, 0, 0}, green{0, 1, 0}, blue{0, 0, 1};
    const Vec3 yellow{1, 1, 0}, cyan{0, 1, 1}, magenta{1, 0, 1};
    const float h = 0.5f;

    // 6 faces × 4 corners; winding is CCW when viewed from outside (front face CCW).
    out_vertices = {
        // +X (red)
        {{h, -h, -h}, red}, {{h, -h, h}, red}, {{h, h, h}, red}, {{h, h, -h}, red},
        // -X (green)
        {{-h, -h, h}, green}, {{-h, -h, -h}, green}, {{-h, h, -h}, green}, {{-h, h, h}, green},
        // +Y (blue)
        {{-h, h, -h}, blue}, {{h, h, -h}, blue}, {{h, h, h}, blue}, {{-h, h, h}, blue},
        // -Y (yellow)
        {{-h, -h, h}, yellow}, {{h, -h, h}, yellow}, {{h, -h, -h}, yellow}, {{-h, -h, -h}, yellow},
        // +Z (cyan)
        {{h, -h, h}, cyan}, {{-h, -h, h}, cyan}, {{-h, h, h}, cyan}, {{h, h, h}, cyan},
        // -Z (magenta)
        {{-h, -h, -h}, magenta}, {{h, -h, -h}, magenta}, {{h, h, -h}, magenta}, {{-h, h, -h}, magenta},
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

Create `engine/render/render_system.hpp`:

```cpp
#pragma once

#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{
// Render-time traversal (NOT a World tick System): draws every entity with a
// WorldTransform + MeshRenderer, transformed by a fixed camera. The app calls
// render() inside on_frame, between clear() and end_frame().
class RenderSystem
{
public:
    RenderSystem()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        view_ = Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f},
                              Vec3{0.0f, 1.0f, 0.0f});
        projection_ = Mat4::perspective(kFovY, kDefaultAspect, kNear, kFar);
    }

    // Rebuild the projection for a new aspect ratio (called by the app on resize).
    void set_aspect(float aspect)
    {
        projection_ = mosaic::foundation::math::Mat4::perspective(kFovY, aspect, kNear, kFar);
    }

    void render(World& world, RenderDevice& device) const
    {
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
                device.draw(mr.mesh, projection_ * view_ * wt->matrix);
            });
    }

    const mosaic::foundation::math::Mat4& view() const { return view_; }
    const mosaic::foundation::math::Mat4& projection() const { return projection_; }

private:
    static constexpr float kFovY = 1.0472f;        // 60 degrees
    static constexpr float kDefaultAspect = 16.0f / 9.0f;
    static constexpr float kNear = 0.1f;
    static constexpr float kFar = 100.0f;

    mosaic::foundation::math::Mat4 view_;
    mosaic::foundation::math::Mat4 projection_;
};
} // namespace mosaic::engine::render
```

- [ ] **Step 5: Add throwing stubs to VulkanRenderDevice**

In `modules/render_vulkan/vulkan_render_device.cpp`, add `#include <stdexcept>` (if absent) and give `VulkanRenderDevice` these two overrides (Task 3 replaces them with the real implementation):

```cpp
    mosaic::engine::render::MeshHandle
    create_mesh(const std::vector<mosaic::engine::render::Vertex>&,
                const std::vector<std::uint32_t>&) override
    {
        throw std::runtime_error("VulkanRenderDevice::create_mesh not implemented (36b Task 3)");
    }

    void draw(mosaic::engine::render::MeshHandle,
              const mosaic::foundation::math::Mat4&) override
    {
        throw std::runtime_error("VulkanRenderDevice::draw not implemented (36b Task 3)");
    }
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[render]"`
Expected: PASS — the RenderSystem/cube/fake-device cases green; everything builds (Vulkan stubs keep the device concrete). `mosaic run --window` still clears the screen (draw not yet called).

- [ ] **Step 7: Full suite + commit**

Run: `ctest --preset debug` (green).

```bash
git add engine/render/render_device.hpp engine/render/mesh_renderer.hpp engine/render/cube.hpp engine/render/render_system.hpp modules/render_vulkan/vulkan_render_device.cpp tests/CMakeLists.txt tests/unit/render/render_system_test.cpp
git commit -m "render 36b: Vertex/MeshRenderer/cube + RenderSystem traversal (fake-device tested) (#43)"
```

---

### Task 3: Vulkan backend — pipeline, shaders, depth, mesh upload, draw

Implement `create_mesh`/`draw` and the machinery they need. **GPU code, no unit
tests** — verified by warning-clean build + the Task 4 smoke run. Replace the Task 2
stubs.

**Files:**
- Create: `modules/render_vulkan/shaders/cube.vert`
- Create: `modules/render_vulkan/shaders/cube.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt` (glslc custom commands + shader dir define)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (depth, pipeline, mesh upload, draw)

**Interfaces:**
- Consumes: Task 2 interface (`Vertex`, `MeshHandle`, `create_mesh`, `draw`), Task 1 math.
- Produces: the real Vulkan implementation (no new public API).

- [ ] **Step 1: Write the shaders**

Create `modules/render_vulkan/shaders/cube.vert`:

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform Push { mat4 mvp; } push;
layout(location = 0) out vec3 fragColor;
void main()
{
    gl_Position = push.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
}
```

Create `modules/render_vulkan/shaders/cube.frag`:

```glsl
#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(fragColor, 1.0); }
```

- [ ] **Step 2: Wire glslc into CMake**

In `modules/render_vulkan/CMakeLists.txt`, after the library target, add:

```cmake
find_program(GLSLC glslc REQUIRED
  DOC "glslc shader compiler (ships with the Vulkan SDK)")

set(_shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${_shader_dir})
foreach(_stage cube.vert cube.frag)
  add_custom_command(
    OUTPUT  ${_shader_dir}/${_stage}.spv
    COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_stage} -o ${_shader_dir}/${_stage}.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_stage}
    COMMENT "Compiling ${_stage} -> SPIR-V")
  list(APPEND _spv ${_shader_dir}/${_stage}.spv)
endforeach()
add_custom_target(mosaic_render_vulkan_shaders DEPENDS ${_spv})
add_dependencies(mosaic_render_vulkan mosaic_render_vulkan_shaders)
target_compile_definitions(mosaic_render_vulkan PRIVATE
  MOSAIC_SHADER_DIR="${_shader_dir}")
```

- [ ] **Step 3: Implement the backend (detailed spec — GPU code)**

In `modules/render_vulkan/vulkan_render_device.cpp`, replace the Task 2 stubs with the
real implementation, adding these members and helpers. Follow the spec exactly.

Add members: `VkImage depth_image_`, `VkDeviceMemory depth_memory_`, `VkImageView depth_view_` (part of the swapchain group); `VkPipelineLayout pipeline_layout_`; `VkPipeline pipeline_`; `struct MeshBuffers { VkBuffer vbuf; VkDeviceMemory vmem; VkBuffer ibuf; VkDeviceMemory imem; uint32_t index_count; }` and `std::vector<MeshBuffers> meshes_` (a `MeshHandle` is `index+1`; `0` invalid); a stored `Color clear_color_` set by `clear()`.

New/changed pieces:

1. **Depth resources** (`create_depth_resources()`, called in the swapchain group after image views): create a `D32_SFLOAT` image sized to `extent_` with `usage=DEPTH_STENCIL_ATTACHMENT`, allocate device-local memory (add a `find_memory_type(typeBits, properties)` helper using `vkGetPhysicalDeviceMemoryProperties`), bind, create a depth image view (`aspect=DEPTH`). Destroy them in `cleanup_swapchain()` (before the color views) and recreate in `recreate_swapchain()`.

2. **Render pass** now has **two attachments**: color (as 36a) + depth (`format=D32_SFLOAT`, `loadOp=CLEAR`, `storeOp=DONT_CARE`, `initial=UNDEFINED`, `final=DEPTH_STENCIL_ATTACHMENT_OPTIMAL`). The subpass gets `pDepthStencilAttachment`. Extend the subpass dependency `srcStageMask`/`dstStageMask` to include `EARLY_FRAGMENT_TESTS` and `dstAccessMask` to include `DEPTH_STENCIL_ATTACHMENT_WRITE`.

3. **Framebuffers** now attach `{color_view[i], depth_view_}`.

4. **Pipeline** (`create_pipeline()`, once at init after the render pass): load the two `.spv` (a `read_file(MOSAIC_SHADER_DIR "/cube.vert.spv")` helper → `std::vector<char>`; `vkCreateShaderModule`; throw on read failure); vertex input = one binding (stride `sizeof(Vertex)`, `VERTEX`) + two attributes (`loc0` `R32G32B32_SFLOAT` offset `offsetof(Vertex,position)`; `loc1` same format offset `offsetof(Vertex,color)`); input assembly `TRIANGLE_LIST`; **dynamic** viewport+scissor (`VkDynamicState` VIEWPORT+SCISSOR, counts 1); rasterizer `cullMode=BACK`, `frontFace=COUNTER_CLOCKWISE`, `polygonMode=FILL`; depth-stencil `depthTestEnable=VK_TRUE`, `depthWriteEnable=VK_TRUE`, `compareOp=LESS`; one color-blend attachment (no blend, RGBA write mask); `pipeline_layout_` with a push-constant range (`stageFlags=VERTEX`, offset 0, size `sizeof(mat4)=64`). Destroy pipeline + layout + shader modules in the dtor (before device).

5. **`create_mesh`**: return `0` if either input is empty. Create a device-local vertex buffer (`usage=VERTEX_BUFFER|TRANSFER_DST`) and index buffer (`INDEX_BUFFER|TRANSFER_DST`), each filled via a **staging buffer** (`HOST_VISIBLE|HOST_COHERENT`, `map/memcpy/unmap`) + a one-time-submit copy (`begin_single_time_commands()`/`end_single_time_commands()` helpers on the graphics queue). Push a `MeshBuffers{...index_count=indices.size()}` and return `meshes_.size()` (so handle == index+1).

6. **`draw(mesh, mvp)`**: if `mesh == 0 || mesh > meshes_.size()` return (no-op). Record into the frame's command buffer (the render pass is active — see below): the pipeline is bound once in `begin_frame`; here bind the mesh's vertex buffer (`vkCmdBindVertexBuffers`), index buffer (`vkCmdBindIndexBuffer`, `UINT32`), `vkCmdPushConstants(pipeline_layout_, VERTEX, 0, 64, &mvp.m[0])`, `vkCmdDrawIndexed(index_count, 1, 0, 0, 0)`.

7. **Frame flow changes**: `clear(color)` stores `clear_color_`. `begin_frame` (after acquire + begin cmd): set the **negative-height viewport** to flip Y — `viewport.x=0, viewport.y=(float)extent_.height, viewport.width=(float)extent_.width, viewport.height=-(float)extent_.height, minDepth=0, maxDepth=1`; scissor = full extent; `vkCmdSetViewport`/`vkCmdSetScissor`; begin the render pass with **two** clear values (`clear_color_` for color, `{1.0f, 0}` for depth); `vkCmdBindPipeline(GRAPHICS, pipeline_)`. `end_frame` ends the render pass, submits, presents (unchanged from 36a besides the above).

8. **Teardown additions** (dtor, before logical device): destroy every `MeshBuffers` (4 objects each); destroy `pipeline_`, `pipeline_layout_`, shader modules if retained. Depth resources are destroyed in `cleanup_swapchain()`.

- [ ] **Step 4: Build (no unit tests — GPU)**

Run: `cmake --preset debug && cmake --build --preset debug`
Expected: compiles + links warning-clean; `glslc` compiles both shaders (see the build log's "Compiling …→ SPIR-V").

- [ ] **Step 5: Full suite (unchanged, must stay green)**

Run: `ctest --preset debug`
Expected: PASS (no new tests here; the engine tests from Task 2 still pass).

- [ ] **Step 6: Commit**

```bash
git add modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag modules/render_vulkan/CMakeLists.txt modules/render_vulkan/vulkan_render_device.cpp
git commit -m "render 36b: Vulkan pipeline, SPIR-V shaders, depth buffer, mesh upload + draw (#43)"
```

---

### Task 4: App — windowed spinning-cube demo

Wire the cube demo into `--window` mode and drive the `RenderSystem` each frame.

**Files:**
- Modify: `app/main.cpp` (create cube mesh, spawn entity, spin, render in on_frame)

**Interfaces:**
- Consumes: `make_cube_mesh`, `MeshRenderer`, `RenderSystem` (Task 2); the real `create_mesh`/`draw` (Task 3); `TransformSystem`, `World`.
- Produces: the runnable demo (verified by smoke; not unit-tested).

- [ ] **Step 1: Update main.cpp's windowed path**

In `app/main.cpp`, add includes:

```cpp
#include "engine/render/cube.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "foundation/math/vec3.hpp"
```

Inside the `if (options.window)` block, after `render_device` is created and before
building `deps.on_frame`, set up the demo scene and render system:

```cpp
        namespace render = mosaic::engine::render;
        namespace transform = mosaic::engine::transform;
        using mosaic::foundation::math::Vec3;

        // Upload the cube and spawn one entity that carries it.
        std::vector<render::Vertex>        cube_v;
        std::vector<std::uint32_t>         cube_i;
        render::make_cube_mesh(cube_v, cube_i);
        const render::MeshHandle cube = render_device->create_mesh(cube_v, cube_i);

        const auto cube_entity = world.create_entity();
        world.add_component<transform::Transform>(cube_entity, transform::Transform{});
        world.add_component<transform::WorldTransform>(cube_entity,
            transform::WorldTransform{mosaic::foundation::math::Mat4::identity()});
        world.add_component<render::MeshRenderer>(cube_entity, render::MeshRenderer{cube});

        auto render_system = std::make_shared<render::RenderSystem>();

        auto* device = render_device.get();
        deps.on_frame = [glfw_window, device, &world, render_system] {
            glfwPollEvents();
            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(glfw_window, &fbw, &fbh);
            if (fbh > 0)
            {
                render_system->set_aspect(static_cast<float>(fbw) / static_cast<float>(fbh));
            }
            device->begin_frame();
            device->clear(render::Color{0.1F, 0.1F, 0.12F, 1.0F});
            render_system->render(world, *device);
            device->end_frame();
        };
```

To make the cube **spin**, register a tiny rotation into the world so `TransformSystem`
recomputes `WorldTransform` each tick. Add a small app-local system before the loop —
replace the existing `world.add_system<transform::TransformSystem>();` line (near the
top of `main`) so both run in order:

```cpp
    world.add_system<mosaic::app::SpinSystem>();          // advances Transform.rotation
    world.add_system<engine::transform::TransformSystem>();
```

And define `SpinSystem` near the top of `main.cpp` (app-tier demo system):

```cpp
namespace mosaic::app
{
// Demo-only: spins every entity that has a Transform around Y, so the cube visibly
// rotates and its WorldTransform flows to the screen.
class SpinSystem : public mosaic::engine::ecs::System
{
public:
    void update(mosaic::engine::World& world, float dt) override
    {
        namespace m = mosaic::foundation::math;
        world.for_each<mosaic::engine::transform::Transform>(
            [&](mosaic::foundation::EntityID, mosaic::engine::transform::Transform& t) {
                angle_ += dt * 0.9F;
                const float half = angle_ * 0.5F;
                t.rotation = m::Quat{0.0F, std::sin(half), 0.0F, std::cos(half)};
            });
    }
private:
    float angle_ = 0.0F;
};
} // namespace mosaic::app
```

(Include `<cmath>` and `engine/ecs/system.hpp` at the top of `main.cpp` for this.)

> Note: `SpinSystem` accumulates one shared `angle_` and applies it to every
> `Transform`; for the single-cube demo that is exactly the intended behavior. It is
> deliberately demo-only app code (not an engine system).

- [ ] **Step 2: Build + full suite**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: PASS — full suite green; `mosaic` builds.

- [ ] **Step 3: Manual smoke (requires display + GPU)**

Run: `./build/debug/app/mosaic run --window`
Expected: a window shows a **spinning, depth-correct, vertex-colored cube** (each face
a distinct flat color) on the dark background; resizing keeps it rendering with the
correct aspect (no stretching, no validation errors); closing the window exits 0.
Also confirm headless is unaffected: `./build/debug/app/mosaic run --ticks 5` → exit 0.

- [ ] **Step 4: Commit**

```bash
git add app/main.cpp
git commit -m "render 36b: windowed spinning-cube demo driving RenderSystem (#43)"
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-08-18-render-36b-mesh-draw-design.md`):

| Spec item | Task |
| --- | --- |
| Neutral `create_mesh` + `draw(handle, mvp)` | Task 2 (interface), Task 3 (Vulkan impl) |
| `Vertex{position,color}`, `MeshHandle` | Task 2 |
| `MeshRenderer` runtime component (not reflected) | Task 2 |
| `RenderSystem` render-time traversal, fixed camera, `mvp=proj*view*world` | Task 2 |
| `perspective`/`look_at` (RH, Z[0,1], column-major) + Vec3 ops | Task 1 |
| Depth buffer in the recreatable group | Task 3 |
| Graphics pipeline + push-constant MVP + dynamic viewport | Task 3 |
| GLSL→SPIR-V via glslc at build time | Task 3 (shaders + CMake) |
| Vulkan Y-flip via negative-height viewport | Task 3 (begin_frame) |
| Windowed spinning-cube demo | Task 4 |
| RenderSystem traversal + cube unit-tested (fake device) | Task 2 |
| glslc required (CMake hard-errors if absent) | Task 3 (`find_program ... REQUIRED`) |
| Render boundary (no Vulkan/GLFW in engine/foundation) | Tasks 1–2 (engine/render Vulkan-free), Task 4 (app uses neutral types) |

No spec item is unmapped.

**Placeholder scan:** Tasks 1, 2, 4 carry complete code. Task 3 is a detailed
implementation spec (every Vulkan object + parameter + the exact frame/depth/pipeline
algorithm) rather than transcribed boilerplate — the same justified approach as 36a
Task 2, because it is un-unit-testable GPU code written against the Vulkan spec; the
non-obvious parts (negative-height Y-flip, depth attachment + dependency, staging
upload, push-constant draw) are specified concretely.

**Type consistency:** `Vertex{position,color}`, `MeshHandle=uint32`, `create_mesh(vector<Vertex>, vector<uint32_t>)->MeshHandle`, `draw(MeshHandle, const Mat4&)`, `MeshRenderer{mesh}`, `make_cube_mesh(out_v, out_i)`, `RenderSystem::{render,set_aspect,view,projection}`, and `Mat4::{perspective,look_at}` / `Vec3 dot/cross/normalize/length` are named identically across tasks and tests. The Task 2 Vulkan stubs and the Task 3 real methods share the exact override signatures. Namespaces: `mosaic::engine::render`, `mosaic::foundation::math`, `mosaic::render::vk`, `mosaic::app`.
