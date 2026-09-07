# Audio A4 — runtime wiring + demo sound — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `AudioSystem` + a real miniaudio backend into the runtime and add an audible demo tone, so `mosaic run --window` plays sound.

**Architecture:** `setup_runtime` gains a defaulted `unique_ptr<AudioBackend>` (null = silent, so tests/editor are unchanged); when non-null it registers a procedural demo tone and adds `AudioSystem`. The demo scene attaches an `AudioListener` (camera) + `AudioSource` (orbiter). A testable `register_audio_clips` helper hands cooked project clips to the backend. `main.cpp` creates the device backend for the windowed run.

**Tech Stack:** C++20, miniaudio (A2), Catch2, CMake. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-07-audio-a4-runtime-wiring-design.md](2026-09-07-audio-a4-runtime-wiring-design.md)

## Global Constraints

- The new `setup_runtime` audio param defaults to `nullptr` — every existing caller (editor, all tests) compiles unchanged and stays silent; no test opens an audio device.
- Demo audio components attach to **existing** entities (camera, orbiter) — default-scene entity/component counts are unchanged.
- Audio verified headlessly on miniaudio's **null device** / null backend; **audible hardware output is the maintainer's check**.
- `AudioSystem` already no-ops with a null backend.
- New/edited tests tagged `[app]`; `mosaic_unit_tests` already links `mosaic_audio_miniaudio`. Build target `mosaic_unit_tests`; preset dir `build/debug`.

---

### Task 1: Runtime wiring, demo tone, and `register_audio_clips`

**Files:**
- Modify: `app/run/demo_scene.hpp` (includes, `kToneClipId`, `make_demo_tone`, `setup_runtime` param + body, `build_default_scene` audio components)
- Modify: `app/asset/asset_io.hpp` (`register_audio_clips` helper)
- Modify: `tests/unit/app/demo_scene_test.cpp` (runtime plays / null-safe tests)
- Modify: `tests/unit/app/audio_asset_test.cpp` (`register_audio_clips` test)

**Interfaces:**
- Produces: `setup_runtime(..., std::unique_ptr<AudioBackend> audio_backend = nullptr)`; `mosaic::app::kToneClipId`; `mosaic::app::make_demo_tone() -> std::shared_ptr<AudioClip>`; `mosaic::app::register_audio_clips(AudioBackend&, AssetRegistry&) -> int`.

- [ ] **Step 1: Add audio includes + `<cmath>` to `demo_scene.hpp`**

In `app/run/demo_scene.hpp`, after `#include "engine/animation/animation_system.hpp"`, add:

```cpp
#include "engine/audio/audio_clip.hpp"
#include "engine/audio/audio_components.hpp"
#include "engine/audio/audio_system.hpp"
```

And in the standard-library includes block (near `#include <memory>`), add:

```cpp
#include <cmath>
```

- [ ] **Step 2: Add `kToneClipId` + `make_demo_tone`**

In `app/run/demo_scene.hpp`, immediately after the `kStripesTextureId` constant block, add:

```cpp
// Well-known AssetID for the procedural demo tone (A4). Same fixed-id pattern as the textures.
inline const mosaic::foundation::serialization::AssetID kToneClipId{
    std::array<std::uint8_t, 16>{0xA0, 0xD1, 0x0}};

// A short looping stereo 440 Hz sine, generated in code (no committed binary). Used as the
// default scene's demo sound so `mosaic run --window` is audible.
inline std::shared_ptr<mosaic::engine::audio::AudioClip> make_demo_tone()
{
    const std::uint32_t rate     = 48000;
    const std::uint32_t channels = 2;
    const std::uint64_t frames   = rate / 2; // 0.5 s
    auto                clip      = std::make_shared<mosaic::engine::audio::AudioClip>();
    clip->channels    = channels;
    clip->sample_rate = rate;
    clip->frames      = frames;
    clip->pcm.resize(static_cast<std::size_t>(frames) * channels);
    for (std::uint64_t i = 0; i < frames; ++i)
    {
        const float s = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f *
                                        static_cast<float>(i) / static_cast<float>(rate));
        clip->pcm[i * channels + 0] = s;
        clip->pcm[i * channels + 1] = s;
    }
    return clip;
}
```

