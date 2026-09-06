# Particle Rendering (Instanced Billboards) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make simulated particles visible as camera-facing, alpha-blended billboard sprites — the first consumer of `ParticleSystem::particles_for`.

**Architecture:** `RenderSystem::render()` gains a particle pass after the mesh pass: it gathers each emitter's live particles via `particles_for`, builds a flat instance buffer (world position, size, color, derived alpha), and issues one neutral `RenderDevice::draw_particles(...)` call. The Vulkan backend expands each instance into a billboard through a dedicated alpha-blended pipeline. The neutral wall holds: the engine tier gains particle draw *types* but no Vulkan; GPU code stays in `modules/render_vulkan`.

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (FetchContent), Vulkan (`modules/render_vulkan`), GLSL compiled to SPIR-V via `glslc`.

**Spec:** [docs/superpowers/specs/2026-09-06-particle-rendering-design.md](../specs/2026-09-06-particle-rendering-design.md)

## Global Constraints

- **Tier wall:** no `Vk*`/Vulkan types above `modules/render_vulkan`; no pybind11/CPython anywhere here. The neutral `RenderDevice` is the only seam.
- **Determinism:** particle simulation stays bit-identical; rendering reads pools, never mutates them.
- **Header-only engine tier:** `engine/render/*.hpp` and `engine/particles/*.hpp` are header-only; match the style.
- **Math conventions:** column-major storage, column vectors, right-handed, Y-up, radians. `Mat4::m` is `std::array<float,16>` column-major (column `c`, row `r` at `m[c*4+r]`).
- **Build:** configure/build with preset `debug`; run tests with the `debug` test preset (ctest).
- **Test tags:** particle-tier tests use `[particles]`; render-tier tests use `[render]`.
- **Catch2 test names:** no leading dashes, no unbalanced `[` brackets (breaks CTest discovery).
- **Sandbox:** the GUI window cannot be screenshotted; verify `mosaic run --window` functionally (exit 0 + zero Vulkan validation errors in stderr).

---

### Task 1: ParticleEmitter appearance fields (`color`, `size`)

Add reflected, serialized appearance to the emitter so each emitter's particles carry a color and size. Alpha fade is derived at render time, so `Particle` is untouched.

**Files:**
- Modify: `engine/particles/particle_emitter.hpp`
- Test: `tests/unit/particles/particle_test.cpp` (extend the existing round-trip case)

**Interfaces:**
- Consumes: nothing new.
- Produces: `ParticleEmitter::color` (`mosaic::foundation::math::Vec3`, default `{1,1,1}`) and `ParticleEmitter::size` (`float`, default `0.1f`), both reflected via `reflect()`.

- [ ] **Step 1: Extend the failing test** — in `tests/unit/particles/particle_test.cpp`, inside `TEST_CASE("ParticleEmitter round-trips through reflection", "[particles]")`, set the new fields before `to_value` and assert after `from_value`:

```cpp
    e.color            = Vec3{0.2f, 0.4f, 0.6f};
    e.size             = 0.35f;
```

and, after the existing assertions:

```cpp
    REQUIRE(back.color.x == Approx(0.2f));
    REQUIRE(back.color.y == Approx(0.4f));
    REQUIRE(back.color.z == Approx(0.6f));
    REQUIRE(back.size == Approx(0.35f));
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug 2>&1 | tail -20`
Expected: compile error — `ParticleEmitter` has no member `color` / `size`.

- [ ] **Step 3: Add the fields + reflection** — in `engine/particles/particle_emitter.hpp`, add to the `ParticleEmitter` struct (after `max_particles`):

```cpp
    Vec3          color            = Vec3{1.0f, 1.0f, 1.0f};     // base particle color
    float         size             = 0.1f;                       // world-space billboard edge length
```

and add to `reflect()` (after the `max_particles` line):

