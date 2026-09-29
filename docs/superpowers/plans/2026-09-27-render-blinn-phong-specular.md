# Rendering — Blinn-Phong Specular Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a view-dependent Blinn-Phong specular highlight to all three light types, with authorable per-`Material` `shininess` and `specular_strength`.

**Architecture:** Two scalar material parameters ride in the existing push-constant block (208 → 216 bytes); the world-space camera position rides in the existing per-frame lighting UBO (176 → 192 bytes). The fragment shader computes one view vector and adds a Blinn-Phong term per light, reusing each light's already-computed attenuation, cone, and shadow factors. No new descriptor set, buffer, binding, or render pass.

**Tech Stack:** C++20, Vulkan, GLSL 450, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-09-27-render-blinn-phong-specular-design.md`

## Global Constraints

- Column-major `Mat4`: `element(row, col) == m[col * 4 + row]`. Translation lives at `m[12] m[13] m[14]`.
- World-space shading throughout. Nothing in this slice reintroduces object-space lighting.
- `PushConstants` must end at **216 bytes**, `FrameLightingUbo` at **192 bytes**, each pinned by a `static_assert`.
- The two GLSL uniform blocks (`cube.vert` and `cube.frag`) must remain **byte-identical to each other** and to the spec's offset tables.
- Specular is multiplied by **light color, never by `albedo`**.
- Ambient stays `0.1 * albedo`. Do not add an ambient specular term.
- Defaults, used identically by `Material` and by the no-`Material` draw path: `shininess = 32.0f`, `specular_strength = 0.5f`.
- The engine does **not** clamp `shininess`; the shader does. Do not add an engine-side clamp (see Review Focus 4).
- Build: `cmake --build --preset debug`. Test: `ctest --preset debug --output-on-failure`.
- **A binary existing on disk is not proof a build finished.** Wait for the build command to actually exit before running tests. This has bitten this project twice.

## Review Focus

Input classes the spec implies but whose tests no task would otherwise own. Each has a test assigned to the task that owns the code.

1. **A frame with no meshes must still send `camera_position`** — otherwise the backend's UBO keeps a stale camera and highlights freeze in place while the camera moves. This is the exact hazard the UBO migration introduced; specular is the first feature that makes it *visible*. → Task 2.
2. **Camera movement between frames must change `camera_position`** — the core promise of the feature is that highlights track the camera. A one-shot test cannot catch a value that is computed once and cached. → Task 2.
3. **`specular_strength = 0` must reach the draw call as exactly 0** — the "matte is unchanged" guarantee. A falsy-zero bug that substitutes the 0.5 default would make every material glossy with no way to opt out. → Task 2.
4. **A zero or negative `shininess` from a scene file must pass through the engine unmodified** — the shader clamps to `>= 1`; the engine must not. Pinning this stops someone from later adding a well-meaning engine-side clamp that silently diverges from the shader and makes the two disagree. → Task 1 (serialization) and Task 2 (pass-through).
5. **A `Material` that exists but leaves the specular fields at their defaults** — distinct from the no-`Material` path, and the common real case. Both must produce 32.0 / 0.5. → Task 2.

---

### Task 1: Authorable material parameters

Adds the two fields and makes them persist. Compiles and tests standalone: this is a pure addition, and no existing code reads the new fields yet.

**Files:**
- Modify: `engine/render/material.hpp`
- Test: `tests/unit/reflection/lighting_reflection_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `Material::shininess` (`float`, default `32.0f`) and `Material::specular_strength` (`float`, default `0.5f`), both reflected under those exact names.

- [ ] **Step 1: Write the failing test**

Extend the existing `"Material and DirectionalLight round-trip through serialization"` case in `tests/unit/reflection/lighting_reflection_test.cpp`. Do **not** add a parallel test case.

After the line `mat.texture = tex_id;`, add:

```cpp
    mat.shininess = 64.0f;
    mat.specular_strength = 0.25f;
```

