# Audio A2 — miniaudio device backend — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the neutral `AudioBackend` on miniaudio in a `modules/audio_miniaudio` module, proven headless against miniaudio's null device.

**Architecture:** Follows the Box3D/CPython module-wall pattern: `ma_*` types stay inside `miniaudio_backend.cpp`; the app gets a neutral `create_miniaudio_backend(bool null_device)` factory. Two small A1-interface refinements land first (`play` gains `bool spatial`; a neutral `register_clip` PCM seam). Not wired into `setup_runtime` (deferred to A4).

**Tech Stack:** C++20, miniaudio v0.11.21 (single header via FetchContent), Catch2, CMake/CTest. Links only `Threads::Threads` + `${CMAKE_DL_LIBS}` + `m` (miniaudio runtime-links OS backends; no ALSA/Pulse dev headers).

**Spec:** [docs/superpowers/specs/2026-09-07-audio-a2-miniaudio-design.md](2026-09-07-audio-a2-miniaudio-design.md)

## Global Constraints

- C++20; `ma_*` types confined to `miniaudio_backend.cpp` (module wall).
- miniaudio pinned to `GIT_TAG 0.11.21`.
- Tests construct the backend with `create_miniaudio_backend(/*null_device=*/true)` — no hardware.
- A `Voice` (its `ma_sound` + `ma_audio_buffer`) is heap-owned via `unique_ptr` so its address is stable (miniaudio objects are non-movable); the buffer must outlive the sound and is uninited after it.
- `register_clip` takes interleaved **f32** PCM; invalid (null ptr / zero frames / zero channels / zero rate) → `false`.
- `play` of an unregistered clip returns `0`; `is_playing`/`stop`/setters on an unknown voice are safe no-ops.
- New tests tagged `[audio]`; `mosaic_audio_miniaudio` linked into `mosaic_unit_tests`. Build target `mosaic_unit_tests`; preset build dir `build/debug`.

---

### Task 1: A1-interface refinements (`spatial` + `register_clip`)

**Files:**
- Modify: `engine/audio/audio_backend.hpp` (add `bool spatial` to `play`; add `register_clip`)
- Modify: `engine/audio/audio_system.hpp` (pass `src.spatial` to `play`)
- Modify: `tests/unit/audio/audio_system_test.cpp` (`FakeAudioBackend`: new `play` signature + `register_clip` stub)

**Interfaces:**
- Produces: `AudioBackend::play(AssetID, float, float, bool looping, bool spatial)`, `AudioBackend::register_clip(AssetID, const float*, std::uint64_t, std::uint32_t, std::uint32_t) -> bool`.

- [ ] **Step 1: Update the neutral interface**

In `engine/audio/audio_backend.hpp`, add `#include <cstdint>` is already present. Change the `play` pure virtual and add `register_clip` right after it:

```cpp
    virtual VoiceId play(AssetID clip, float gain, float pitch, bool looping, bool spatial) = 0;
    // Register decoded interleaved f32 PCM for a clip id so play(clip,...) can use it.
    // Returns false on invalid data. Re-registering an id replaces it.
    virtual bool register_clip(AssetID clip, const float* interleaved, std::uint64_t frames,
                               std::uint32_t channels, std::uint32_t sample_rate) = 0;
```

(The existing `play` line — `virtual VoiceId play(AssetID clip, float gain, float pitch, bool looping) = 0;` — is replaced by the two lines above.)

- [ ] **Step 2: Pass `spatial` from the system**

In `engine/audio/audio_system.hpp`, in the start-edge branch, change the `play` call:

```cpp
                    st.id    = backend_->play(src.clip, src.gain, src.pitch, src.looping, src.spatial);
```

- [ ] **Step 3: Update the FakeAudioBackend in the A1 system test**

In `tests/unit/audio/audio_system_test.cpp`, update `FakeAudioBackend`:

- Change the `PlayCall` struct to include `spatial`:

```cpp
    struct PlayCall { AssetID clip; float gain; float pitch; bool looping; bool spatial; };
```

- Change the `play` override signature and body:

```cpp
    VoiceId play(AssetID clip, float gain, float pitch, bool looping, bool spatial) override
    {
        plays.push_back(PlayCall{clip, gain, pitch, looping, spatial});
        return next_voice_++;
    }
```