```cpp
    visitor.member("color", e.color);
    visitor.member("size", e.size);
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `ctest --preset debug -R "ParticleEmitter round-trips" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/particles/particle_emitter.hpp tests/unit/particles/particle_test.cpp
git commit -m "feat(particles): ParticleEmitter color + size (reflected)"
```

---

### Task 2: Neutral `draw_particles` API + RenderSystem particle pass

Add the neutral particle draw types + a default-no-op device method, then the RenderSystem particle pass that gathers instances and issues the call. Tested headlessly against a fake device that records `draw_particles`.

**Files:**
- Modify: `engine/render/render_device.hpp`
- Modify: `engine/render/render_system.hpp`
- Test: `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `ParticleEmitter::color`/`size` (Task 1); `ParticleSystem::particles_for(EntityID) -> const std::vector<Particle>*`; `World::get_system<T>() -> T*`; `World::for_each<T>`.
- Produces:
  - `struct ParticleInstance { Vec3 world_position; float size; Vec3 color; float alpha; };`
  - `struct ParticleDrawParams { Mat4 view_proj; Vec3 camera_right; Vec3 camera_up; };`
  - `virtual void RenderDevice::draw_particles(const std::vector<ParticleInstance>&, const ParticleDrawParams&) {}`
  - `RenderSystem::render` now emits one `draw_particles` call per frame when any live particles exist.

- [ ] **Step 1: Add the neutral types + device method** — in `engine/render/render_device.hpp`, after the `DrawParams` struct (before `class RenderDevice`):

```cpp
// One billboarded particle. world_position/size are world-space; alpha is the derived
// fade (1 at spawn -> 0 at end of life), computed by the RenderSystem, not stored.
struct ParticleInstance
{
    mosaic::foundation::math::Vec3 world_position;
    float                          size = 0.0f;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// Camera data for billboarding. view_proj = projection * view (no model matrix; each
// instance is positioned in the vertex shader). camera_right/up are the world-space
// camera basis vectors the billboard quad is spanned along.
struct ParticleDrawParams
{
    mosaic::foundation::math::Mat4 view_proj;
    mosaic::foundation::math::Vec3 camera_right;
    mosaic::foundation::math::Vec3 camera_up;
};
```

and add to `class RenderDevice` (after the `draw(...)` pure virtual, near the other optional hooks):

```cpp
    // Draw camera-facing, alpha-blended billboards, one per instance. Default no-op so
    // headless devices and test fakes need not implement it (like the UI/viewport hooks).
    // Must be called after mesh draws in the same pass (see RenderSystem ordering contract).
    virtual void draw_particles(const std::vector<ParticleInstance>& /*instances*/,
                                const ParticleDrawParams& /*params*/) {}
```

Add `#include <vector>` if not already present (it is).

- [ ] **Step 2: Extend `FakeRenderDevice` + write the first failing test** — in `tests/unit/render/render_system_test.cpp`:

Add includes near the top:

```cpp
#include "engine/particles/particle_emitter.hpp"
#include "engine/particles/particle_system.hpp"
```

Add using-declarations:

```cpp
using mosaic::engine::render::ParticleInstance;
using mosaic::engine::render::ParticleDrawParams;
using mosaic::engine::particles::ParticleEmitter;
using mosaic::engine::particles::ParticleSystem;
```

Extend `FakeRenderDevice` (inside the class, after `draw`):

```cpp
    struct ParticleDrawCall { std::vector<ParticleInstance> instances; ParticleDrawParams params; };
    std::vector<ParticleDrawCall> particle_draws;
    void draw_particles(const std::vector<ParticleInstance>& instances,
                        const ParticleDrawParams& params) override
    {
        particle_draws.push_back({instances, params});
    }
```

Add the first test case at the end of the file:

```cpp
TEST_CASE("RenderSystem emits one batched draw_particles with per-particle instances", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    world.add_system<ParticleSystem>();
    auto* psys = world.get_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{Vec3{1, 2, 3}, {}, Vec3{1, 1, 1}, {}});
    ParticleEmitter em;
    em.emitting = true;
    em.rate = 100.0f;
    em.lifetime = 10.0f;
    em.initial_velocity = Vec3{0, 0, 0};
    em.acceleration = Vec3{0, 0, 0};
    em.color = Vec3{0.9f, 0.5f, 0.1f};
    em.size = 0.25f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // spawns ~10 particles at (1,2,3)
    const auto* pool = psys->particles_for(e);
    REQUIRE(pool != nullptr);
    REQUIRE(!pool->empty());

    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    const auto& call = device.particle_draws[0];
    REQUIRE(call.instances.size() == pool->size());
    REQUIRE(call.instances[0].color.x == Approx(0.9f));
    REQUIRE(call.instances[0].size == Approx(0.25f));
    REQUIRE(call.instances[0].world_position.x == Approx(1.0f));
    REQUIRE(call.instances[0].world_position.y == Approx(2.0f));
}
```

