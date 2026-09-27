# Rendering — Shading-Data UBO Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move per-frame lighting off push constants into a uniform buffer and shift shading from object space to world space, dropping the per-draw payload from 256 bytes (the hard ceiling) to 208.

**Architecture:** Lights stop being transformed into each mesh's object space; their world-space values go into a small `FrameLighting` uniform buffer written once per frame (descriptor set 2), while per-draw push constants carry only `mvp`, `model`, a normal matrix, and `base_color`. The shaders do all lighting in world space and derive the shadow lookup from the UBO's `light_view_proj`.

**Tech Stack:** C++20, Vulkan 1.x (`modules/render_vulkan`), GLSL → SPIR-V (glslc), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-27-render-shading-ubo-migration-design.md`

## Global Constraints

- Output must be **identical for uniformly-scaled objects** — this is a refactor plus a latent-bug fix, not a visible change to existing content.
- Point/spot attenuation distance and normals **become correct under non-uniform mesh scale** (the limitation 36e-1 documented). This is the one intentional behavior change.
- The falloff formulas themselves — windowed inverse-square distance attenuation, the spot cone `smoothstep`, the shadow bias and out-of-frustum fallback — are **carried over unchanged**; only the space they operate in changes.
- Per-draw push constants must total **exactly 208 bytes**, guarded by `static_assert`.
- `RenderSystem::render_shadows` and the depth-only shadow pipeline are **not touched** by this slice.
- One light per type ("first found") is **unchanged** — this slice moves data, it does not add multi-light support.
- No new `Material` fields and no specular math — Blinn-Phong is the next slice, deliberately not bundled.

## Review Focus

Five failure modes the spec implies that no obvious task test would otherwise cover. Each has a test pinned to the task that owns the code (all land in Task 2).

1. **A frame with no meshes must still call `set_frame_lighting`.** If the call were inside or gated by the mesh loop, the UBO would keep a previous frame's contents — or on frame 0, uninitialized memory — and a later frame with meshes could shade from garbage. This is the ordering hazard the refactor itself introduces.
2. **Non-uniformly-scaled mesh normals.** The spec claims this migration fixes non-uniform scale; the current suite has no non-uniform-scale coverage at all, so "fixed" is unverified by default. The normal matrix must be the inverse-transpose, not the model matrix.
3. **A world with no lights at all.** `active_light` falls back to a default white light and point/spot are absent; the UBO must carry that default with zero ranges so the two positional lights contribute nothing rather than shading from stale or zeroed-out garbage.
4. **A zero-length `DirectionalLight.direction`** (authorable in the editor and in scene files). `normalize` already returns the zero vector rather than NaN, so the correct behavior is "unlit, no NaN" — but after this change the value is shared by every fragment via the UBO instead of being per-mesh, so it is worth pinning rather than assuming.
5. **A zero-scaled mesh `Transform`** (also authorable), making the model matrix singular. `Mat4::inverse_affine` already returns identity for a singular linear part, so the normal matrix must come out finite — again worth pinning, since the normal matrix is new load-bearing state.

Note for the implementer: failure modes 4 and 5 are **already handled** by the existing `normalize` and `inverse_affine` guards. Their tests pin that behavior; **do not add redundant guard code** for them.

---

### Task 1: `Mat4::transpose`

**Files:**
- Modify: `foundation/math/mat4.hpp` (declare + implement `transpose`)
- Test: `tests/unit/math/mat4_test.cpp`

**Interfaces:**
- Consumes: nothing new (existing `Mat4`, column-major `m[16]`, `element(row,col) == m[col*4+row]`).
- Produces: `Mat4::transpose(const Mat4& m) -> Mat4` — consumed by Task 2 to build the normal matrix.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/math/mat4_test.cpp`:

```cpp
TEST_CASE("transpose swaps rows and columns", "[math]")
{
    Mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = static_cast<float>(i);
    }
    const Mat4 t = Mat4::transpose(m);
    // element(row,col) == m[col*4+row], so transpose(m)[col*4+row] == m[row*4+col].
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            REQUIRE(t.m[col * 4 + row] == Approx(m.m[row * 4 + col]));
        }
    }
}

TEST_CASE("transpose of identity is identity, and transposing twice round-trips", "[math]")
{
    const Mat4 id = Mat4::identity();
    const Mat4 t = Mat4::transpose(id);
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(t.m[i] == Approx(id.m[i]));
    }

    const Mat4 r = Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const Mat4 rtt = Mat4::transpose(Mat4::transpose(r));
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(rtt.m[i] == Approx(r.m[i]));
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -20`
Expected: compile error — `transpose` is not a member of `Mat4`.

- [ ] **Step 3: Declare and implement `transpose`**