- Add a `register_clip` override (after `play`):

```cpp
    std::vector<AssetID> registered;
    bool register_clip(AssetID clip, const float*, std::uint64_t, std::uint32_t,
                       std::uint32_t) override
    {
        registered.push_back(clip);
        return true;
    }
```

- [ ] **Step 4: Build and run the audio tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "Audio" --output-on-failure`
Expected: PASS — all A1 `[audio]` cases still green with the new `play` signature (the start-edge test still asserts gain/pitch/looping; `spatial` defaults to `true` in `AudioSource`).

- [ ] **Step 5: Commit**

```bash
git add engine/audio/audio_backend.hpp engine/audio/audio_system.hpp tests/unit/audio/audio_system_test.cpp
git commit -m "refactor(audio): play() gains spatial + neutral register_clip seam

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: miniaudio backend module + FetchContent + tests

**Files:**
- Modify: `CMakeLists.txt` (FetchContent miniaudio + `add_subdirectory`)
- Create: `modules/audio_miniaudio/CMakeLists.txt`
- Create: `modules/audio_miniaudio/miniaudio_backend.hpp`
- Create: `modules/audio_miniaudio/miniaudio_backend.cpp`
- Modify: `tests/CMakeLists.txt` (link module + add test)
- Create: `tests/unit/audio/miniaudio_backend_test.cpp`

**Interfaces:**
- Consumes: the refined `AudioBackend` (Task 1); `AudioSource`, `AudioSystem` (for the integration test).
- Produces: `mosaic::audio::miniaudio::create_miniaudio_backend(bool null_device = false) -> std::unique_ptr<AudioBackend>`.

- [ ] **Step 1: FetchContent miniaudio in the top-level `CMakeLists.txt`**

After the line `FetchContent_MakeAvailable(box3d)`, add:

```cmake

# --- miniaudio (audio backend; single header, pinned) -------------------------
FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.21
)
FetchContent_MakeAvailable(miniaudio)
```

Then, after the line `add_subdirectory(modules/script_python)`, add:

```cmake
add_subdirectory(modules/audio_miniaudio)
```

- [ ] **Step 2: Create `modules/audio_miniaudio/CMakeLists.txt`**

```cmake
add_library(mosaic_audio_miniaudio STATIC miniaudio_backend.cpp)
target_include_directories(mosaic_audio_miniaudio
  PUBLIC  ${CMAKE_SOURCE_DIR}
  PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(mosaic_audio_miniaudio
  PUBLIC  mosaic_engine
  PRIVATE mosaic_foundation Threads::Threads ${CMAKE_DL_LIBS} m)
target_compile_features(mosaic_audio_miniaudio PUBLIC cxx_std_20)
```

- [ ] **Step 3: Create `modules/audio_miniaudio/miniaudio_backend.hpp`**

```cpp
#pragma once

#include <memory>

#include "engine/audio/audio_backend.hpp"

namespace mosaic::audio::miniaudio
{
// Constructs a miniaudio-backed AudioBackend, or nullptr if the audio engine could
// not initialize. ma_* stays inside the .cpp. null_device = true forces miniaudio's
// null backend (no hardware) for headless tests; false auto-selects the OS device.
std::unique_ptr<mosaic::engine::audio::AudioBackend> create_miniaudio_backend(
    bool null_device = false);
} // namespace mosaic::audio::miniaudio
```

- [ ] **Step 4: Create `modules/audio_miniaudio/miniaudio_backend.cpp`**