Add the include for the world/particle system tick path if needed: `#include "engine/particles/particle_system.hpp"` (added above). `world.tick` and `add_system` come from `engine/world/world.hpp` (already included).

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build --preset debug 2>&1 | tail -20 && ctest --preset debug -R "batched draw_particles" --output-on-failure`
Expected: FAIL — `device.particle_draws` is empty (RenderSystem has no particle pass yet).

- [ ] **Step 4: Implement the particle pass** — in `engine/render/render_system.hpp`:

Add include (with the other engine includes):

```cpp
#include "engine/particles/particle_system.hpp"
```

At the end of `render(...)`, after the `world.for_each<MeshRenderer>(...)` block closes (after its `);`), add:

```cpp
        // Particle pass: gather every emitter's live particles into one instanced,
        // camera-facing billboard draw. Runs last in the pass (the device binds the
        // particle pipeline; no mesh draw may follow in the same pass).
        auto* particles = world.get_system<mosaic::engine::particles::ParticleSystem>();
        if (particles != nullptr)
        {
            const Mat4 cam_world = Mat4::inverse_affine(view);
            const Vec3 cam_right = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[0], cam_world.m[1], cam_world.m[2]});
            const Vec3 cam_up = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[4], cam_world.m[5], cam_world.m[6]});

            std::vector<ParticleInstance> instances;
            world.for_each<mosaic::engine::particles::ParticleEmitter>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::particles::ParticleEmitter& em)
                {
                    const auto* pool = particles->particles_for(e);
                    if (pool == nullptr)
                    {
                        return;
                    }
                    for (const auto& p : *pool)
                    {
                        const float alpha = (p.lifetime > 0.0f)
                            ? std::clamp(1.0f - p.age / p.lifetime, 0.0f, 1.0f)
                            : 1.0f;
                        instances.push_back(ParticleInstance{p.position, em.size, em.color, alpha});
                    }
                });

            if (!instances.empty())
            {
                device.draw_particles(instances, ParticleDrawParams{projection * view, cam_right, cam_up});
            }
        }
```

Add includes at the top if missing: `#include <algorithm>` (for `std::clamp`) and `#include <vector>`.

- [ ] **Step 5: Run to verify it passes**

Run: `ctest --preset debug -R "batched draw_particles" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Add the remaining headless cases** — append to `tests/unit/render/render_system_test.cpp`:

```cpp
TEST_CASE("RenderSystem particle alpha fades with age", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = true;
    em.rate = 1000.0f;   // spawn many so ages differ across the pool
    em.lifetime = 1.0f;
    em.acceleration = Vec3{0, 0, 0};
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.5f); // pool now holds a spread of ages, all < lifetime
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    for (const auto& inst : device.particle_draws[0].instances)
    {
        REQUIRE(inst.alpha >= 0.0f);
        REQUIRE(inst.alpha <= 1.0f);
    }
}

TEST_CASE("RenderSystem batches two emitters with distinct colors", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto make = [&](Vec3 color)
    {
        auto e = world.create_entity();
        world.add_component<mosaic::engine::transform::Transform>(
            e, mosaic::engine::transform::Transform{});
        ParticleEmitter em;
        em.emitting = true; em.rate = 50.0f; em.lifetime = 10.0f;
        em.acceleration = Vec3{0, 0, 0}; em.color = color;
        world.add_component<ParticleEmitter>(e, em);
    };
    make(Vec3{1, 0, 0});
    make(Vec3{0, 0, 1});

    world.tick(0.1f);
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    bool saw_red = false, saw_blue = false;
    for (const auto& inst : device.particle_draws[0].instances)
    {
        if (inst.color.x > 0.5f) saw_red = true;
        if (inst.color.z > 0.5f) saw_blue = true;
    }
    REQUIRE(saw_red);
    REQUIRE(saw_blue);
}

TEST_CASE("RenderSystem skips particle pass when no ParticleSystem present", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<ParticleEmitter>(e, ParticleEmitter{}); // emitter but no system

    rs.render(world, device, registry); // must not crash
    REQUIRE(device.particle_draws.empty());
}

TEST_CASE("RenderSystem emits no particle draw when there are no live particles", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = false; em.burst = 0; // never spawns
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    rs.render(world, device, registry);
    REQUIRE(device.particle_draws.empty());
}

