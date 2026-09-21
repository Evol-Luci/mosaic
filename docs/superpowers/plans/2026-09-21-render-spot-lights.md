# Rendering — Spot Lights Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `SpotLight` component: a third simultaneous "first found" light type alongside `DirectionalLight`/`PointLight`, with a smooth cone falloff, reusing the point light's positional machinery and windowed inverse-square distance attenuation.

**Architecture:** No new render pass, no new `RenderDevice` hooks — extends the existing single forward pass's `DrawParams`/push-constant path exactly the way the point-light slice (36e-1) did.

**Tech Stack:** C++20, Vulkan 1.x (`modules/render_vulkan`), GLSL → SPIR-V (glslc), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-21-render-spot-lights-design.md`

## Global Constraints

- Spot lights do not cast shadows in this slice (directional-only shadows stay as-is).
- No specular/PBR shading changes in this slice — spot lights reuse the existing Lambert diffuse term, exactly like the point light does.
- No hard-cutoff cone edge — the cone falloff is smooth (`smoothstep` between the outer and inner cosines).
- `PushConstants` grows a third time (208 → 256 bytes), guarded by the existing device-limit `static_assert`/runtime check — no new machinery (no UBO) introduced for this.
- Only one `SpotLight` is honored per frame ("first found, else absent" — identical to the existing `DirectionalLight`/`PointLight` limitation); real multi-light support is out of scope.

---

### Task 1: `SpotLight` component + registration

**Files:**
- Create: `engine/render/spot_light.hpp`
- Modify: `engine/reflection/builtin_components.hpp` (include + register)
- Modify: `tests/unit/reflection/builtin_components_test.cpp` (registration assertion)

**Interfaces:**
- Consumes: nothing new.
- Produces: `mosaic::engine::render::SpotLight` (a reflected component with `color`, `intensity`, `range`, `inner_angle`, `outer_angle` fields), registered under the stable name `"SpotLight"` — consumed by Task 2.

- [ ] **Step 1: Write the failing test**

In `tests/unit/reflection/builtin_components_test.cpp`, find the existing line `REQUIRE(types.has("PointLight"));` and add directly after it:

```cpp
    REQUIRE(types.has("SpotLight"));
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -30`
Expected: compile error — `"SpotLight"` isn't a real type yet (or, if the test file alone compiles fine without the include, a runtime `REQUIRE` failure: `types.has("SpotLight")` is false). Either way, confirm it doesn't pass before continuing.

- [ ] **Step 3: Create the component**

Create `engine/render/spot_light.hpp`:

```cpp
#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
// A positional, directional light with a cone. World position and direction are
// NOT stored here -- position is derived from the light entity's WorldTransform
// translation (like PointLight), direction from its rotation (WorldTransform's
// local -Z axis in world space, via Mat4::forward() -- the same convention
// Camera's view derivation already uses). `color` is premultiplied by
// `intensity` at shading time; `range` is the same windowed-attenuation
// distance falloff PointLight already uses. `inner_angle`/`outer_angle` are
// cone HALF-angles in radians (angle from the light's forward axis to the cone
// edge) -- full intensity inside `inner_angle`, smoothly fading to zero at
// `outer_angle`. `inner_angle` should be <= `outer_angle`; this is not
// enforced here -- a caller passing inner > outer gets an inverted, likely
// visually-wrong falloff, not a crash, consistent with this project's
// "missing/malformed data degrades gracefully, doesn't throw" convention for
// render components.
struct SpotLight
{
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
    float                          intensity{1.0f};
    float                          range{10.0f};
    float                          inner_angle{0.4f};  // radians (~23 degrees)
    float                          outer_angle{0.6f};  // radians (~34 degrees)
};
template <class V> void reflect(V& visitor, SpotLight& l)
{
    visitor.member("color", l.color);
    visitor.member("intensity", l.intensity);
    visitor.member("range", l.range);
    visitor.member("inner_angle", l.inner_angle);
    visitor.member("outer_angle", l.outer_angle);
}
} // namespace mosaic::engine::render
```

- [ ] **Step 4: Register it**

In `engine/reflection/builtin_components.hpp`, add the include next to the existing light includes:

```cpp
#include "engine/render/spot_light.hpp"
```

(Place it directly after `#include "engine/render/point_light.hpp"`.)

Add the registration line directly after the existing `PointLight` registration:

```cpp
    types.register_component<mosaic::engine::render::SpotLight>(registry, "SpotLight");
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[reflection]"`
Expected: all `[reflection]`-tagged tests pass, including the updated registration test.

- [ ] **Step 6: Commit**

```bash
git add engine/render/spot_light.hpp engine/reflection/builtin_components.hpp tests/unit/reflection/builtin_components_test.cpp
git commit -m "feat(render): add SpotLight component + registration

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 2: `RenderSystem` — `active_spot_light` + object-space transform + `DrawParams`

**Files:**
- Modify: `engine/render/render_device.hpp` (`DrawParams` fields)
- Modify: `engine/render/render_system.hpp` (`SpotLightState`, `active_spot_light`, main-loop wiring)
- Modify: `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `SpotLight` (Task 1).
- Produces: `DrawParams::spot_position`/`spot_direction`/`spot_color`/`spot_range`/`spot_cos_inner`/`spot_cos_outer` — consumed by Task 3 (the Vulkan backend).

- [ ] **Step 1: Write the failing test**

In `tests/unit/render/render_system_test.cpp`, add the include if not already present via `render_system.hpp`:

```cpp
#include "engine/render/spot_light.hpp"
```

Add this test case directly after the existing `"RenderSystem transforms point light into mesh object space"` test:

```cpp
TEST_CASE("RenderSystem transforms spot light position and direction into mesh object space", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    // Spot light at a known world position with identity rotation (so its forward
    // direction, via Mat4::forward(), is the default world -Z: {0,0,-1}).
    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<SpotLight>(light, SpotLight{Vec3{0.3f, 0.6f, 0.9f}, 2.0f, 8.0f, 0.4f, 0.6f});

    // Mesh with a 90-degree-about-Y world rotation (identical setup to the existing
    // point-light object-space test), so object-space values differ from world ones.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;

    // spot_position: identical golden-value derivation to the existing point-light
    // test (same light world position, same mesh rotation) -- world->object is the
    // inverse rotation R_y(-90 deg), mapping world {2,0,0} -> object {0,0,2}.
    REQUIRE(p.spot_position.x == Approx(0.0f).margin(1e-5));
    REQUIRE(p.spot_position.y == Approx(0.0f).margin(1e-5));
    REQUIRE(p.spot_position.z == Approx(2.0f).margin(1e-5));

    // spot_direction: independently hand-derived (not by re-running the production
    // formula, so a systematic error in it cannot make this test pass by matching
    // itself). World direction is {0,0,-1} (identity-rotation light, Mat4::forward()).
    // Applying the same R_y(-90 deg) world->object rotation the position above uses:
    // R_y(-90) maps (x,z) -> (x*0 + z*(-1), -x*(-1) + z*0) = (-z, x). For direction
    // (x=0, z=-1): (-(-1), 0) = (1, 0). So object-space direction = {1, 0, 0}.
    REQUIRE(p.spot_direction.x == Approx(1.0f).margin(1e-5));
    REQUIRE(p.spot_direction.y == Approx(0.0f).margin(1e-5));
    REQUIRE(p.spot_direction.z == Approx(0.0f).margin(1e-5));

    // spot_color is color * intensity (premultiplied); range/cosines pass through.
    REQUIRE(p.spot_color.x == Approx(0.6f));  // 0.3 * 2
    REQUIRE(p.spot_color.y == Approx(1.2f));  // 0.6 * 2
    REQUIRE(p.spot_color.z == Approx(1.8f));  // 0.9 * 2
    REQUIRE(p.spot_range == Approx(8.0f));
    REQUIRE(p.spot_cos_inner == Approx(std::cos(0.4f)));
    REQUIRE(p.spot_cos_outer == Approx(std::cos(0.6f)));
}

TEST_CASE("RenderSystem disables spot light when none present", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.draws[0].params.spot_range == Approx(0.0f)); // 0 == disabled
}
```

Add `#include <cmath>` to the top of this test file if not already present (needed for `std::cos`).

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -40`
Expected: compile errors — `DrawParams` has no `spot_position`/`spot_direction`/`spot_color`/`spot_range`/`spot_cos_inner`/`spot_cos_outer` members yet.

- [ ] **Step 3: Add the `DrawParams` fields**

In `engine/render/render_device.hpp`, add to `DrawParams` directly after the existing `light_mvp` field:

```cpp
    mosaic::foundation::math::Vec3 spot_position;  // object space
    mosaic::foundation::math::Vec3 spot_direction; // object space, normalized
    mosaic::foundation::math::Vec3 spot_color;     // premultiplied by intensity
    float                          spot_range = 0.0f; // 0 == no spot light (disabled)
    float                          spot_cos_inner = 1.0f;
    float                          spot_cos_outer = 1.0f;