Inside the existing `dst.for_each<Material>(...)` lambda, add:

```cpp
        REQUIRE(m.shininess == Approx(64.0f));
        REQUIRE(m.specular_strength == Approx(0.25f));
```

Then add this new case immediately after that test case:

```cpp
TEST_CASE("Material specular defaults and out-of-range values survive a round-trip", "[reflection]")
{
    // Review Focus 4: shininess is authorable by hand in a scene file. A zero or
    // negative exponent must round-trip UNCHANGED -- the shader clamps to >= 1, the
    // engine deliberately does not, and an engine-side clamp added later would make
    // the two silently disagree about what a scene file means.
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    auto defaults = src.create_entity();
    src.add_component<Material>(defaults, Material{});
    auto weird = src.create_entity();
    Material odd{};
    odd.shininess = -4.0f;
    odd.specular_strength = 2.5f;
    src.add_component<Material>(weird, odd);

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    std::vector<float> shininess;
    std::vector<float> strength;
    dst.for_each<Material>([&](World::EntityID, Material& m) {
        shininess.push_back(m.shininess);
        strength.push_back(m.specular_strength);
    });
    REQUIRE(shininess.size() == 2);
    // Order follows entity creation order.
    REQUIRE(shininess[0] == Approx(32.0f));   // documented default
    REQUIRE(strength[0] == Approx(0.5f));     // documented default
    REQUIRE(shininess[1] == Approx(-4.0f));   // NOT clamped by the engine
    REQUIRE(strength[1] == Approx(2.5f));     // NOT clamped by the engine
}
```

Add `#include <vector>` to the file's includes if it is not already present.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests
./build/debug/tests/unit/mosaic_unit_tests "[reflection]"
```

Expected: a **compile** failure — `'struct Material' has no member named 'shininess'`. That is the correct failure for this step.

- [ ] **Step 3: Write the implementation**

In `engine/render/material.hpp`, replace the struct and `reflect` with:

```cpp
struct Material
{
    mosaic::foundation::math::Vec3          base_color{0.8f, 0.8f, 0.8f};
    mosaic::foundation::serialization::AssetID texture{}; // nil = untextured
    // Blinn-Phong specular. shininess is a raw Phong exponent (perceptually
    // non-linear; a 0..1 perceptual remap is deferred to the PBR work). The shader
    // clamps it to >= 1 at shading time -- deliberately NOT clamped here, so a
    // scene file round-trips byte-for-byte.
    float shininess = 32.0f;
    float specular_strength = 0.5f; // 0 = matte
};
template <class V> void reflect(V& v, Material& m)
{
    v.member("base_color", m.base_color);
    v.member("texture", m.texture);
    v.member("shininess", m.shininess);
    v.member("specular_strength", m.specular_strength);
}
```

The new fields go **after** `texture` so that existing aggregate initializations such as `Material{Vec3{0.1f, 0.2f, 0.3f}}` keep compiling.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests
./build/debug/tests/unit/mosaic_unit_tests "[reflection]"
```

Expected: PASS, all reflection cases.

- [ ] **Step 5: Commit**

The pre-commit hook regenerates `docs/reference/components.md` (the new fields appear in the component reference automatically). If the hook reports stale generated docs, `git add` the regenerated files and commit again.

**Do not use `git add -A`** — the working tree has an unrelated modified file, `docs/project/advancement-plans-and-evaluation.md`, that must never be staged. Stage explicit paths only.

```bash
git add engine/render/material.hpp tests/unit/reflection/lighting_reflection_test.cpp
git commit -m "feat(render): authorable shininess and specular_strength on Material"
```

---

### Task 2: Camera position and per-draw material parameters

Wires the new data from the world to the `RenderDevice` interface. Compiles standalone: every change is an addition of a defaulted member or a new assignment, so the Vulkan backend keeps building untouched (it simply ignores the new fields until Task 3).