```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "modules/audio_miniaudio/miniaudio_backend.hpp"

namespace mosaic::audio::miniaudio
{
namespace
{
using mosaic::engine::audio::AudioBackend;
using mosaic::engine::audio::ListenerState;
using mosaic::engine::audio::VoiceId;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

class MiniaudioBackend : public AudioBackend
{
public:
    explicit MiniaudioBackend(bool null_device)
    {
        ma_context_config ctxConfig    = ma_context_config_init();
        ma_backend        nullList[]   = {ma_backend_null};
        ma_backend*       backends     = null_device ? nullList : nullptr;
        ma_uint32         backendCount = null_device ? 1u : 0u;
        if (ma_context_init(backends, backendCount, &ctxConfig, &context_) != MA_SUCCESS)
        {
            return;
        }
        context_ok_ = true;

        ma_engine_config engConfig = ma_engine_config_init();
        engConfig.pContext         = &context_;
        if (ma_engine_init(&engConfig, &engine_) != MA_SUCCESS) { return; }
        engine_ok_ = true;
    }

    ~MiniaudioBackend() override
    {
        for (auto& [id, v] : voices_)
        {
            ma_sound_uninit(&v->sound);
            ma_audio_buffer_uninit(&v->buffer);
        }
        voices_.clear();
        if (engine_ok_) { ma_engine_uninit(&engine_); }
        if (context_ok_) { ma_context_uninit(&context_); }
    }

    bool ok() const { return engine_ok_; }

    bool register_clip(AssetID clip, const float* interleaved, std::uint64_t frames,
                       std::uint32_t channels, std::uint32_t sample_rate) override
    {
        if (interleaved == nullptr || frames == 0 || channels == 0 || sample_rate == 0)
        {
            return false;
        }
        Clip c;
        c.channels    = channels;
        c.sample_rate = sample_rate;
        c.frames      = frames;
        c.pcm.assign(interleaved,
                     interleaved + static_cast<std::size_t>(frames) * channels);
        clips_[clip] = std::move(c);
        return true;
    }

    VoiceId play(AssetID clip, float gain, float pitch, bool looping, bool spatial) override
    {
        if (!engine_ok_) { return 0; }
        auto it = clips_.find(clip);
        if (it == clips_.end()) { return 0; }
        const Clip& c = it->second;

        auto v = std::make_unique<Voice>();
        ma_audio_buffer_config bufConfig =
            ma_audio_buffer_config_init(ma_format_f32, c.channels, c.frames, c.pcm.data(), nullptr);
        bufConfig.sampleRate = c.sample_rate;
        if (ma_audio_buffer_init(&bufConfig, &v->buffer) != MA_SUCCESS) { return 0; }

        if (ma_sound_init_from_data_source(&engine_, &v->buffer, 0, nullptr, &v->sound) != MA_SUCCESS)
        {
            ma_audio_buffer_uninit(&v->buffer);
            return 0;
        }
        ma_sound_set_volume(&v->sound, gain);
        ma_sound_set_pitch(&v->sound, pitch);
        ma_sound_set_looping(&v->sound, looping ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(&v->sound, spatial ? MA_TRUE : MA_FALSE);
        ma_sound_start(&v->sound);

        const VoiceId id = next_voice_++;
        voices_.emplace(id, std::move(v));
        return id;
    }

    void stop(VoiceId voice) override
    {
        auto it = voices_.find(voice);
        if (it == voices_.end()) { return; }
        ma_sound_stop(&it->second->sound);
        ma_sound_uninit(&it->second->sound);
        ma_audio_buffer_uninit(&it->second->buffer);
        voices_.erase(it);
    }

    void set_gain(VoiceId voice, float gain) override
    {
        if (Voice* v = find(voice)) { ma_sound_set_volume(&v->sound, gain); }
    }

    void set_pitch(VoiceId voice, float pitch) override
    {
        if (Voice* v = find(voice)) { ma_sound_set_pitch(&v->sound, pitch); }
    }

    void set_voice_position(VoiceId voice, const Vec3& p) override
    {
        if (Voice* v = find(voice)) { ma_sound_set_position(&v->sound, p.x, p.y, p.z); }
    }

    bool is_playing(VoiceId voice) const override
    {
        auto it = voices_.find(voice);
        return it != voices_.end() && ma_sound_is_playing(&it->second->sound) == MA_TRUE;
    }

    void set_listener(const ListenerState& l) override
    {
        if (!engine_ok_) { return; }
        ma_engine_listener_set_position(&engine_, 0, l.position.x, l.position.y, l.position.z);
        ma_engine_listener_set_direction(&engine_, 0, l.forward.x, l.forward.y, l.forward.z);
        ma_engine_listener_set_world_up(&engine_, 0, l.up.x, l.up.y, l.up.z);
    }

private:
    struct Clip
    {
        std::vector<float> pcm;
        ma_uint32          channels    = 0;
        ma_uint32          sample_rate = 0;
        ma_uint64          frames      = 0;
    };
    struct Voice
    {
        ma_audio_buffer buffer{};
        ma_sound        sound{};
    };
    Voice* find(VoiceId voice)
    {
        auto it = voices_.find(voice);
        return it == voices_.end() ? nullptr : it->second.get();
    }

    ma_context context_{};
    ma_engine  engine_{};
    bool       context_ok_ = false;
    bool       engine_ok_  = false;
    VoiceId    next_voice_ = 1;
    std::unordered_map<AssetID, Clip>                   clips_;
    std::unordered_map<VoiceId, std::unique_ptr<Voice>> voices_;
};
} // namespace

std::unique_ptr<mosaic::engine::audio::AudioBackend> create_miniaudio_backend(bool null_device)
{
    auto backend = std::make_unique<MiniaudioBackend>(null_device);
    if (!backend->ok()) { return nullptr; }
    return backend;
}
} // namespace mosaic::audio::miniaudio
```