- [ ] **Step 3: Add the audio backend param to `setup_runtime`**

In `app/run/demo_scene.hpp`, change the `setup_runtime` signature by adding a final parameter. Replace:

```cpp
    mosaic::engine::script::ScriptHost                      host = {},
    bool                                                    script_thread = false)
{
```

with:

```cpp
    mosaic::engine::script::ScriptHost                      host = {},
    bool                                                    script_thread = false,
    std::unique_ptr<mosaic::engine::audio::AudioBackend>    audio_backend = nullptr)
{
```

- [ ] **Step 4: Wire `AudioSystem` + register the demo tone**

In `setup_runtime`, immediately after the `ParticleSystem` line
(`world.add_system<mosaic::engine::particles::ParticleSystem>();`), add:

```cpp
    // Audio: an injected backend (null in headless/tests -> AudioSystem no-ops). When a real
    // backend is present, register the procedural demo tone so the default scene is audible.
    if (audio_backend != nullptr)
    {
        auto tone = make_demo_tone();
        audio_backend->register_clip(kToneClipId, tone->pcm.data(), tone->frames, tone->channels,
                                     tone->sample_rate);
        mosaic::engine::assets::AssetMetadata tone_meta;
        tone_meta.uuid    = kToneClipId;
        tone_meta.type    = "audio";
        tone_meta.version = 1;
        registry.register_resource<mosaic::engine::audio::AudioClip>(tone_meta, tone);
    }
    world.add_system<mosaic::engine::audio::AudioSystem>(std::move(audio_backend));
```

- [ ] **Step 5: Attach demo audio to the camera + orbiter**

In `build_default_scene`, after the camera's `Camera` component line
(`world.add_component<render::Camera>(camera, render::Camera{});`), add:

```cpp
    world.add_component<mosaic::engine::audio::AudioListener>(
        camera, mosaic::engine::audio::AudioListener{});
```

Then, after the orbiter's trail `ParticleEmitter` is added
(`world.add_component<mosaic::engine::particles::ParticleEmitter>(orbiter, trail);`), add:

```cpp
    // A looping 3D tone on the orbiter — pans as it circles (script-driven emission's sibling
    // for audio). References kToneClipId, which setup_runtime registers with the backend.
    mosaic::engine::audio::AudioSource orbiter_sound;
    orbiter_sound.clip    = kToneClipId;
    orbiter_sound.gain    = 0.5f;
    orbiter_sound.looping = true;
    orbiter_sound.spatial = true;
    orbiter_sound.playing = true;
    world.add_component<mosaic::engine::audio::AudioSource>(orbiter, orbiter_sound);
```

- [ ] **Step 6: Add `register_audio_clips` to `asset_io.hpp`**

In `app/asset/asset_io.hpp`, add `#include <vector>` to the includes and
`#include "engine/audio/audio_backend.hpp"` (next to the other engine includes;
`AudioClip` already comes via `app/asset/audio_cook.hpp` — ensure that is included,
which A3 added). Then, after `load_project_assets`, add:

```cpp
// Register every loaded "audio" asset in `registry` with `backend` so play(clip) can find its
// PCM. Returns the count registered. Called once at startup after load_project_assets.
inline int register_audio_clips(mosaic::engine::audio::AudioBackend& backend,
                                mosaic::engine::assets::AssetRegistry& registry)
{
    std::vector<mosaic::foundation::serialization::AssetID> audio_ids;
    registry.for_each(
        [&](const mosaic::engine::assets::AssetMetadata& meta)
        {
            if (meta.type == "audio") { audio_ids.push_back(meta.uuid); }
        });

    int count = 0;
    for (const auto& id : audio_ids)
    {
        auto clip = registry.load<mosaic::engine::audio::AudioClip>(id);
        if (clip && backend.register_clip(id, clip->pcm.data(), clip->frames, clip->channels,
                                          clip->sample_rate))
        {
            ++count;
        }
    }
    return count;
}
```