In `foundation/math/mat4.hpp`, add the declaration alongside the other statics (directly after the `orthographic` line):

```cpp
    static Mat4 transpose(const Mat4& m);
```

Add the implementation directly after `Mat4::orthographic`'s definition:

```cpp
inline Mat4 Mat4::transpose(const Mat4& m)
{
    Mat4 out;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            out.m[col * 4 + row] = m.m[row * 4 + col];
        }
    }
    return out;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[math]"`
Expected: all `[math]` tests pass, including the two new ones.

- [ ] **Step 5: Commit**

```bash
git add foundation/math/mat4.hpp tests/unit/math/mat4_test.cpp
git commit -m "feat(math): add Mat4::transpose

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: `FrameLighting` + reshaped `DrawParams` + world-space `RenderSystem`

**Files:**
- Modify: `engine/render/render_device.hpp` (`FrameLighting`, reshaped `DrawParams`, `set_frame_lighting` hook)
- Modify: `engine/render/render_system.hpp` (build `FrameLighting`, world-space per-draw params)
- Modify: `tests/unit/render/render_system_test.cpp` (rewrite 7 lighting tests, add 5 new)

**Interfaces:**
- Consumes: `Mat4::transpose` (Task 1).
- Produces, all consumed by Task 3:
  - `struct FrameLighting { Vec3 light_direction; Vec3 light_color; Vec3 point_position; Vec3 point_color; float point_range; Vec3 spot_position; Vec3 spot_direction; Vec3 spot_color; float spot_range; float spot_cos_inner; float spot_cos_outer; Mat4 light_view_proj; }`
  - `struct DrawParams { Mat4 mvp; Mat4 model; Mat4 normal_matrix; Vec3 base_color; }`
  - `virtual void RenderDevice::set_frame_lighting(const FrameLighting&) {}`

- [ ] **Step 1: Reshape the engine-layer types**

In `engine/render/render_device.hpp`, **replace** the entire existing `DrawParams` struct with these two structs:

```cpp
// Per-frame, world-space lighting shared by every draw this frame. One light of
// each type ("first found"), matching the RenderSystem accessors. A zero `range`
// means that positional light is absent and contributes nothing.
struct FrameLighting
{
    mosaic::foundation::math::Vec3 light_direction;  // world space, normalized
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 point_position;   // world space
    mosaic::foundation::math::Vec3 point_color;      // premultiplied by intensity
    float                          point_range = 0.0f; // 0 == no point light
    mosaic::foundation::math::Vec3 spot_position;    // world space
    mosaic::foundation::math::Vec3 spot_direction;   // world space, normalized
    mosaic::foundation::math::Vec3 spot_color;       // premultiplied by intensity
    float                          spot_range = 0.0f;  // 0 == no spot light
    float                          spot_cos_inner = 1.0f;
    float                          spot_cos_outer = 1.0f;
    mosaic::foundation::math::Mat4 light_view_proj;  // for shadow-map sampling
};

// Per-draw parameters. Lighting lives in FrameLighting now; shading is done in
// WORLD space, so the backend needs the model matrix (to place the fragment) and
// the normal matrix (transpose of the inverse model, so normals stay correct
// under non-uniform scale).
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Mat4 model;         // object -> world
    mosaic::foundation::math::Mat4 normal_matrix; // transpose(inverse_affine(model)); upper 3x3 used
    mosaic::foundation::math::Vec3 base_color;
};
```

Add the hook to `RenderDevice`, directly after the three shadow hooks:

```cpp
    // Per-frame world-space lighting, uploaded once before the frame's mesh draws.
    // Default no-op: a backend with no uniform-buffer path simply renders unlit
    // rather than failing.
    virtual void set_frame_lighting(const FrameLighting& /*lighting*/) {}