**Files:**
- Modify: `engine/render/render_device.hpp`
- Modify: `engine/render/render_system.hpp`
- Test: `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `Material::shininess`, `Material::specular_strength` from Task 1.
- Produces: `FrameLighting::camera_position` (`Vec3`, world space, declared immediately before `light_view_proj`); `DrawParams::shininess` and `DrawParams::specular_strength` (`float`, declared after `base_color`, defaulted to `32.0f` / `0.5f`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/render/render_system_test.cpp`:

```cpp
TEST_CASE("RenderSystem reports the camera's world position in FrameLighting", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{2.0f, 3.0f, 4.0f});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(fl.camera_position.x == Approx(2.0f).margin(1e-5));
    REQUIRE(fl.camera_position.y == Approx(3.0f).margin(1e-5));
    REQUIRE(fl.camera_position.z == Approx(4.0f).margin(1e-5));
}

TEST_CASE("RenderSystem recovers the camera position from the fallback view", "[render]")
{
    // No Camera component at all: the position has to be recovered from the fallback
    // view matrix rather than read off a WorldTransform. fallback_view() looks from
    // {0, 1.5, 4} at the origin, so that is the expected eye position.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(fl.camera_position.x == Approx(0.0f).margin(1e-4));
    REQUIRE(fl.camera_position.y == Approx(1.5f).margin(1e-4));
    REQUIRE(fl.camera_position.z == Approx(4.0f).margin(1e-4));
}

TEST_CASE("RenderSystem sends the camera position even with no meshes", "[render]")
{
    // Review Focus 1: the backend's UBO is only refreshed by set_frame_lighting. If a
    // mesh-less frame skipped it, the camera position would go stale and highlights
    // would freeze in place while the camera moved. Specular is the first feature
    // that makes this hazard visible.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{5.0f, 0.0f, 0.0f});

    rs.render(world, device, registry);

    REQUIRE(device.draws.empty());
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].camera_position.x == Approx(5.0f).margin(1e-5));
}

TEST_CASE("RenderSystem's camera position follows a moving camera", "[render]")
{
    // Review Focus 2: the whole point of specular is that it tracks the camera. A
    // value computed once and cached would pass every single-frame test above.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    auto cam = add_camera(world, Vec3{1.0f, 0.0f, 0.0f});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    auto* cam_wt = world.get_component<WorldTransform>(cam);
    REQUIRE(cam_wt != nullptr);
    cam_wt->matrix = Mat4::translation(Vec3{-7.0f, 0.0f, 0.0f});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 2);
    REQUIRE(device.frame_lighting_calls[0].camera_position.x == Approx(1.0f).margin(1e-5));
    REQUIRE(device.frame_lighting_calls[1].camera_position.x == Approx(-7.0f).margin(1e-5));
}

TEST_CASE("RenderSystem forwards a Material's specular parameters to the draw call", "[render]")
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
    Material mat{Vec3{0.2f, 0.4f, 0.6f}};
    mat.shininess = 128.0f;
    mat.specular_strength = 0.75f;
    world.add_component<Material>(mesh, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.shininess == Approx(128.0f));
    REQUIRE(p.specular_strength == Approx(0.75f));
}

TEST_CASE("RenderSystem uses the documented specular defaults with and without a Material", "[render]")
{
    // Review Focus 5: a Material that exists but leaves the specular fields alone is
    // the common real case, and it is a DIFFERENT code path from having no Material
    // at all. Both must yield 32.0 / 0.5.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto bare = world.create_entity();
    world.add_component<WorldTransform>(bare, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(bare, MeshRenderer{mesh_id});

    auto with_material = world.create_entity();
    world.add_component<WorldTransform>(with_material, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(with_material, MeshRenderer{mesh_id});
    world.add_component<Material>(with_material, Material{Vec3{1.0f, 0.0f, 0.0f}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 2);
    for (const auto& d : device.draws)
    {
        REQUIRE(d.params.shininess == Approx(32.0f));
        REQUIRE(d.params.specular_strength == Approx(0.5f));
    }
}

TEST_CASE("RenderSystem forwards a zero specular strength and a negative shininess unchanged", "[render]")
{
    // Review Focus 3: strength 0 is how a material opts OUT of specular. If a
    // falsy-zero bug substituted the 0.5 default, every material would be glossy with
    // no way to turn it off.
    // Review Focus 4: the engine must not clamp shininess -- the shader does. An
    // engine-side clamp added later would silently diverge from the shader.
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
    Material mat{};
    mat.shininess = -4.0f;
    mat.specular_strength = 0.0f;
    world.add_component<Material>(mesh, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.draws[0].params.specular_strength == Approx(0.0f));
    REQUIRE(device.draws[0].params.shininess == Approx(-4.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests
./build/debug/tests/unit/mosaic_unit_tests "[render]"
```