TEST_CASE("RenderSystem derives an orthonormal camera basis for billboards", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4}); // identity-rotation camera looking down -Z
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = true; em.rate = 50.0f; em.lifetime = 10.0f; em.acceleration = Vec3{0, 0, 0};
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    const auto& params = device.particle_draws[0].params;
    // A camera at +Z looking at the origin (no roll) has world right = +X, up = +Y.
    REQUIRE(params.camera_right.x == Approx(1.0f).margin(1e-4));
    REQUIRE(params.camera_right.y == Approx(0.0f).margin(1e-4));
    REQUIRE(params.camera_up.y == Approx(1.0f).margin(1e-4));
    // Right and up are perpendicular.
    const float dot = params.camera_right.x * params.camera_up.x +
                      params.camera_right.y * params.camera_up.y +
                      params.camera_right.z * params.camera_up.z;
    REQUIRE(dot == Approx(0.0f).margin(1e-4));
}
```

- [ ] **Step 7: Run the full render + particle suites**

Run: `ctest --preset debug -R "\[render\]|\[particles\]" --output-on-failure`
Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(render): RenderSystem particle pass + neutral draw_particles API"
```

---

### Task 3: Vulkan billboard pipeline + `draw_particles` implementation

Implement the GPU side: two shaders, two pipelines (swapchain + offscreen viewport), per-frame instance buffers, and the `draw_particles` override. No unit test (GPU path); the deliverable is a validation-clean build that compiles the shaders and records the draw. Functional acceptance happens in Task 4.

**Files:**
- Create: `modules/render_vulkan/shaders/particle.vert`
- Create: `modules/render_vulkan/shaders/particle.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`

**Interfaces:**
- Consumes: `ParticleInstance`, `ParticleDrawParams`, `RenderDevice::draw_particles` (Task 2).
- Produces: a working `draw_particles` override on the Vulkan device.

- [ ] **Step 1: Write `particle.vert`** — create `modules/render_vulkan/shaders/particle.vert`:

```glsl
#version 450

// Per-instance attributes (instance-rate vertex binding 0).
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in float inSize;
layout(location = 2) in vec3 inColor;
layout(location = 3) in float inAlpha;

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_right; // world-space, .xyz
    vec4 camera_up;    // world-space, .xyz
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;
layout(location = 2) out vec2 fragUV;

// Two triangles forming a quad centered on the particle, in [-0.5, 0.5].
const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

void main()
{
    vec2 corner = kCorners[gl_VertexIndex];
    vec3 worldPos = inWorldPos
        + corner.x * inSize * pc.camera_right.xyz
        + corner.y * inSize * pc.camera_up.xyz;
    gl_Position = pc.view_proj * vec4(worldPos, 1.0);
    fragColor = inColor;
    fragAlpha = inAlpha;
    fragUV = corner + vec2(0.5); // [0,1] across the quad
}
```

- [ ] **Step 2: Write `particle.frag`** — create `modules/render_vulkan/shaders/particle.frag`:

```glsl
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragAlpha;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main()
{
    // Soft round sprite: fade to 0 at the quad's inscribed circle edge.
    float d = length(fragUV - vec2(0.5)) * 2.0;
    float falloff = smoothstep(1.0, 0.0, d);
    outColor = vec4(fragColor, fragAlpha * falloff);
}
```

- [ ] **Step 3: Compile the shaders via CMake** — in `modules/render_vulkan/CMakeLists.txt`, change the shader `foreach` line to include the new stages:

```cmake
foreach(_stage cube.vert cube.frag particle.vert particle.frag)
```

- [ ] **Step 4: Verify the shaders compile**

Run: `cmake --build --preset debug --target mosaic_render_vulkan_shaders 2>&1 | tail -20`
Expected: builds `particle.vert.spv` and `particle.frag.spv` with no glslc errors.

- [ ] **Step 5: Add the particle push-constant struct + members** — in `modules/render_vulkan/vulkan_render_device.cpp`, after the `PushConstants` struct (near line 50), add:

```cpp
struct ParticlePush
{
    float view_proj[16];
    float camera_right[4]; // vec3 + pad
    float camera_up[4];    // vec3 + pad
};
static_assert(sizeof(ParticlePush) == 96, "particle push constant layout must match the shader");
```

In the device class's private members (alongside `pipeline_` / `vp_pipeline_`), add:

```cpp
    VkPipeline       particle_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_particle_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout particle_pipeline_layout_ = VK_NULL_HANDLE;

    struct InstanceBuffer { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize capacity = 0; };
    std::array<InstanceBuffer, kFramesInFlight> particle_instances_{};
```

Add `#include <array>` if not already present (it is — `PushConstants`/clears use it).