```

- [ ] **Step 4: Add `SpotLightState` + `active_spot_light` to `RenderSystem`**

In `engine/render/render_system.hpp`, add the include:

```cpp
#include "engine/render/spot_light.hpp"
```

Add the state struct and accessor directly after the existing `PointLightState`/`active_point_light` (find `return s;\n    }` closing `active_point_light`, and insert after it):

```cpp
    struct SpotLightState
    {
        bool                           present = false;
        mosaic::foundation::math::Vec3 world_pos{};
        mosaic::foundation::math::Vec3 direction{}; // world space, normalized
        mosaic::foundation::math::Vec3 color{};     // premultiplied by intensity
        float                          range = 0.0f;
        float                          cos_inner = 1.0f;
        float                          cos_outer = 1.0f;
    };

    // First SpotLight whose entity has a WorldTransform; world position = its
    // translation, direction = its local -Z axis in world space (Mat4::forward()).
    SpotLightState active_spot_light(World& world) const
    {
        using mosaic::foundation::math::Vec3;
        SpotLightState s;
        world.for_each<SpotLight>(
            [&](mosaic::foundation::EntityID e, SpotLight& l)
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
                s.direction = wt->matrix.forward();
                s.color = l.color * l.intensity;
                s.range = l.range;
                s.cos_inner = std::cos(l.inner_angle);
                s.cos_outer = std::cos(l.outer_angle);
            });
        return s;
    }
```

Add `#include <cmath>` to `render_system.hpp`'s includes if not already present (needed for `std::cos`).

- [ ] **Step 5: Wire it into `render`'s main loop**

In `render_system.hpp`'s `render(...)` method, add the spot-light lookup alongside the existing `const PointLightState point = active_point_light(world);` line:

```cpp
        const SpotLightState spot = active_spot_light(world);
```

In the main per-mesh loop, directly after the existing point-light object-space transform block (the `if (point.present) { ... }` block, before the `device.draw(...)` call), add:

```cpp
                Vec3 spot_position{0.0f, 0.0f, 0.0f};
                Vec3 spot_direction{0.0f, 0.0f, -1.0f};
                Vec3 spot_color{0.0f, 0.0f, 0.0f};
                float spot_range = 0.0f;
                float spot_cos_inner = 1.0f;
                float spot_cos_outer = 1.0f;
                if (spot.present)
                {
                    const Mat4 inv = Mat4::inverse_affine(wt->matrix);
                    spot_position = mosaic::foundation::math::transform_point(inv, spot.world_pos);
                    spot_direction = mosaic::foundation::math::normalize(
                        mosaic::foundation::math::transform_direction(inv, spot.direction));
                    spot_color = spot.color;
                    spot_range = spot.range;
                    spot_cos_inner = spot.cos_inner;
                    spot_cos_outer = spot.cos_outer;
                }
```

Then update the `device.draw(...)` call's `DrawParams{...}` aggregate-init to append the six new arguments after `light_view_proj * wt->matrix` (the existing `light_mvp` argument), in the exact order the struct declares them (`spot_position, spot_direction, spot_color, spot_range, spot_cos_inner, spot_cos_outer`):

```cpp
                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, light_obj,
                                       light.color, base_color, point_position, point_color,
                                       point_range, light_view_proj * wt->matrix,
                                       spot_position, spot_direction, spot_color, spot_range,
                                       spot_cos_inner, spot_cos_outer});
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[render]"`
Expected: all `[render]`-tagged tests pass, including the two new ones. Also run the full suite to catch any other `DrawParams{...}` aggregate-init call site broken by the new fields:

Run: `./build/tests/mosaic_unit_tests`
Expected: same pass count as the pre-existing baseline plus the two new tests (the one known pre-existing threaded-ScriptSystem flake aside).

- [ ] **Step 7: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(render): RenderSystem spot-light object-space transform + DrawParams fields

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 3: Vulkan backend — push constants + shader

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (`PushConstants` struct, `static_assert`, `draw` copy)
- Modify: `modules/render_vulkan/shaders/cube.frag`