- [ ] **Step 5: Link the module + add the test in `tests/CMakeLists.txt`**

After the line `  unit/audio/audio_system_test.cpp`, add:

```cmake
  unit/audio/miniaudio_backend_test.cpp
```

And in the `target_link_libraries(mosaic_unit_tests PRIVATE ...)` list, after the `mosaic_script_python` line, add:

```cmake
  mosaic_audio_miniaudio
```

- [ ] **Step 6: Create `tests/unit/audio/miniaudio_backend_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "engine/audio/audio_components.hpp"
#include "engine/audio/audio_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/reference.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"

using mosaic::audio::miniaudio::create_miniaudio_backend;
using mosaic::engine::World;
using mosaic::engine::audio::AudioBackend;
using mosaic::engine::audio::AudioSource;
using mosaic::engine::audio::AudioSystem;
using mosaic::engine::audio::VoiceId;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

namespace
{
// A short generated stereo f32 clip so tests have real PCM to play.
std::vector<float> sine_pcm(std::uint32_t channels, std::uint64_t frames)
{
    std::vector<float> pcm(static_cast<std::size_t>(frames) * channels);
    for (std::uint64_t i = 0; i < frames; ++i)
    {
        const float s = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f * (float)i / 48000.0f);
        for (std::uint32_t ch = 0; ch < channels; ++ch) { pcm[i * channels + ch] = s; }
    }
    return pcm;
}
const AssetID kClip = AssetID::from_string("00000000-0000-0000-0000-0000000000a1");
}

TEST_CASE("miniaudio backend initializes on the null device", "[audio]")
{
    auto backend = create_miniaudio_backend(/*null_device=*/true);
    REQUIRE(backend != nullptr);
}

TEST_CASE("miniaudio backend plays a registered clip", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    REQUIRE(backend != nullptr);
    const auto pcm = sine_pcm(2, 12000);
    REQUIRE(backend->register_clip(kClip, pcm.data(), 12000, 2, 48000));

    const VoiceId v = backend->play(kClip, 1.0f, 1.0f, false, true);
    REQUIRE(v != 0);
    REQUIRE(backend->is_playing(v));
}

TEST_CASE("miniaudio backend returns no voice for an unregistered clip", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    REQUIRE(backend != nullptr);
    const AssetID unknown = AssetID::from_string("00000000-0000-0000-0000-0000000000ff");
    REQUIRE(backend->play(unknown, 1.0f, 1.0f, false, true) == 0);
}

TEST_CASE("miniaudio backend stops a voice", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);
    const VoiceId v = backend->play(kClip, 1.0f, 1.0f, false, true);
    REQUIRE(backend->is_playing(v));
    backend->stop(v);
    REQUIRE_FALSE(backend->is_playing(v));
}

TEST_CASE("miniaudio backend voice ops and unknown-voice calls are safe", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);
    const VoiceId v = backend->play(kClip, 1.0f, 1.0f, false, true);

    backend->set_gain(v, 0.5f);
    backend->set_pitch(v, 1.5f);
    backend->set_voice_position(v, Vec3{1.0f, 2.0f, 3.0f});

    // Unknown / zero voice must be safe.
    backend->set_gain(0, 0.5f);
    backend->stop(0);
    REQUIRE_FALSE(backend->is_playing(0));
    REQUIRE_FALSE(backend->is_playing(999999));
}

TEST_CASE("miniaudio backend plays two independent voices of one clip", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);

    const VoiceId a = backend->play(kClip, 1.0f, 1.0f, false, true);
    const VoiceId b = backend->play(kClip, 1.0f, 1.0f, false, true);
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    REQUIRE(a != b);
    REQUIRE(backend->is_playing(a));
    REQUIRE(backend->is_playing(b));
    backend->stop(a);
    REQUIRE_FALSE(backend->is_playing(a));
    REQUIRE(backend->is_playing(b));
}

TEST_CASE("miniaudio backend rejects invalid clip data", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 4);
    REQUIRE_FALSE(backend->register_clip(kClip, nullptr, 4, 2, 48000));
    REQUIRE_FALSE(backend->register_clip(kClip, pcm.data(), 0, 2, 48000));
    REQUIRE_FALSE(backend->register_clip(kClip, pcm.data(), 4, 0, 48000));
    REQUIRE_FALSE(backend->register_clip(kClip, pcm.data(), 4, 2, 0));
}

TEST_CASE("AudioSystem drives the real miniaudio backend", "[audio]")
{
    World world;
    auto  backend = create_miniaudio_backend(true);
    REQUIRE(backend != nullptr);
    auto* raw = backend.get(); // stays valid after the move (the system owns it, same object)
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s;
    s.clip    = kClip;
    s.playing = true;
    s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f); // start edge -> backend->play assigns the first voice id (1)
    REQUIRE(raw->is_playing(1)); // the system actually started a voice on the real backend
}
```