- [ ] **Step 6: Create the particle pipelines** — add a `create_particle_pipeline()` method (model it on `create_pipeline()`), and call it from the constructor right after `create_pipeline();` (near line 160):

```cpp
    void create_particle_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/particle.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/particle.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        // Instance-rate binding: one ParticleInstance per billboard.
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(mosaic::engine::render::ParticleInstance);
        binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(mosaic::engine::render::ParticleInstance, world_position);
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32_SFLOAT;
        attrs[1].offset = offsetof(mosaic::engine::render::ParticleInstance, size);
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[2].offset = offsetof(mosaic::engine::render::ParticleInstance, color);
        attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT;
        attrs[3].offset = offsetof(mosaic::engine::render::ParticleInstance, alpha);

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE; // billboards face the camera; winding irrelevant

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE; // transparent: respect scene depth, don't occlude peers
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push.offset = 0; push.size = sizeof(ParticlePush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0; // particles are untextured; no descriptor sets
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &particle_pipeline_layout_),
                 "vkCreatePipelineLayout(particle)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = particle_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &particle_pipeline_),
                 "vkCreateGraphicsPipelines(particle)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_particle_pipeline_),
                 "vkCreateGraphicsPipelines(particle offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }
```

Call it in the constructor immediately after the existing `create_pipeline();`.

- [ ] **Step 7: Implement `draw_particles`** — add the override after `draw(...)` (near line 546):

```cpp
    void draw_particles(const std::vector<mosaic::engine::render::ParticleInstance>& instances,
                        const mosaic::engine::render::ParticleDrawParams& params) override
    {
        if (!viewport_pass_open_)
        {
            ensure_swapchain_pass();
        }
        if (frame_skipped_ || instances.empty())
        {
            return;
        }

        // Grow this frame's instance buffer if needed, then upload.
        const VkDeviceSize bytes = instances.size() * sizeof(instances[0]);
        InstanceBuffer& ib = particle_instances_[frame_];
        if (ib.capacity < bytes)
        {
            if (ib.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, ib.buf, nullptr); }
            if (ib.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, ib.mem, nullptr); }
            create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          ib.buf, ib.mem);
            ib.capacity = bytes;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, ib.mem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, instances.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device_, ib.mem);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_particle_pipeline_ : particle_pipeline_);

        ParticlePush pc{};
        std::memcpy(pc.view_proj, params.view_proj.m.data(), sizeof(pc.view_proj));
        pc.camera_right[0] = params.camera_right.x;
        pc.camera_right[1] = params.camera_right.y;
        pc.camera_right[2] = params.camera_right.z;
        pc.camera_up[0] = params.camera_up.x;
        pc.camera_up[1] = params.camera_up.y;
        pc.camera_up[2] = params.camera_up.z;
        vkCmdPushConstants(cmd_[frame_], particle_pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &ib.buf, &offset);
        vkCmdDraw(cmd_[frame_], 6, static_cast<uint32_t>(instances.size()), 0, 0);
    }
```

Add `#include <cstring>` if not present (it is — `draw` uses `std::memcpy`).

- [ ] **Step 8: Destroy the new objects in teardown** — in the destructor / cleanup path (near where `vp_pipeline_`/`pipeline_` are destroyed, ~lines 180–236), add:

```cpp
            if (vp_particle_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_particle_pipeline_, nullptr); }
            if (particle_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, particle_pipeline_, nullptr); }
            if (particle_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, particle_pipeline_layout_, nullptr); }
            for (auto& ib : particle_instances_)
            {
                if (ib.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, ib.buf, nullptr); }
                if (ib.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, ib.mem, nullptr); }
            }
```

(Match the existing guard style/order; place these near the other pipeline destroys, ensuring they run before `vkDestroyDevice`.)

- [ ] **Step 9: Build the whole project**

Run: `cmake --build --preset debug 2>&1 | tail -25`
Expected: builds clean (no compile/link errors, no `static_assert` failure).

- [ ] **Step 10: Run the full test suite (nothing regressed)**

Run: `ctest --preset debug --output-on-failure 2>&1 | tail -15`
Expected: all tests pass (Task 3 adds no unit tests; this guards against breakage).

- [ ] **Step 11: Commit**

```bash
git add modules/render_vulkan/shaders/particle.vert modules/render_vulkan/shaders/particle.frag modules/render_vulkan/CMakeLists.txt modules/render_vulkan/vulkan_render_device.cpp
git commit -m "feat(render-vulkan): instanced billboard particle pipeline + draw_particles"
```