**Interfaces:**
- Consumes: `DrawParams::spot_position`/`spot_direction`/`spot_color`/`spot_range`/`spot_cos_inner`/`spot_cos_outer` (Task 2).
- Produces: nothing consumed by later tasks (renders the spot light on the GPU).

> No unit test — this is GPU/shader code with no device in the test build, exactly like every prior Vulkan-backend task in this project. The deliverable is verified by a clean build (shaders compile, `static_assert` holds) and a functional run in Task 4.

- [ ] **Step 1: Grow `PushConstants`**

Replace the `PushConstants` struct and its `static_assert` (three new `vec4`s: position+range, direction+cos_outer, color+cos_inner — packing each `Vec3`+`float` pair into one `vec4`, exactly like `point_position`/`point_color` already do):

```cpp
struct PushConstants
{
    float mvp[16];
    float light_direction[4]; // vec3 + pad
    float light_color[4];
    float base_color[4];
    float point_position[4]; // xyz = object-space pos, w = range
    float point_color[4];    // xyz = color * intensity
    float light_mvp[16];     // light_view_proj * model, for shadow sampling
    float spot_position[4];  // xyz = object-space pos, w = range
    float spot_direction[4]; // xyz = object-space direction, w = cos(outer_angle)
    float spot_color[4];     // xyz = color * intensity, w = cos(inner_angle)
};
static_assert(sizeof(PushConstants) == 256, "push constant layout must match the shader");
```

- [ ] **Step 2: Copy the new fields in `draw`**

In the `draw` method, directly after the existing `std::memcpy(pc.light_mvp, params.light_mvp.m.data(), sizeof(pc.light_mvp));` line and before `vkCmdPushConstants(...)`, add:

```cpp
        pc.spot_position[0] = params.spot_position.x;
        pc.spot_position[1] = params.spot_position.y;
        pc.spot_position[2] = params.spot_position.z;
        pc.spot_position[3] = params.spot_range;
        pc.spot_direction[0] = params.spot_direction.x;
        pc.spot_direction[1] = params.spot_direction.y;
        pc.spot_direction[2] = params.spot_direction.z;
        pc.spot_direction[3] = params.spot_cos_outer;
        pc.spot_color[0] = params.spot_color.x;
        pc.spot_color[1] = params.spot_color.y;
        pc.spot_color[2] = params.spot_color.z;
        pc.spot_color[3] = params.spot_cos_inner;
```

- [ ] **Step 3: Update the fragment shader**

Replace `modules/render_vulkan/shaders/cube.frag`:

```glsl
#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec4 fragLightSpacePos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
    vec4 point_position; // object space .xyz, .w = range
    vec4 point_color;    // .xyz = color * intensity
    mat4 light_mvp;
    vec4 spot_position;  // object space .xyz, .w = range
    vec4 spot_direction; // object space .xyz, .w = cos(outer_angle)
    vec4 spot_color;     // .xyz = color * intensity, .w = cos(inner_angle)
} pc;
layout(location = 0) out vec4 outColor;

// Hard shadow (v1, no PCF): 1.0 = fully lit, 0.0 = in shadow. A fragment whose
// light-clip-space position falls outside the fixed frustum's [0,1] UV range is
// treated as not shadowed (fully lit) -- see the design doc's Shading section.
float shadow_factor()
{
    vec3 proj = fragLightSpacePos.xyz / fragLightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
    {
        return 1.0;
    }
    const float bias = 0.0015; // constant bias; slope-scaling deferred (see design doc)
    float closest = texture(shadowMap, uv).r;
    return (proj.z - bias > closest) ? 0.0 : 1.0;
}

void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l)) * shadow_factor();
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (object space): windowed inverse-square attenuation. Unaffected
    // by shadows -- only the directional light casts shadows.
    vec3  toL   = pc.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = pc.point_position.w;
    float atten = 0.0;
    if (range > 0.0)
    {
        float win = clamp(1.0 - pow(d / range, 4.0), 0.0, 1.0);
        atten = (win * win) / (d * d + 1.0);
    }
    float pl = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    // Spot light (object space): same windowed inverse-square falloff as the point
    // light, additionally scaled by a smooth cone edge (smoothstep between the
    // outer and inner cosines -- full intensity inside the inner cone, smoothly
    // fading to zero at the outer cone, never a hard/aliased cutoff). No shadows.
    vec3  toSpotL   = pc.spot_position.xyz - fragPos;
    float spotD     = length(toSpotL);
    float spotRange = pc.spot_position.w;
    float spotAtten = 0.0;
    if (spotRange > 0.0)
    {
        float win = clamp(1.0 - pow(spotD / spotRange, 4.0), 0.0, 1.0);
        spotAtten = (win * win) / (spotD * spotD + 1.0);
    }
    float cosAngle = dot(normalize(-toSpotL), pc.spot_direction.xyz);
    float coneAtten = smoothstep(pc.spot_direction.w, pc.spot_color.w, cosAngle); // (cos_outer, cos_inner)
    float spotL = max(0.0, dot(n, toSpotL / max(spotD, 1e-4))) * spotAtten * coneAtten;

    vec3 color = albedo * (0.1 + lambert * pc.light_color.xyz)
               + albedo * pl * pc.point_color.xyz
               + albedo * spotL * pc.spot_color.xyz;
    outColor = vec4(color, 1.0);
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build 2>&1 | tail -60`
Expected: clean build — `cube.frag` compiles via `glslc`, `static_assert(sizeof(PushConstants) == 256)` holds, no link errors.

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/cube.frag
git commit -m "feat(render): Vulkan spot light (256-byte push constant + shader)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 4: End-to-end verification + build handoff