Expected: a **compile** failure — `'struct ...FrameLighting' has no member named 'camera_position'`.

- [ ] **Step 3: Extend the RenderDevice interface**

In `engine/render/render_device.hpp`, inside `struct FrameLighting`, insert immediately **before** the `light_view_proj` member:

```cpp
    mosaic::foundation::math::Vec3 camera_position; // world space, for specular
```

In `struct DrawParams`, after the `base_color` member, add:

```cpp
    // Blinn-Phong specular. Defaults match Material's, so the no-Material draw path
    // and a default-constructed DrawParams agree. shininess is NOT clamped here; the
    // shader clamps it to >= 1.
    float shininess = 32.0f;
    float specular_strength = 0.5f;
```

Also update the `DrawParams` doc comment's first line to mention that the per-draw payload now carries material shading parameters as well as transforms.

- [ ] **Step 4: Wire it up in the RenderSystem**

In `engine/render/render_system.hpp`, in `render()`:

First, hoist the camera world matrix. Immediately after the `const Mat4 light_view_proj = directional_light_view_projection(light);` line, add:

```cpp
        // The camera's world matrix is the inverse of the view matrix; its
        // translation is the eye position, which world-space specular needs. Computed
        // once here and reused by the billboard pass below, which previously computed
        // it for itself.
        const Mat4 cam_world = Mat4::inverse_affine(view);
```

Then, in the `FrameLighting` block, immediately before the `frame_lighting.light_view_proj = light_view_proj;` line, add:

```cpp
        frame_lighting.camera_position = Vec3{cam_world.m[12], cam_world.m[13], cam_world.m[14]};
```

In the billboard pass, **delete** the now-duplicate local:

```cpp
            const Mat4 cam_world = Mat4::inverse_affine(view);
```

Leave the two lines below it (`cam_right`, `cam_up`) exactly as they are — they now read the hoisted `cam_world`.

In the mesh loop, replace the material read and the draw call. The existing block is:

```cpp
                Vec3 base_color{0.8f, 0.8f, 0.8f};
                TextureHandle texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    texture = texture_cache_.get(mat->texture, registry, device);
                }
```

Replace it with:

```cpp
                // Defaults here must match Material's own defaults, so a mesh with no
                // Material shades the same as one with an untouched Material.
                Vec3  base_color{0.8f, 0.8f, 0.8f};
                float shininess = 32.0f;
                float specular_strength = 0.5f;
                TextureHandle texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    shininess = mat->shininess;
                    specular_strength = mat->specular_strength;
                    texture = texture_cache_.get(mat->texture, registry, device);
                }
```

And replace the draw call:

```cpp
                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, wt->matrix,
                                       normal_matrix, base_color, shininess,
                                       specular_strength});
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests
./build/debug/tests/unit/mosaic_unit_tests "[render]"
```

Expected: PASS, including all pre-existing `[render]` cases. If the billboard/particle tests fail, the `cam_world` hoist was done wrong — confirm the local in the billboard pass was deleted and not merely shadowed.