```

- [ ] **Step 2: Rewrite `RenderSystem::render`'s lighting**

In `engine/render/render_system.hpp`, inside `render(...)`, **replace** everything from `const Mat4 light_view_proj = directional_light_view_projection(light);` through the closing `});` of the `world.for_each<MeshRenderer>` mesh loop with:

```cpp
        const Mat4 light_view_proj = directional_light_view_projection(light);

        // Per-frame, world-space lighting. Sent unconditionally, before (and
        // independently of) any mesh draw -- a frame with no meshes must still
        // refresh the backend's uniform buffer, or it would shade from a previous
        // frame's contents.
        FrameLighting frame_lighting;
        frame_lighting.light_direction = mosaic::foundation::math::normalize(light.direction);
        frame_lighting.light_color = light.color;
        if (point.present)
        {
            frame_lighting.point_position = point.world_pos;
            frame_lighting.point_color = point.color;
            frame_lighting.point_range = point.range;
        }
        if (spot.present)
        {
            frame_lighting.spot_position = spot.world_pos;
            frame_lighting.spot_direction = spot.direction;
            frame_lighting.spot_color = spot.color;
            frame_lighting.spot_range = spot.range;
            frame_lighting.spot_cos_inner = spot.cos_inner;
            frame_lighting.spot_cos_outer = spot.cos_outer;
        }
        frame_lighting.light_view_proj = light_view_proj;
        device.set_frame_lighting(frame_lighting);

        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                const MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device);
                if (mesh == 0) // nil / unregistered / failed upload -> nothing to draw
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                Vec3 base_color{0.8f, 0.8f, 0.8f};
                TextureHandle texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    texture = texture_cache_.get(mat->texture, registry, device);
                }
                if (texture == 0)
                {
                    texture = default_white(device);
                }

                // Normals transform by the inverse-transpose of the model matrix, or
                // non-uniformly-scaled meshes shade wrong. inverse_affine already
                // returns identity for a singular linear part (zero scale), so this
                // stays finite without extra guarding.
                const Mat4 normal_matrix = Mat4::transpose(Mat4::inverse_affine(wt->matrix));

                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, wt->matrix,
                                       normal_matrix, base_color});
            });
```

The `PointLightState`/`SpotLightState` accessors, `active_light`, and
`render_shadows` are unchanged — only how their values are consumed changes.

- [ ] **Step 3: Teach `FakeRenderDevice` to record frame lighting**

In `tests/unit/render/render_system_test.cpp`, add to the `using` block near the top:

```cpp
using mosaic::engine::render::FrameLighting;
```

Add to `FakeRenderDevice` (directly after the `end_shadow_pass` override):

```cpp
    std::vector<FrameLighting> frame_lighting_calls;
    void set_frame_lighting(const FrameLighting& fl) override
    {
        frame_lighting_calls.push_back(fl);
    }