(If `app/asset/audio_cook.hpp` is not already included in `asset_io.hpp` from A3, add
`#include "app/asset/audio_cook.hpp"`.)

- [ ] **Step 7: Write the runtime tests in `demo_scene_test.cpp`**

In `tests/unit/app/demo_scene_test.cpp`, add includes at the top (after the existing includes):

```cpp
#include "engine/audio/audio_backend.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"
```

Append these tests:

```cpp
TEST_CASE("setup_runtime + default scene starts the demo sound on a real backend", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry    reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    auto  backend = mosaic::audio::miniaudio::create_miniaudio_backend(/*null_device=*/true);
    REQUIRE(backend != nullptr);
    auto* raw = backend.get();
    setup_runtime(world, registry, types,
                  [] { return mosaic::script::python::create_python_script_engine(); }, {}, false,
                  std::move(backend));
    build_default_scene(world);

    world.tick(0.0f); // AudioSystem start-edge -> the orbiter's AudioSource plays voice 1
    REQUIRE(raw->is_playing(1));
}

TEST_CASE("setup_runtime + default scene is silent and safe with a null audio backend", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry    reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    setup_runtime(world, registry, types,
                  [] { return mosaic::script::python::create_python_script_engine(); }, {}, false,
                  nullptr); // no audio backend
    build_default_scene(world);
    REQUIRE_NOTHROW(world.tick(0.0f)); // AudioSystem no-ops; no crash
}
```

- [ ] **Step 8: Write the `register_audio_clips` test in `audio_asset_test.cpp`**

In `tests/unit/app/audio_asset_test.cpp`, append:

```cpp
TEST_CASE("register_audio_clips hands loaded audio assets to a backend", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj_reg");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("src_reg") + ".wav";
    REQUIRE(save_scene_file(src, make_wav16(48000, 2000)));
    const auto id = import_asset(proj, src);
    REQUIRE(id.has_value());

    AssetRegistry registry;
    REQUIRE(load_project_assets(registry, (fs::path(proj) / "assets").string()) == 1);

    auto backend = create_miniaudio_backend(/*null_device=*/true);
    REQUIRE(backend != nullptr);
    REQUIRE(mosaic::app::register_audio_clips(*backend, registry) == 1);
    REQUIRE(backend->play(*id, 1.0f, 1.0f, false, true) != 0); // clip is now playable

    fs::remove_all(proj); fs::remove(src);
}
```

- [ ] **Step 9: Build and run the audio wiring tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "demo sound|silent and safe|register_audio_clips|build_default_scene|populate_world" --output-on-failure`
Expected: PASS — the two runtime tests, the `register_audio_clips` test, and the existing default-scene count tests (unchanged).

- [ ] **Step 10: Run the full suite**

Run: `ctest --test-dir build/debug`
Expected: PASS. (If the threaded `ScriptWorker` case flakes, re-run: `ctest --test-dir build/debug --rerun-failed --output-on-failure`.)

- [ ] **Step 11: Commit**

```bash
git add app/run/demo_scene.hpp app/asset/asset_io.hpp tests/unit/app/demo_scene_test.cpp tests/unit/app/audio_asset_test.cpp
git commit -m "feat(audio): wire AudioSystem into setup_runtime + demo tone + register_audio_clips

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `main.cpp` device backend + app link

**Files:**
- Modify: `app/main.cpp` (create backend, pass to `setup_runtime`, register project clips)
- Modify: `app/CMakeLists.txt` (link `mosaic_audio_miniaudio` into the `mosaic` exe)

**Interfaces:**
- Consumes: `create_miniaudio_backend` (A2), `register_audio_clips` + `setup_runtime` (Task 1).

- [ ] **Step 1: Include the audio headers in `main.cpp`**

In `app/main.cpp`, add near the other module includes:

```cpp
#include "engine/audio/audio_backend.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"
```

- [ ] **Step 2: Create the backend and pass it to `setup_runtime`**

In `app/main.cpp`, replace the `setup_runtime` call:

```cpp
    auto render_system =
        setup_runtime(world, asset_registry, types,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      script_host, options.script_thread)
            .render_system;
```