- [ ] **Step 6: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(render): camera position and per-draw specular params"
```

---

### Task 3: Vulkan layout and shader math

The layout change and the two shader blocks land together, because the C++ struct and the GLSL blocks must agree byte-for-byte.

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: `modules/render_vulkan/shaders/cube.vert`
- Modify: `modules/render_vulkan/shaders/cube.frag`

**Interfaces:**
- Consumes: `FrameLighting::camera_position`, `DrawParams::shininess`, `DrawParams::specular_strength` from Task 2.
- Produces: nothing consumed by a later task.

- [ ] **Step 1: Update the C++ layout structs**

In `modules/render_vulkan/vulkan_render_device.cpp`, replace the `PushConstants` struct and its assertion:

```cpp
struct PushConstants
{
    float mvp[16];
    float model[16];         // object -> world
    float normal_matrix[16]; // transpose(inverse(model)); only the upper 3x3 is used
    float base_color[4];     // xyz
    // Push-constant blocks are std430: these two floats pack tight after the vec4.
    float shininess;
    float specular_strength;
};
static_assert(sizeof(PushConstants) == 216, "push constant layout must match the shader");
```

Replace the `FrameLightingUbo` struct and its assertion. Note that `camera_position` goes **before** `light_view_proj`, which moves the matrix from offset 112 to 128:

```cpp
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
    float camera_position[4]; // xyz world space, w unused
    float light_view_proj[16];
};
static_assert(sizeof(FrameLightingUbo) == 192, "UBO layout must match the shader block");
```

- [ ] **Step 2: Populate the new fields**

In `set_frame_lighting`, after the `ubo.spot_color[...]` assignments and before the `light_view_proj` copy, add:

```cpp
        ubo.camera_position[0] = fl.camera_position.x;
        ubo.camera_position[1] = fl.camera_position.y;
        ubo.camera_position[2] = fl.camera_position.z;
```

In `draw()`, after the `pc.base_color[2] = params.base_color.z;` line, add:

```cpp
        pc.shininess = params.shininess;
        pc.specular_strength = params.specular_strength;
```

No other backend change is needed: the descriptor set, buffer allocation, pipeline layout, and `vkCmdPushConstants` call all already size themselves from `sizeof(...)`, and the existing `maxPushConstantsSize` runtime check already compares against `sizeof(PushConstants)`.

- [ ] **Step 3: Update the uniform block in both shaders**

In **both** `modules/render_vulkan/shaders/cube.vert` and `modules/render_vulkan/shaders/cube.frag`, replace the `FrameLighting` block with this exact text (identical in both files):

```glsl
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    vec4 camera_position; // world space .xyz
    mat4 light_view_proj;
} fl;
```

In **both** files, replace the push-constant block with this exact text:

```glsl
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;         // object -> world
    mat4 normal_matrix; // transpose(inverse(model)); upper 3x3 used
    vec4 base_color;    // .xyz
    float shininess;
    float specular_strength;
} pc;
```

`cube.vert` needs no other change.

- [ ] **Step 4: Add the specular math to the fragment shader**

In `modules/render_vulkan/shaders/cube.frag`, add this helper immediately after the `shadow_factor()` function:

```glsl
// Blinn-Phong specular for one light. L points FROM the fragment TOWARD the light
// and is unit length. Returns the specular coefficient; the caller multiplies by
// that light's own attenuation/shadow/cone factor and by the LIGHT's color --
// never by albedo, which would make every surface read as painted metal.
float blinn_spec(vec3 n, vec3 v, vec3 L, float shininess, float strength)
{
    vec3 hv = v + L;
    // v == -L (viewer exactly opposite the light) gives a zero-length half-vector,
    // and normalize() would produce NaN that propagates into the output color as an
    // undiagnosable pixel. Fall back to the normal, where dot(n, h) == 1.
    vec3  h = (dot(hv, hv) > 1e-8) ? normalize(hv) : n;
    // pow(x, 0.0) is 1 everywhere and pow(0, 0) is undefined, so a zero exponent
    // would wash the entire surface. Clamp rather than trusting authored input.
    float e = max(shininess, 1.0);
    // The step() gate kills highlights on geometry facing away from the light, which
    // the half-vector alone would happily produce -- it reads as light leaking
    // through the object.
    return pow(max(dot(n, h), 0.0), e) * strength * step(0.0, dot(n, L));
}
```

In `main()`, replace the `lambert` line so the shadow factor is computed once and shared:

```glsl
    float shadow = shadow_factor();
    float lambert = max(0.0, dot(n, -l)) * shadow;