```

- [ ] **Step 4: Rewrite the 7 object-space lighting tests to world space**

Replace `"RenderSystem produces lit DrawParams from Material + DirectionalLight"` in its entirety with:

```cpp
TEST_CASE("RenderSystem reports world-space directional light in FrameLighting, untouched by mesh rotation", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    // Light NOT aligned with the mesh's rotation axis (Y): if any leftover
    // object-space transform were still applied, the result would visibly differ
    // from the world direction -- which the final guard below checks for.
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{1, -1, 0}, Vec3{1, 0.9f, 0.8f}});

    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});
    world.add_component<Material>(mesh, Material{Vec3{0.2f, 0.4f, 0.6f}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.frame_lighting_calls.size() == 1);
    const DrawParams&    p  = device.draws[0].params;
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(device.draws[0].mesh != 0);

    // base_color stays per-draw.
    REQUIRE(p.base_color.x == Approx(0.2f));
    REQUIRE(p.base_color.y == Approx(0.4f));
    REQUIRE(p.base_color.z == Approx(0.6f));

    // Light color and direction are now per-frame and in WORLD space.
    REQUIRE(fl.light_color.x == Approx(1.0f));
    REQUIRE(fl.light_color.z == Approx(0.8f));
    const Vec3 world_norm = mosaic::foundation::math::normalize(Vec3{1, -1, 0});
    REQUIRE(fl.light_direction.x == Approx(world_norm.x).margin(1e-5));
    REQUIRE(fl.light_direction.y == Approx(world_norm.y).margin(1e-5));
    REQUIRE(fl.light_direction.z == Approx(world_norm.z).margin(1e-5));

    // Guard, inverted from the object-space era: the mesh's 90-degree rotation must
    // NOT affect the light direction any more. If a leftover object-space transform
    // is applied, the x component differs from the world value by well over 0.1.
    const Vec3 object_space = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(Mat4::inverse_affine(mesh_world),
                                                      Vec3{1, -1, 0}));
    REQUIRE(std::abs(fl.light_direction.x - object_space.x) > 0.1f);
}
```

Replace `"RenderSystem::render_shadows records the shadow pass with the right light_mvp, matching render()'s own light_mvp"` in its entirety with:

```cpp
TEST_CASE("render_shadows' light_mvp agrees with FrameLighting's light_view_proj and the per-draw model", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    const Mat4 mesh_world = Mat4::translation(Vec3{1.0f, 0.0f, 0.0f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render_shadows(world, device, registry);
    rs.render(world, device, registry);

    REQUIRE(device.shadow_pass_begins == 1);
    REQUIRE(device.shadow_pass_ends == 1);
    REQUIRE(device.shadow_draws.size() == 1);
    REQUIRE(device.shadow_draws[0].mesh == device.draws[0].mesh);
    REQUIRE(device.frame_lighting_calls.size() == 1);

    const Mat4 light_vp = mosaic::engine::render::directional_light_view_projection(
        DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    // The shadow pass still gets a fully composed light_mvp (its own pipeline is
    // untouched by this migration).
    const Mat4 expected_shadow = light_vp * mesh_world;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(device.shadow_draws[0].light_mvp.m[i] == Approx(expected_shadow.m[i]).margin(1e-4));
    }

    // The main pass now composes it in the shader from these two halves. If they ever
    // disagree with what the shadow pass rendered, the shadow map would misalign --
    // so assert both halves rather than the product.
    const FrameLighting& fl = device.frame_lighting_calls[0];
    const DrawParams&    p  = device.draws[0].params;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(fl.light_view_proj.m[i] == Approx(light_vp.m[i]).margin(1e-4));
        REQUIRE(p.model.m[i] == Approx(mesh_world.m[i]).margin(1e-4));
    }
}
```

In `"RenderSystem uses default base color and light when none present"`, replace the two final assertions:

```cpp
    REQUIRE(p.base_color.x == Approx(0.8f)); // default material
    REQUIRE(p.light_color.x == Approx(1.0f)); // default light is white
```

with:

```cpp
    REQUIRE(p.base_color.x == Approx(0.8f)); // default material
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].light_color.x == Approx(1.0f)); // default light is white
```

Replace `"RenderSystem transforms point light into mesh object space"` in its entirety with:

```cpp
TEST_CASE("RenderSystem reports point light in world space, unaffected by mesh rotation", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<PointLight>(light, PointLight{Vec3{0.5f, 0.25f, 0.1f}, 2.0f, 6.0f});

    // Mesh rotated 90 degrees about Y -- kept deliberately from the object-space era:
    // under the old behavior this produced object-space {0,0,2}, so if any leftover
    // transform is applied these assertions fail.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];

    REQUIRE(fl.point_position.x == Approx(2.0f).margin(1e-5));
    REQUIRE(fl.point_position.y == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.point_position.z == Approx(0.0f).margin(1e-5));

    REQUIRE(fl.point_color.x == Approx(1.0f));   // 0.5 * 2
    REQUIRE(fl.point_color.y == Approx(0.5f));   // 0.25 * 2
    REQUIRE(fl.point_color.z == Approx(0.2f));   // 0.1 * 2
    REQUIRE(fl.point_range == Approx(6.0f));
}
```

Replace `"RenderSystem transforms spot light position and direction into mesh object space"` in its entirety with:

```cpp
TEST_CASE("RenderSystem reports spot light position and direction in world space", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<SpotLight>(light, SpotLight{Vec3{0.3f, 0.6f, 0.9f}, 2.0f, 8.0f, 0.4f, 0.6f});

    // Same deliberately-rotated mesh as before: under object-space shading this
    // produced position {0,0,2} and direction {1,0,0}, so a leftover transform fails here.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];

    REQUIRE(fl.spot_position.x == Approx(2.0f).margin(1e-5));
    REQUIRE(fl.spot_position.y == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.spot_position.z == Approx(0.0f).margin(1e-5));

    // Identity-rotation light, so Mat4::forward() gives world -Z.
    REQUIRE(fl.spot_direction.x == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.spot_direction.y == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.spot_direction.z == Approx(-1.0f).margin(1e-5));

    REQUIRE(fl.spot_color.x == Approx(0.6f));  // 0.3 * 2
    REQUIRE(fl.spot_color.y == Approx(1.2f));  // 0.6 * 2
    REQUIRE(fl.spot_color.z == Approx(1.8f));  // 0.9 * 2
    REQUIRE(fl.spot_range == Approx(8.0f));
    REQUIRE(fl.spot_cos_inner == Approx(std::cos(0.4f)));
    REQUIRE(fl.spot_cos_outer == Approx(std::cos(0.6f)));
}
```

In `"RenderSystem disables spot light when none present"`, replace the final assertion
`REQUIRE(device.draws[0].params.spot_range == Approx(0.0f)); // 0 == disabled` with:

```cpp
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].spot_range == Approx(0.0f)); // 0 == disabled
```

In `"RenderSystem disables point light when none present"`, replace its equivalent
`point_range` assertion on `device.draws[0].params` with:

```cpp
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].point_range == Approx(0.0f)); // 0 == disabled
```

- [ ] **Step 5: Add the five Review Focus tests**

Append to `tests/unit/render/render_system_test.cpp`:

```cpp
TEST_CASE("RenderSystem sets frame lighting even when the scene has no meshes", "[render]")
{
    // Review Focus 1: if the UBO were only refreshed alongside mesh draws, an empty
    // frame would leave the backend shading from the previous frame's buffer -- or,
    // on the very first frame, from uninitialized memory.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.empty());
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].light_color.x == Approx(1.0f));
}

TEST_CASE("RenderSystem's normal_matrix is the inverse-transpose under non-uniform scale", "[render]")
{
    // Review Focus 2: the migration claims to fix non-uniform scale. A mesh scaled 2x
    // on X only must get normals scaled by 1/2 on X -- passing the model matrix
    // straight through would give 2x, which is exactly the bug this pins.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const Mat4 mesh_world = Mat4::scale(Vec3{2.0f, 1.0f, 1.0f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.normal_matrix.m[0] == Approx(0.5f).margin(1e-5));
    REQUIRE(p.normal_matrix.m[5] == Approx(1.0f).margin(1e-5));
    REQUIRE(p.normal_matrix.m[10] == Approx(1.0f).margin(1e-5));
    // Guard: the model matrix itself carries 2.0 there, so this fails loudly if
    // someone passes `model` as the normal matrix.
    REQUIRE(p.model.m[0] == Approx(2.0f));
}

TEST_CASE("RenderSystem emits a sane default FrameLighting for a world with no lights", "[render]")
{
    // Review Focus 3: no lights at all -- the default directional light must come
    // through and both positional lights must be range 0 (contributing nothing).
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

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(fl.light_color.x == Approx(1.0f));
    REQUIRE(fl.point_range == Approx(0.0f));
    REQUIRE(fl.spot_range == Approx(0.0f));
    // The default direction is normalized, so it is a unit vector rather than zero.
    const float len = mosaic::foundation::math::length(fl.light_direction);
    REQUIRE(len == Approx(1.0f).margin(1e-5));
}

TEST_CASE("RenderSystem emits no NaN for a zero-length directional light direction", "[render]")
{
    // Review Focus 4: authorable in the editor and in scene files. normalize()
    // already returns the zero vector rather than NaN, so the expected behavior is
    // "unlit, still finite" -- pinned here because the value is now shared by every
    // fragment through the UBO instead of being recomputed per mesh.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, 0, 0}, Vec3{1, 1, 1}});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(std::isfinite(fl.light_direction.x));
    REQUIRE(std::isfinite(fl.light_direction.y));
    REQUIRE(std::isfinite(fl.light_direction.z));
}

TEST_CASE("RenderSystem emits finite per-draw matrices for a zero-scaled mesh", "[render]")
{
    // Review Focus 5: a zero-scaled Transform makes the model matrix singular.
    // Mat4::inverse_affine already returns identity for a singular linear part, so
    // the normal matrix must stay finite rather than filling with NaN/inf.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::scale(Vec3{1.0f, 1.0f, 0.0f})});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(std::isfinite(p.normal_matrix.m[i]));
    }
}
```

Ensure `#include <cmath>` is present at the top of the file (needed for `std::isfinite`
and `std::cos`); it was added by the spot-lights slice, so verify rather than duplicate.

- [ ] **Step 6: Build and run the render tests**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[render]"`
Expected: all `[render]` tests pass, including the 5 new ones.

- [ ] **Step 7: Run the full suite**

Run: `./build/tests/mosaic_unit_tests 2>&1 | tail -10`
Expected: the pre-existing baseline plus the new tests. The one known pre-existing
threaded-ScriptSystem SIGSEGV flake (`script_system_test.cpp:191`) is acceptable and
unrelated — do not chase it.

- [ ] **Step 8: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "refactor(render): world-space shading via per-frame FrameLighting

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Vulkan backend — uniform buffer, descriptor set 2, world-space shaders

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: `modules/render_vulkan/shaders/cube.vert`
- Modify: `modules/render_vulkan/shaders/cube.frag`

**Interfaces:**
- Consumes: `FrameLighting`, reshaped `DrawParams`, `set_frame_lighting` (Task 2).
- Produces: nothing consumed by later tasks (renders on the GPU).

> No unit test — GPU/shader code with no device in the test build, exactly like every prior Vulkan-backend task here. Verified by a clean build (shaders compile, `static_assert`s hold) and the functional runs in Task 4.

- [ ] **Step 1: Replace the push-constant struct and add the UBO struct**

Replace the `PushConstants` struct and its `static_assert` with:

```cpp
struct PushConstants
{
    float mvp[16];
    float model[16];         // object -> world
    float normal_matrix[16]; // transpose(inverse(model)); only the upper 3x3 is used
    float base_color[4];     // xyz
};
static_assert(sizeof(PushConstants) == 208, "push constant layout must match the shader");

// std140 layout, matching the FrameLighting uniform block in cube.vert/cube.frag.
struct FrameLightingUbo
{
    float light_direction[4]; // xyz, w unused
    float light_color[4];     // xyz, w unused
    float point_position[4];  // xyz, w = range
    float point_color[4];     // xyz, w unused
    float spot_position[4];   // xyz, w = range
    float spot_direction[4];  // xyz, w = cos(outer_angle)
    float spot_color[4];      // xyz, w = cos(inner_angle)
    float light_view_proj[16];
};
static_assert(sizeof(FrameLightingUbo) == 176, "UBO layout must match the shader block");
```