- [ ] **Step 7: Build and run the miniaudio tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "miniaudio|AudioSystem drives" --output-on-failure`
Expected: PASS — the null-device backend cases and the AudioSystem-drives-backend integration case. (First build fetches miniaudio; allow extra time.)

- [ ] **Step 8: Run the full suite**

Run: `ctest --test-dir build/debug`
Expected: PASS. (If the threaded `ScriptWorker` case flakes, re-run: `ctest --test-dir build/debug --rerun-failed --output-on-failure`.)

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt modules/audio_miniaudio tests/CMakeLists.txt tests/unit/audio/miniaudio_backend_test.cpp
git commit -m "feat(audio): miniaudio device backend (headless null-device tested)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Handoff doc update

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update the handoff doc**

In `docs/project/next-steps.md`: add an **Audio — A2 done** entry — a `modules/audio_miniaudio` implementing the neutral `AudioBackend` on miniaudio (v0.11.21, FetchContent), `play` now carries `spatial`, a neutral `register_clip` PCM seam, `create_miniaudio_backend(null_device)` factory; proven headless on the **null device** (8 tests) since the sandbox has no audio hardware. Note the remaining audio slices: **A3** cooked `.wav`→audio-clip asset + `AssetRegistry` loading feeding `register_clip`; **A4** inject the real-device backend + wire `AudioSystem` into `setup_runtime` + a demo sound. Update the top "Last updated" summary.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(audio): next-steps — audio A2 (miniaudio backend) done

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `play` gains `spatial`; neutral `register_clip` → Task 1. ✓
- `AudioSystem` passes `src.spatial` → Task 1 Step 2. ✓
- FetchContent miniaudio 0.11.21 + module + CMake + wall → Task 2 Steps 1-4. ✓
- Factory `create_miniaudio_backend(bool)` returns nullptr on init failure → Task 2 Step 3-4. ✓
- Voice = heap `unique_ptr` (stable address), buffer outlives sound → Task 2 Step 4 (`Voice` + destructor/stop order). ✓
- Null-device tests: init, register+play, unknown clip→0, stop, unknown-voice-safe, two voices, invalid-data, AudioSystem-drives → Task 2 Step 6. ✓
- Not wired into `setup_runtime` → no `demo_scene`/`setup_runtime` edits. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `create_miniaudio_backend(bool null_device=false) -> unique_ptr<AudioBackend>`; `play(AssetID,float,float,bool,bool)` and `register_clip(AssetID,const float*,uint64,uint32,uint32)->bool` identical in the neutral header, the miniaudio impl, the FakeAudioBackend, and the tests; miniaudio API calls (`ma_audio_buffer_config_init`, `ma_sound_init_from_data_source`, `ma_sound_set_looping/set_spatialization_enabled/set_position/set_pitch/set_volume`, `ma_engine_listener_set_position/direction/world_up`, `ma_audio_buffer_config.sampleRate`) verified against miniaudio 0.11.21. ✓