with:

```cpp
    // Real audio device only for the interactive windowed run; headless runs stay silent.
    std::unique_ptr<mosaic::engine::audio::AudioBackend> audio =
        options.window ? mosaic::audio::miniaudio::create_miniaudio_backend() : nullptr;
    mosaic::engine::audio::AudioBackend* audio_raw = audio.get();

    auto render_system =
        setup_runtime(world, asset_registry, types,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      script_host, options.script_thread, std::move(audio))
            .render_system;
```

- [ ] **Step 3: Register project clips with the backend after load**

In `app/main.cpp`, in the `if (options.assets_dir)` block, after the
`load_project_assets` lines, add (inside the block, after the existing log line):

```cpp
        if (audio_raw != nullptr)
        {
            const int a = mosaic::app::register_audio_clips(*audio_raw, asset_registry);
            if (a > 0) { mosaic::foundation::log::info("audio", "registered {} audio clip(s)", a); }
        }
```

- [ ] **Step 4: Link the miniaudio module into the `mosaic` executable**

In `app/CMakeLists.txt`, change:

```cmake
add_executable(mosaic main.cpp)
target_link_libraries(mosaic PRIVATE mosaic_app)
```

to:

```cmake
add_executable(mosaic main.cpp)
target_link_libraries(mosaic PRIVATE mosaic_app mosaic_audio_miniaudio)
```

- [ ] **Step 5: Build the app and smoke-run**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -6; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0, no new Vulkan validation errors. (On a machine with audio the orbiter tone is audible; in the sandbox the device may be null/absent — the run must still exit 0. If the real-device init prints a benign warning, that is acceptable; a crash or non-zero exit is not.)

- [ ] **Step 6: Commit**

```bash
git add app/main.cpp app/CMakeLists.txt
git commit -m "feat(audio): mosaic run --window opens a device + registers project clips

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Handoff doc update

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update the handoff doc**

In `docs/project/next-steps.md`: add an **Audio — A4 done** entry — `setup_runtime`
gains an injected (defaulted-null) audio backend + adds `AudioSystem`; a procedural
demo tone (`kToneClipId`) plays from the orbiter (3D) with an `AudioListener` on the
camera; `register_audio_clips` hands cooked project clips to the backend;
`mosaic run --window` opens a real miniaudio device (headless/tests stay silent).
**The Audio pillar (A1–A4) is complete — sound plays on hardware (maintainer to
confirm audibly).** Note follow-ups: editor audio, listener orientation from the
camera rotation, attenuation/doppler, one-shot cleanup / voice cap, script bindings.
Update the top "Last updated" summary and drop Audio from the active-subsystem list
(remaining: VFX, UI, networking).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(audio): next-steps — audio A4 done (pillar complete)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `setup_runtime` gains defaulted `unique_ptr<AudioBackend>`; adds `AudioSystem`; registers demo tone when non-null → Task 1 Steps 3-4. ✓
- `kToneClipId` + `make_demo_tone` → Task 1 Step 2. ✓
- `AudioListener` (camera) + `AudioSource` (orbiter), existing entities → Task 1 Step 5. ✓
- `register_audio_clips` helper → Task 1 Step 6; test Step 8. ✓
- `main.cpp` device backend (window only) + project-clip registration + exe link → Task 2. ✓
- Tests: demo tone plays via runtime (null device), null-backend safe, register_audio_clips, counts unchanged → Task 1 Steps 7-9. ✓
- Editor left silent (default null) → no editor.cpp edit. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `setup_runtime(..., std::unique_ptr<audio::AudioBackend> = nullptr)`; `make_demo_tone() -> shared_ptr<AudioClip>`; `register_audio_clips(AudioBackend&, AssetRegistry&) -> int`; `create_miniaudio_backend(bool)`; `backend->register_clip(AssetID,const float*,uint64,uint32,uint32)` / `play(AssetID,float,float,bool,bool)` match A2; `AssetRegistry::for_each(fn(const AssetMetadata&))` + `load<AudioClip>` match the registry API; audio components reflected/registered (A1). ✓