- [ ] **Step 2: Add the UBO and descriptor member state**

Near the existing shadow member state (after the `shadow_pipeline_` member), add:

```cpp
    // Per-frame lighting UBO -- one per frame-in-flight, so writing frame N cannot
    // race a still-executing frame N-1 reading its own copy. Persistently mapped.
    VkBuffer              frame_ubo_[kFramesInFlight]{};
    VkDeviceMemory        frame_ubo_memory_[kFramesInFlight]{};
    void*                 frame_ubo_mapped_[kFramesInFlight]{};
    VkDescriptorSetLayout frame_ubo_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      frame_ubo_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       frame_ubo_set_[kFramesInFlight]{};
```

- [ ] **Step 3: Create the buffers, layout, pool, and sets**

Add this method next to `create_shadow_descriptor()`:

```cpp
    // Set 2 in the main pipeline: per-frame world-space lighting. Visible to BOTH
    // stages -- the vertex shader reads light_view_proj to compute the shadow
    // lookup position, the fragment shader reads the lights themselves.
    void create_frame_lighting_resources()
    {
        for (int i = 0; i < kFramesInFlight; ++i)
        {
            create_buffer(sizeof(FrameLightingUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame_ubo_[i], frame_ubo_memory_[i]);
            vk_check(vkMapMemory(device_, frame_ubo_memory_[i], 0, sizeof(FrameLightingUbo), 0,
                                 &frame_ubo_mapped_[i]),
                     "vkMapMemory(frame lighting ubo)");
            std::memset(frame_ubo_mapped_[i], 0, sizeof(FrameLightingUbo));
        }

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = 1;
        layout.pBindings = &binding;
        vk_check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &frame_ubo_set_layout_),
                 "vkCreateDescriptorSetLayout(frame lighting)");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = kFramesInFlight;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = kFramesInFlight;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &frame_ubo_pool_),
                 "vkCreateDescriptorPool(frame lighting)");

        for (int i = 0; i < kFramesInFlight; ++i)
        {
            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = frame_ubo_pool_;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &frame_ubo_set_layout_;
            vk_check(vkAllocateDescriptorSets(device_, &alloc, &frame_ubo_set_[i]),
                     "vkAllocateDescriptorSets(frame lighting)");

            VkDescriptorBufferInfo info{};
            info.buffer = frame_ubo_[i];
            info.offset = 0;
            info.range = sizeof(FrameLightingUbo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame_ubo_set_[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &info;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }
```

- [ ] **Step 4: Implement `set_frame_lighting`**

Add next to the shadow-pass methods:

```cpp
    void set_frame_lighting(const mosaic::engine::render::FrameLighting& fl) override
    {
        FrameLightingUbo ubo{};
        ubo.light_direction[0] = fl.light_direction.x;
        ubo.light_direction[1] = fl.light_direction.y;
        ubo.light_direction[2] = fl.light_direction.z;
        ubo.light_color[0] = fl.light_color.x;
        ubo.light_color[1] = fl.light_color.y;
        ubo.light_color[2] = fl.light_color.z;
        ubo.point_position[0] = fl.point_position.x;
        ubo.point_position[1] = fl.point_position.y;
        ubo.point_position[2] = fl.point_position.z;
        ubo.point_position[3] = fl.point_range;
        ubo.point_color[0] = fl.point_color.x;
        ubo.point_color[1] = fl.point_color.y;
        ubo.point_color[2] = fl.point_color.z;
        ubo.spot_position[0] = fl.spot_position.x;
        ubo.spot_position[1] = fl.spot_position.y;
        ubo.spot_position[2] = fl.spot_position.z;
        ubo.spot_position[3] = fl.spot_range;
        ubo.spot_direction[0] = fl.spot_direction.x;
        ubo.spot_direction[1] = fl.spot_direction.y;
        ubo.spot_direction[2] = fl.spot_direction.z;
        ubo.spot_direction[3] = fl.spot_cos_outer;
        ubo.spot_color[0] = fl.spot_color.x;
        ubo.spot_color[1] = fl.spot_color.y;
        ubo.spot_color[2] = fl.spot_color.z;
        ubo.spot_color[3] = fl.spot_cos_inner;
        std::memcpy(ubo.light_view_proj, fl.light_view_proj.m.data(), sizeof(ubo.light_view_proj));

        // frame_ is the current frame-in-flight index (advanced at the end of
        // end_frame), so this always targets the buffer this frame will read.
        std::memcpy(frame_ubo_mapped_[frame_], &ubo, sizeof(ubo));
    }
```