**Files:**
- Modify: `docs/project/next-steps.md` (prepend the shipment entry)
- Modify: `docs/project/deferred-work-backlog.md` (mark spot lights shipped)

**Interfaces:**
- Consumes: everything from Tasks 1-3.
- Produces: nothing (terminal task).

- [ ] **Step 1: Full unit suite**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests 2>&1 | tail -15`
Expected: same pass count as the pre-existing baseline plus the new Task 1/2 tests (the one known pre-existing threaded-ScriptSystem flake aside).

Also run via the exact CI gate:
Run: `cmake --build --preset debug && ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"`
Expected: 100% pass (these two are excluded from the CI gate for pre-existing, unrelated flake reasons recorded in `docs/project/deferred-work-backlog.md`'s Tech debt section).

**Known hazard from the shadows v1 plan, repeated here so it isn't relearned the hard way:** `build/debug/` (the CMake-preset directory `ctest --preset debug` uses) is a separate build tree from an ad hoc `build/` directory some tasks above build into. Confirm the `ctest --preset debug` run is against a binary that was actually freshly linked (check its mtime is recent, e.g. `ls -la build/debug/tests/mosaic_unit_tests`) — a build invocation that appears to return quickly may still have its link step running in the background; verify the binary's timestamp advanced, don't just confirm the file exists.

- [ ] **Step 2: Functional windowed run**

Run: `PROJ=$(mktemp -d)/spot_demo && ./build/debug/app/mosaic create "$PROJ" && ./build/debug/app/mosaic run --window "$PROJ" --duration 2`
Expected: exit 0, no error output on stderr. (Visual confirmation that the spot light actually looks right needs real hardware outside this sandbox, per this project's established GPU-screenshot limitation — functional exit is the acceptance here.)

- [ ] **Step 3: Functional editor run**

Run: `PROJ=$(mktemp -d)/spot_demo_editor && ./build/debug/app/mosaic create "$PROJ" && timeout 3 ./build/debug/app/mosaic editor "$PROJ"; echo "exit=$?"`
Expected: clean startup for the full 3 seconds (no Vulkan validation errors), terminated only by the `timeout` wrapper — the shadows v1 plan's editor-pass-ordering bug was specific to the shadow pass's placement, but re-verify here since this task also touches the shared `draw()` push-constant path both entry points use.

- [ ] **Step 4: Update the build handoff**

Prepend an entry to the top of `docs/project/next-steps.md` (after the file's header, before the current top entry), in that file's established dense per-entry voice — describe what shipped (a third simultaneous light type, the push-constant growth to 256 bytes and its being the documented ceiling, the deferred specular/PBR/multi-light follow-ups), test counts, and verification results. Follow the exact structure of the entries already at the top of that file.

- [ ] **Step 5: Update the deferred-work backlog**

In `docs/project/deferred-work-backlog.md`'s Rendering section, mark the "spot lights" sub-item of the "Specular/PBR, spot lights" decomposition as shipped (mirroring how the shadows v1 entry was marked: `~~item~~` with a **SHIPPED** note and date), leaving the sibling deferred bullets (Blinn-Phong specular, full PBR, multi-light/UBO) exactly as they are, since none of those are built by this plan.

- [ ] **Step 6: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "docs: record spot lights shipment

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```