```

Immediately after the `albedo` line, add:

```glsl
    vec3 v = normalize(fl.camera_position.xyz - fragPos);
```

After the point-light block's `float pl = ...;` line, add:

```glsl
    vec3 pointL = toL / max(d, 1e-4);
```

After the spot-light block's `float spotL = ...;` line, add:

```glsl
    vec3 spotLdir = toSpotL / max(spotD, 1e-4);
```

Finally, replace the color composition with:

```glsl
    // Specular is tinted by LIGHT color, not albedo, and each light's highlight is
    // gated by that same light's own factors -- so shadowed ground gets no sun glint
    // and a spot's highlight respects its cone.
    float spec_dir  = blinn_spec(n, v, -l, pc.shininess, pc.specular_strength) * shadow;
    float spec_pt   = blinn_spec(n, v, pointL, pc.shininess, pc.specular_strength) * atten;
    float spec_spot = blinn_spec(n, v, spotLdir, pc.shininess, pc.specular_strength)
                    * spotAtten * coneAtten;

    vec3 color = albedo * (0.1 + lambert * fl.light_color.xyz)
               + albedo * pl * fl.point_color.xyz
               + albedo * spotL * fl.spot_color.xyz
               + spec_dir * fl.light_color.xyz
               + spec_pt * fl.point_color.xyz
               + spec_spot * fl.spot_color.xyz;
    outColor = vec4(color, 1.0);
```

- [ ] **Step 5: Verify the layouts by hand against the spec**

Before building, open the spec's two offset tables side by side with the structs just written and confirm each field's offset. The `static_assert`s catch C++-side drift but **cannot see the shaders**, so this manual check is the only thing standing between a typo and a subtly wrong image.

Confirm specifically:
- The `FrameLighting` GLSL block is character-identical in `cube.vert` and `cube.frag`.
- `camera_position` precedes `light_view_proj` in all three places (C++ struct, `.vert`, `.frag`).

- [ ] **Step 6: Build and run the full suite**

```bash
cmake --build --preset debug
```

Wait for this command to **exit** before continuing. Then:

```bash
ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"
```

Expected: PASS. Those two exclusions are known pre-existing embedded-Python threading flakes, not regressions from this change.

- [ ] **Step 7: Verify both entry points render clean**

```bash
./build/debug/app/mosaic run --window --duration 3
timeout 5 ./build/debug/app/mosaic editor; echo "exit: $?"
```

Expected for both: exit cleanly (the editor is killed by `timeout`, so exit 124 is the success case) with **zero Vulkan validation errors** on stderr. Validation silence is the signal that matters for a push-constant and UBO layout change.

**Check in this order, because it isolates the failure:**
1. **Shadows first.** `light_view_proj` moved within the UBO; if the offsets are wrong, shadows break visibly and immediately. A shadow regression means a layout bug, not a shading bug — do not go looking at the specular math.
2. **Then a matte material.** Confirm something with `specular_strength = 0` looks exactly as it did before.
3. **Only then the highlights themselves**, and confirm they move when the camera moves.

- [ ] **Step 8: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag
git commit -m "feat(render): Blinn-Phong specular in the Vulkan backend and shaders"
```