- [ ] **Step 5: Update `draw()` — push constants and the set-2 bind**

In `draw()`, replace the whole block from `PushConstants pc{};` through the
`vkCmdPushConstants(...)` call with:

```cpp
        PushConstants pc{};
        std::memcpy(pc.mvp, params.mvp.m.data(), sizeof(pc.mvp));
        std::memcpy(pc.model, params.model.m.data(), sizeof(pc.model));
        std::memcpy(pc.normal_matrix, params.normal_matrix.m.data(), sizeof(pc.normal_matrix));
        pc.base_color[0] = params.base_color.x;
        pc.base_color[1] = params.base_color.y;
        pc.base_color[2] = params.base_color.z;
        vkCmdPushConstants(cmd_[frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);
```

Directly after the existing set-1 (shadow) `vkCmdBindDescriptorSets` call, add the set-2 bind:

```cpp
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 2, 1, &frame_ubo_set_[frame_], 0, nullptr);
```

- [ ] **Step 6: Extend the main pipeline layout to three sets**

In `create_pipeline()`, replace the two-element set-layout array with:

```cpp
        std::array<VkDescriptorSetLayout, 3> set_layouts{texture_set_layout_, shadow_set_layout_,
                                                         frame_ubo_set_layout_};
```

(The `layout.setLayoutCount` / `layout.pSetLayouts` lines already derive from
`set_layouts.size()` / `.data()`, so they need no change.)

- [ ] **Step 7: Wire construction and destruction**

In the constructor, add directly after `create_shadow_descriptor();`:

```cpp
        create_frame_lighting_resources();
```

In the destructor, add alongside the shadow teardown block:

```cpp
            if (frame_ubo_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, frame_ubo_pool_, nullptr); }
            if (frame_ubo_set_layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, frame_ubo_set_layout_, nullptr); }
            for (int i = 0; i < kFramesInFlight; ++i)
            {
                if (frame_ubo_memory_[i] != VK_NULL_HANDLE) { vkUnmapMemory(device_, frame_ubo_memory_[i]); }
                if (frame_ubo_[i] != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame_ubo_[i], nullptr); }
                if (frame_ubo_memory_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, frame_ubo_memory_[i], nullptr); }
            }
```

- [ ] **Step 8: Rewrite the vertex shader for world space**

Replace `modules/render_vulkan/shaders/cube.vert`:

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;         // object -> world
    mat4 normal_matrix; // transpose(inverse(model)); upper 3x3 used
    vec4 base_color;    // .xyz
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    mat4 light_view_proj;
} fl;
layout(location = 0) out vec3 fragNormal;   // world space
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;      // world space
layout(location = 3) out vec4 fragLightSpacePos;
void main()
{
    vec4 world = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = mat3(pc.normal_matrix) * inNormal; // world space
    fragUV = inUV;
    fragPos = world.xyz;                            // world space
    fragLightSpacePos = fl.light_view_proj * world;
}
```

- [ ] **Step 9: Rewrite the fragment shader for world space**

Replace `modules/render_vulkan/shaders/cube.frag`:

```glsl
#version 450
layout(location = 0) in vec3 fragNormal;   // world space
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;      // world space
layout(location = 3) in vec4 fragLightSpacePos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;
    mat4 normal_matrix;
    vec4 base_color;
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    mat4 light_view_proj;
} fl;
layout(location = 0) out vec4 outColor;

// Hard shadow (no PCF): 1.0 = fully lit, 0.0 = in shadow. A fragment whose
// light-clip-space position falls outside the fixed frustum's [0,1] UV range is
// treated as not shadowed (fully lit). Carried over unchanged from shadows v1.
float shadow_factor()
{
    vec3 proj = fragLightSpacePos.xyz / fragLightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
    {
        return 1.0;
    }
    const float bias = 0.0015;
    float closest = texture(shadowMap, uv).r;
    return (proj.z - bias > closest) ? 0.0 : 1.0;
}

