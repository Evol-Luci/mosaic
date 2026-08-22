# Render 36e-1 — Point Light Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `PointLight` component and object-space point-light shading to the forward renderer, so `mosaic run --window` shows a point light's distance falloff across the spinning cube alongside the existing directional light.

**Architecture:** Reuse the existing object-space, per-draw shading model. A reflected `PointLight` component derives its world position from its entity's `WorldTransform`. The `RenderSystem` transforms that world position into each mesh's object space (`transform_point(inverse_affine(model), pos)`, mirroring the directional light) and passes it plus premultiplied color and range through `DrawParams`. The vertex shader gains one varying (object-space fragment position); the fragment shader adds a windowed inverse-square point term. The Vulkan push constant grows 112 → 144 bytes.

**Tech Stack:** C++20, header-only engine components, Vulkan (GLSL → SPIR-V via glslc), Catch2 unit tests, CMake presets.

**Design spec:** `docs/superpowers/specs/2026-08-22-render-36e1-point-light-design.md`

## Global Constraints

- **Tier contract:** `engine/` depends only on `foundation/`; `foundation/` knows nothing above it. The Vulkan backend lives in `modules/render_vulkan/` and depends on `engine/render` interfaces.
- **Language:** C++20. Engine components are header-only under `engine/render/`.
- **Reflection:** components are reflected via a free `template <class V> void reflect(V&, T&)` and registered by **stable string name** in `engine/reflection/builtin_components.hpp`.
- **Tests:** Catch2, under `tests/unit/`, added to the `mosaic_unit_tests` target in `tests/CMakeLists.txt`. Test/`TEST_CASE` names must **not** start with `--` (breaks ctest discovery).
- **Math conventions:** column-major, column vectors, right-handed, Y-up, radians. `Mat4::inverse_affine` and free `transform_point(const Mat4&, const Vec3&)` already exist in `foundation/math/mat4.hpp`.
- **Build:** `cmake --build --preset debug`. **Test:** `ctest --preset debug` (full suite) or run `build/debug/tests/mosaic_unit_tests "[render]"` for the render tag.
- **Commits:** frequent, one per task. End commit messages with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Sandbox note:** the GUI window cannot be screenshotted; verify `mosaic run --window` **functionally** (exit 0, silent stderr), not by capture.

---

### Task 1: `PointLight` component + reflection registration

**Files:**
- Create: `engine/render/point_light.hpp`
- Modify: `engine/reflection/builtin_components.hpp` (add include + registration)
- Test: `tests/unit/reflection/builtin_components_test.cpp` (add one assertion)

**Interfaces:**
- Produces: `struct mosaic::engine::render::PointLight { Vec3 color; float intensity; float range; }` with a `reflect` overload; registered under the stable name `"PointLight"`.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/reflection/builtin_components_test.cpp`, inside the existing
`TEST_CASE("register_builtin_components registers Transform by name", "[reflection]")`
(after the existing `REQUIRE(types.has("Transform"));` line):

```cpp
    REQUIRE(types.has("PointLight"));
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: FAIL — `PointLight` not registered (and compile error: no `point_light.hpp`).

- [ ] **Step 3: Create the component**

Create `engine/render/point_light.hpp`:

```cpp
#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
// A positional light. World position is NOT stored here — it is derived from the
// light entity's WorldTransform (like Camera derives its view). `color` is premultiplied
// by `intensity` at shading time; `range` is the attenuation window (falloff reaches 0).
struct PointLight
{
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
    float                          intensity{1.0f};
    float                          range{10.0f};
};
template <class V> void reflect(V& visitor, PointLight& l)
{
    visitor.member("color", l.color);
    visitor.member("intensity", l.intensity);
    visitor.member("range", l.range);
}
} // namespace mosaic::engine::render
```

- [ ] **Step 4: Register it**

In `engine/reflection/builtin_components.hpp`, add the include alongside the other render includes:

```cpp
#include "engine/render/point_light.hpp"
```

and add the registration line after the `DirectionalLight` line inside `register_builtin_components`:

```cpp
    types.register_component<mosaic::engine::render::PointLight>(registry, "PointLight");
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/render/point_light.hpp engine/reflection/builtin_components.hpp tests/unit/reflection/builtin_components_test.cpp
git commit -m "$(printf 'render 36e-1: PointLight component + reflection registration (#46)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 2: `DrawParams` fields + `RenderSystem` point-light logic

**Files:**
- Modify: `engine/render/render_device.hpp` (add three `DrawParams` fields)
- Modify: `engine/render/render_system.hpp` (add `active_point_light`, populate new fields)
- Test: `tests/unit/render/render_system_test.cpp` (two new `TEST_CASE`s)

**Interfaces:**
- Consumes: `PointLight` (Task 1); existing `DrawParams`, `RenderSystem`, `transform_point`, `Mat4::inverse_affine`.
- Produces: `DrawParams` gains `Vec3 point_position; Vec3 point_color; float point_range;` (object space; `point_color` premultiplied by intensity; `point_range == 0` means no point light). The Vulkan backend (Task 3) reads these.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/render/render_system_test.cpp`. First add the include near the other render includes:

```cpp
#include "engine/render/point_light.hpp"
```

and the using-declaration near the others:

```cpp
using mosaic::engine::render::PointLight;
```

Then append these two test cases:

```cpp
TEST_CASE("RenderSystem transforms point light into mesh object space", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    // Point light at a known world position, placed via its WorldTransform.
    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<PointLight>(light, PointLight{Vec3{0.5f, 0.25f, 0.1f}, 2.0f, 6.0f});

    // Mesh with a 90-degree-about-Y world rotation, so object-space pos differs from world.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{7});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;

    // point_position = light world pos transformed into the mesh's object space.
    const Vec3 expected =
        mosaic::foundation::math::transform_point(Mat4::inverse_affine(mesh_world), light_world_pos);
    REQUIRE(p.point_position.x == Approx(expected.x).margin(1e-5));
    REQUIRE(p.point_position.y == Approx(expected.y).margin(1e-5));
    REQUIRE(p.point_position.z == Approx(expected.z).margin(1e-5));
    // Guard: object-space position must actually differ from the raw world position,
    // so this fails if the RenderSystem ever skips the object-space transform.
    REQUIRE(std::abs(p.point_position.z - light_world_pos.z) > 0.1f);

    // point_color is color * intensity (premultiplied); point_range passes through.
    REQUIRE(p.point_color.x == Approx(1.0f));   // 0.5 * 2
    REQUIRE(p.point_color.y == Approx(0.5f));   // 0.25 * 2
    REQUIRE(p.point_color.z == Approx(0.2f));   // 0.1 * 2
    REQUIRE(p.point_range == Approx(6.0f));
}

TEST_CASE("RenderSystem disables point light when none present", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{5}); // no PointLight in the world

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    // range 0 == contribution disabled in the shader.
    REQUIRE(device.draws[0].params.point_range == Approx(0.0f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `build/debug/tests/mosaic_unit_tests "[render]"`
Expected: FAIL — `DrawParams` has no `point_position`/`point_color`/`point_range` members.

- [ ] **Step 3: Extend `DrawParams`**

In `engine/render/render_device.hpp`, replace the `DrawParams` struct with:

```cpp
// Per-draw parameters. `light_direction` and `point_position` are in the mesh's OBJECT
// space (the RenderSystem transforms world lights per mesh), so the backend needs no
// model/normal matrix. `point_color` is premultiplied by intensity; `point_range == 0`
// disables the point-light contribution.
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Vec3 light_direction;
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 base_color;
    mosaic::foundation::math::Vec3 point_position;
    mosaic::foundation::math::Vec3 point_color;
    float                          point_range;
};
```

- [ ] **Step 4: Add point-light logic to `RenderSystem`**

In `engine/render/render_system.hpp`, add the include near the other render includes:

```cpp
#include "engine/render/point_light.hpp"
```

Add this private helper struct + method inside the `RenderSystem` class `private:` section
(next to `active_light`):

```cpp
    struct PointLightState
    {
        bool                           present = false;
        mosaic::foundation::math::Vec3 world_pos{};
        mosaic::foundation::math::Vec3 color{}; // premultiplied by intensity
        float                          range = 0.0f;
    };

    // First PointLight whose entity has a WorldTransform; world position = its translation.
    PointLightState active_point_light(World& world) const
    {
        using mosaic::foundation::math::Vec3;
        PointLightState s;
        world.for_each<PointLight>(
            [&](mosaic::foundation::EntityID e, PointLight& p)
            {
                if (s.present)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                s.present = true;
                s.world_pos =
                    mosaic::foundation::math::transform_point(wt->matrix, Vec3{0.0f, 0.0f, 0.0f});
                s.color = p.color * p.intensity;
                s.range = p.range;
            });
        return s;
    }
```

In the `render` method, compute the point-light state once before the `for_each<MeshRenderer>`
loop — add after the `DirectionalLight light = active_light(world);` line:

```cpp
        const PointLightState point = active_point_light(world);
```

Inside the mesh lambda, replace the final `device.draw(...)` call:

```cpp
                Vec3 point_position{0.0f, 0.0f, 0.0f};
                Vec3 point_color{0.0f, 0.0f, 0.0f};
                float point_range = 0.0f;
                if (point.present)
                {
                    point_position = mosaic::foundation::math::transform_point(
                        Mat4::inverse_affine(wt->matrix), point.world_pos);
                    point_color = point.color;
                    point_range = point.range;
                }

                device.draw(mr.mesh, texture,
                            DrawParams{projection * view * wt->matrix, light_obj, light.color,
                                       base_color, point_position, point_color, point_range});
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[render]"`
Expected: PASS (all `[render]` cases, including the pre-existing directional ones).