---

### Task 4: Demo emitter + functional acceptance + next-steps update

Wire a visible fountain emitter into the default scene, fix the affected demo-scene test count, verify the window path runs validation-clean, and update the handoff doc (rides in this feature branch, not its own PR).

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Modify: `tests/unit/app/demo_scene_test.cpp:30`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `ParticleEmitter` (Task 1); the particle pass (Tasks 2–3).
- Produces: a `ParticleEmitter` entity in `build_default_scene`.

- [ ] **Step 1: Update the demo-scene transform count test first** — in `tests/unit/app/demo_scene_test.cpp`, change line 30 from `REQUIRE(transforms == 8);` to:

```cpp
    REQUIRE(transforms == 9); // + the fountain particle emitter
```

- [ ] **Step 2: Run it to verify it fails**

Run: `ctest --preset debug -R "demo" --output-on-failure`
Expected: FAIL — currently 8 transforms (emitter not added yet).

- [ ] **Step 3: Add the demo emitter** — in `app/run/demo_scene.hpp`, add the include at the top with the other engine includes:

```cpp
#include "engine/particles/particle_emitter.hpp"
```

and at the end of `build_default_scene(...)` (after the `orbiter` block), add:

```cpp
    // A fountain of orange particles rising and falling under gravity. No MeshRenderer:
    // the emitter itself is invisible; only its particles draw (RenderSystem particle pass).
    const auto fountain = world.create_entity();
    world.add_component<transform::Transform>(fountain,
        transform::Transform{Vec3{-3, 0.5f, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    mosaic::engine::particles::ParticleEmitter emitter;
    emitter.emitting         = true;
    emitter.rate             = 60.0f;
    emitter.lifetime         = 2.0f;
    emitter.initial_velocity = Vec3{0.0f, 4.0f, 0.0f};
    emitter.spread           = 1.0f;
    emitter.acceleration     = Vec3{0.0f, -9.81f, 0.0f};
    emitter.color            = Vec3{1.0f, 0.5f, 0.1f};
    emitter.size             = 0.15f;
    world.add_component<mosaic::engine::particles::ParticleEmitter>(fountain, emitter);
```

- [ ] **Step 4: Run the demo-scene test to verify it passes**

Run: `ctest --preset debug -R "demo" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Run the full suite**

Run: `ctest --preset debug --output-on-failure 2>&1 | tail -15`
Expected: all PASS.

- [ ] **Step 6: Functional acceptance — window runs validation-clean**

Run: `./build/debug/app/mosaic run --window --ticks 60 2>&1 | tail -30`
(Adjust the binary path if the build tree differs — locate it with `find build -name mosaic -type f`.)
Expected: process exits 0; stderr shows **no** Vulkan validation errors/warnings (the debug messenger routes them through the log). Confirm no `vulkan` error/warn lines about the particle pipeline, push constants, or vertex input.

- [ ] **Step 7: Update the handoff doc** — in `docs/project/next-steps.md`, update the particles follow-up line (in the Phase-4 particles bullet, which currently lists "rendering (instanced billboards/points — the first `particles_for` consumer)" as a follow-up) to mark rendering **done**, noting: instanced camera-facing billboards via a neutral `draw_particles` seam, alpha-fade over life, per-emitter `color`/`size`, a Vulkan alpha-blended pipeline (swapchain + offscreen), and a demo fountain in the default scene. Update the "Last updated" line and test count. Reference this plan and [the spec](../superpowers/specs/2026-09-06-particle-rendering-design.md).

- [ ] **Step 8: Commit**

```bash
git add app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp docs/project/next-steps.md
git commit -m "feat(particles): demo fountain emitter + next-steps (particle rendering done)"
```

---

## Notes for the executor

- **Build/test locations:** binaries under `build/debug/`; `ctest --preset debug` runs from the repo root. If the preset build dir differs, `cmake --preset debug` first.
- **`glslc` requirement:** Task 3 needs the Vulkan SDK's `glslc` on PATH (already required by the existing shader step). If it's missing, the whole `mosaic_render_vulkan` target fails — not particle-specific.
- **Ordering contract:** the RenderSystem always draws meshes before particles; the Vulkan `draw_particles` binds the particle pipeline and must not be followed by a mesh `draw()` in the same pass. Don't reorder the passes.
- **If a subagent cannot run the GUI (headless CI):** Task 4 Step 6 is a manual/acceptance check; record that it was deferred rather than marking it done falsely.