void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(fl.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l)) * shadow_factor();
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (world space): windowed inverse-square attenuation. Formula
    // unchanged from 36e-1; only the space changed, which is what makes the
    // distance correct under non-uniform mesh scale.
    vec3  toL   = fl.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = fl.point_position.w;
    float atten = 0.0;
    if (range > 0.0)
    {
        float win = clamp(1.0 - pow(d / range, 4.0), 0.0, 1.0);
        atten = (win * win) / (d * d + 1.0);
    }
    float pl = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    // Spot light (world space): same falloff plus the smooth cone edge. Formula
    // unchanged from the spot-lights slice.
    vec3  toSpotL   = fl.spot_position.xyz - fragPos;
    float spotD     = length(toSpotL);
    float spotRange = fl.spot_position.w;
    float spotAtten = 0.0;
    if (spotRange > 0.0)
    {
        float win = clamp(1.0 - pow(spotD / spotRange, 4.0), 0.0, 1.0);
        spotAtten = (win * win) / (spotD * spotD + 1.0);
    }
    float cosAngle = dot(normalize(-toSpotL), fl.spot_direction.xyz);
    float coneAtten = smoothstep(fl.spot_direction.w, fl.spot_color.w, cosAngle); // (cos_outer, cos_inner)
    float spotL = max(0.0, dot(n, toSpotL / max(spotD, 1e-4))) * spotAtten * coneAtten;

    vec3 color = albedo * (0.1 + lambert * fl.light_color.xyz)
               + albedo * pl * fl.point_color.xyz
               + albedo * spotL * fl.spot_color.xyz;
    outColor = vec4(color, 1.0);
}
```

- [ ] **Step 10: Build**

Run: `cmake --build build 2>&1 | tail -40`
Expected: clean build — both shaders compile via `glslc`, both `static_assert`s
(`PushConstants == 208`, `FrameLightingUbo == 176`) hold, no link errors.

- [ ] **Step 11: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag
git commit -m "feat(render): per-frame lighting UBO + world-space shaders

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: End-to-end verification + build handoff

**Files:**
- Modify: `docs/project/next-steps.md` (prepend the shipment entry)
- Modify: `docs/project/deferred-work-backlog.md` (mark the migration shipped)

**Interfaces:**
- Consumes: everything from Tasks 1-3.
- Produces: nothing (terminal task).

- [ ] **Step 1: Full unit suite**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests 2>&1 | tail -12`
Expected: baseline plus the new tests; only the known threaded-ScriptSystem flake may fail.

- [ ] **Step 2: The exact CI gate**

Run: `cmake --build --preset debug`

**Wait for that build to actually finish before continuing** — this is a separate
build tree from `build/`, and a previous slice lost time to an apparently-returned
build whose link step was still running. Confirm with
`ls -la build/debug/tests/mosaic_unit_tests` that the binary's mtime is current;
checking that the path merely *exists* is not the same as the build having finished.

Then run:
`ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"`
Expected: 100% pass (those two are excluded from the CI gate for pre-existing,
unrelated flake reasons recorded in the backlog's Tech debt section).

- [ ] **Step 3: Functional windowed run**

Run: `PROJ=$(mktemp -d)/ubo_demo && ./build/debug/app/mosaic create "$PROJ" && ./build/debug/app/mosaic run --window "$PROJ" --duration 2`
Expected: exit 0, **no Vulkan validation errors on stderr**. Validation errors are the
main signal here — a mis-declared descriptor set, a stage-visibility mistake, or a
push-constant/shader layout mismatch shows up loudly as validation output.

- [ ] **Step 4: Functional editor run**

Run: `PROJ=$(mktemp -d)/ubo_editor && ./build/debug/app/mosaic create "$PROJ" && timeout 3 ./build/debug/app/mosaic editor "$PROJ"; echo "exit=$?"`
Expected: clean startup for the full 3 seconds, no validation errors, terminated only
by the `timeout` wrapper (`exit=124`). The editor path renders three passes (shadow,
offscreen viewport, swapchain) and shares the same `draw()`/descriptor path, so it
exercises the set-2 binding under a different pass structure than `run --window`.

- [ ] **Step 5: Update the build handoff**

Prepend an entry to the top of `docs/project/next-steps.md` (after the file header,
before the current top entry), in that file's established dense per-entry voice.
Cover: the 256→208-byte drop and what moved to the UBO, the object-space→world-space
change, the intentional non-uniform-scale fix, the test-churn risk and how it was
mitigated, test counts, and verification results. Follow the structure of the entries
already at the top of that file.

- [ ] **Step 6: Update the deferred-work backlog**

In `docs/project/deferred-work-backlog.md`'s Rendering section, mark the
**Shading-data UBO migration** bullet shipped (`~~...~~` with a **SHIPPED 2026-09-27**
note, mirroring how the shadows v1 and spot-lights entries were marked), and update
the **Specular highlights (Blinn-Phong)** bullet to drop its "blocked on the UBO
migration" clause, since that blocker is now cleared. Leave the other deferred
siblings (full PBR, multi-light, dropping `mvp`) untouched.

- [ ] **Step 7: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "docs: record shading-data UBO migration shipment

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```