- [ ] **Step 6: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "$(printf 'render 36e-1: RenderSystem emits object-space point light in DrawParams (#46)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 3: Vulkan backend push constant + shaders

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (`PushConstants` struct, `static_assert`, `draw` copy, device-limit check)
- Modify: `modules/render_vulkan/shaders/cube.vert`
- Modify: `modules/render_vulkan/shaders/cube.frag`

**Interfaces:**
- Consumes: `DrawParams::point_position`, `point_color`, `point_range` (Task 2).
- Produces: nothing consumed by later tasks (renders the point light on the GPU).

> No unit test — this is GPU/shader code with no device in the test build. The deliverable is verified by a clean build (shaders compile, `static_assert` holds), a green full suite, and a functional windowed run.

- [ ] **Step 1: Grow the `PushConstants` struct**

In `modules/render_vulkan/vulkan_render_device.cpp`, replace the `PushConstants` struct and its `static_assert`:

```cpp
struct PushConstants
{
    float mvp[16];
    float light_direction[4]; // vec3 + pad
    float light_color[4];
    float base_color[4];
    float point_position[4]; // xyz = object-space pos, w = range
    float point_color[4];    // xyz = color * intensity
};
static_assert(sizeof(PushConstants) == 144, "push constant layout must match the shader");
```

- [ ] **Step 2: Copy the new fields in `draw`**

In the `draw` method, after the `base_color` copy block and before `vkCmdPushConstants`, add:

```cpp
        pc.point_position[0] = params.point_position.x;
        pc.point_position[1] = params.point_position.y;
        pc.point_position[2] = params.point_position.z;
        pc.point_position[3] = params.point_range;
        pc.point_color[0] = params.point_color.x;
        pc.point_color[1] = params.point_color.y;
        pc.point_color[2] = params.point_color.z;
```

- [ ] **Step 3: Assert the device push-constant budget**

In `pick_physical_device()`, replace the selection tail (the three assignments before `return;`):

```cpp
            physical_ = dev;
            graphics_family_ = *q.graphics;
            present_family_ = *q.present;
```

with:

```cpp
            physical_ = dev;
            graphics_family_ = *q.graphics;
            present_family_ = *q.present;

            // 144-byte push constants exceed Vulkan's guaranteed 128-byte minimum but fit
            // desktop GPUs (256). Fail loud rather than let validation catch it downstream.
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physical_, &props);
            if (sizeof(PushConstants) > props.limits.maxPushConstantsSize)
            {
                throw std::runtime_error(
                    "device maxPushConstantsSize is smaller than the renderer's push constants");
            }
```

- [ ] **Step 4: Update the vertex shader**

Replace `modules/render_vulkan/shaders/cube.vert` with:

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction; // object space, .xyz
    vec4 light_color;     // .xyz
    vec4 base_color;      // .xyz
    vec4 point_position;  // object space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
} pc;
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;
void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = inNormal; // object space; lights are provided in object space too
    fragUV = inUV;
    fragPos = inPosition;  // object space
}
```

- [ ] **Step 5: Update the fragment shader**

Replace `modules/render_vulkan/shaders/cube.frag` with:

```glsl
#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
    vec4 point_position; // object space .xyz, .w = range
    vec4 point_color;    // .xyz = color * intensity
} pc;
layout(location = 0) out vec4 outColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l));
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (object space): windowed inverse-square attenuation.
    vec3  toL   = pc.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = pc.point_position.w;
    float win   = clamp(1.0 - pow(d / max(range, 1e-4), 4.0), 0.0, 1.0);
    float atten = (range > 0.0) ? (win * win) / (d * d + 1.0) : 0.0;
    float pl    = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    vec3 color = albedo * (0.1 + lambert * pc.light_color.xyz)
               + albedo * pl * pc.point_color.xyz;
    outColor = vec4(color, 1.0);
}
```

- [ ] **Step 6: Build (compiles shaders + asserts layout) and run the full suite**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: build succeeds (glslc compiles both shaders; `static_assert(sizeof == 144)` holds); full suite green.

- [ ] **Step 7: Verify the windowed demo runs**

Run: `build/debug/app/mosaic run --window --duration 1`
Expected: exit 0, no error output on stderr. (The window cannot be screenshotted in the sandbox; functional exit is the acceptance.)

- [ ] **Step 8: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag
git commit -m "$(printf 'render 36e-1: Vulkan point light (144-byte push constant + shaders) (#46)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 4: Add a point light to the windowed demo

**Files:**
- Modify: `app/main.cpp` (add a `PointLight` entity + include)

**Interfaces:**
- Consumes: `PointLight` (Task 1); the render path (Tasks 2–3).
- Produces: nothing (demo scene only).

- [ ] **Step 1: Add the include**

In `app/main.cpp`, add near the other render includes:

```cpp
#include "engine/render/point_light.hpp"
```

- [ ] **Step 2: Add the point-light entity**

In the demo scene setup, immediately after the `DirectionalLight` entity block
(the `light_entity` add_component for `render::DirectionalLight`), add:

```cpp
        // Point light: a warm lamp offset from the cube so its distance falloff reads
        // across the spinning faces. Position comes from its WorldTransform.
        const auto point_light_entity = world.create_entity();
        world.add_component<transform::Transform>(point_light_entity,
            transform::Transform{Vec3{1.5F, 1.0F, 1.5F}, mosaic::foundation::math::Quat::identity(),
                                 Vec3{1.0F, 1.0F, 1.0F}, {}});
        world.add_component<transform::WorldTransform>(point_light_entity,
            transform::WorldTransform{mosaic::foundation::math::Mat4::translation(
                Vec3{1.5F, 1.0F, 1.5F})});
        world.add_component<render::PointLight>(point_light_entity,
            render::PointLight{Vec3{1.0F, 0.85F, 0.6F}, 3.0F, 6.0F});
```

- [ ] **Step 3: Build and run the demo**

Run: `cmake --build --preset debug && build/debug/app/mosaic run --window --duration 1`
Expected: build succeeds; exit 0; silent stderr.

- [ ] **Step 4: Run the full suite (guard against regressions)**

Run: `ctest --preset debug`
Expected: full suite green.

- [ ] **Step 5: Commit**

```bash
git add app/main.cpp
git commit -m "$(printf 'render 36e-1: windowed demo adds a point light (#46)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 5: Update the build handoff (next-steps.md)

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:** none — documentation. (Per project convention, the next-steps update rides in this feature branch, not its own PR.)

- [ ] **Step 1: Update the header and the rendering track**

Edit `docs/project/next-steps.md`:

1. Update the `**Last updated:**` line (line ~7) to today's date and note that **36e-1 (point light) is built** — `mosaic run --window` now shows a point light's distance falloff alongside the directional light — and that the **RenderGraph is deferred** to the first slice needing ≥2 passes (a shadow pre-pass, or #37's offscreen viewport). The next buildable is **#37 (Studio shell)**.
2. In the rendering sub-issue list, mark item 5 (`#46` / 36e) with a **36e-1 (point light) ✓ built** sub-bullet linking the spec (`../superpowers/specs/2026-08-22-render-36e1-point-light-design.md`) and this plan (`../superpowers/plans/2026-08-22-render-36e1-point-light.md`), and record that the render-graph half is deferred (not dropped) to the next ≥2-pass slice.
3. Update the **Suggested order** / **Full v0.1 order** lines to mark 36e-1 done and point at `#37 studio`.

Keep the prose consistent with how prior slices (36d-1/36d-2) were recorded.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(printf 'next-steps: 36e-1 (point light) built; render graph deferred; point at #37 (#46)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Self-Review

**Spec coverage:**
- `PointLight` component (color/intensity/range, position from WorldTransform) → Task 1. ✓
- Registration under `"PointLight"` → Task 1. ✓
- `DrawParams` object-space fields + `RenderSystem` `active_point_light` + per-mesh transform → Task 2. ✓
- Vertex `fragPos` varying + fragment windowed inverse-square term → Task 3 (steps 4–5). ✓
- Push constant 112→144, `static_assert`, `draw` copy, device-limit assert → Task 3 (steps 1–3). ✓
- Demo point light → Task 4. ✓
- Three test assertions (object-space position + guard; color*intensity + range; no-light → range 0) → Task 2 (two `TEST_CASE`s covering all three groups). ✓
- Known limitation (object-space distance) — documented in the spec; no code owed. ✓
- Non-goals (render graph, multi-light, world-space UBO, new math) — nothing to implement. ✓
- next-steps handoff → Task 5. ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete code; test bodies are concrete.

**Type consistency:** `DrawParams{... point_position, point_color, point_range}` field order and names match between Task 2 (definition + aggregate init), Task 3 (backend copy: `params.point_position/point_color/point_range`), and the tests (`p.point_position/point_color/point_range`). `PointLight{color, intensity, range}` order matches its aggregate use in Tasks 2 (test) and 4 (demo). `PushConstants` member names (`point_position[4]`, `point_color[4]`) match the shader `Push` block members. `active_point_light` returns `PointLightState` used only within `RenderSystem`. `transform_point`/`Mat4::inverse_affine` signatures match existing usage.
