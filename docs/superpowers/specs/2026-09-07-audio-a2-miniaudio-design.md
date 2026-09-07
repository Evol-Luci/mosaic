# Audio A2 — miniaudio device backend — Design

## Goal (Phase 4, Audio pillar — slice 2)

Give the Audio pillar a **real backend** that can actually produce sound on
hardware: a `modules/audio_miniaudio` implementing the neutral `AudioBackend`
(A1) on top of [miniaudio](https://github.com/mackron/miniaudio) (single-header,
public-domain/MIT-0). Follows the established module-wall pattern (Box3D physics,
CPython scripting, Vulkan render): the third-party `ma_*` types never leave the
`.cpp`; the app injects the backend through a neutral factory.

**Scope: the backend only.** Cooked audio-clip assets (A3) and `setup_runtime`
wiring + a demo sound (A4) are separate follow-up slices. This slice is **not
wired into `setup_runtime`** and adds no app/demo change.

**Verification bar (agreed):** the sandbox has no audio device, so A2 is proven
**headlessly against miniaudio's null backend** — the module builds, an engine
inits on the null device, a registered PCM clip plays, `is_playing`/`stop`/voice
cleanup behave. Audible playback on real hardware is validated later by the
maintainer. A feasibility **spike** already confirmed this path (null-backend
engine + raw-PCM `ma_sound` + `is_playing` 1→0) builds and runs with only
`-lpthread -lm -ldl`.

## Neutral-interface refinements (in `engine/audio/audio_backend.hpp`)

A1's `AudioBackend` is new and only the test fake + this module implement it, so
two small additions land here as part of A2:

1. `play` gains a `bool spatial` — the backend disables 3D spatialization for a
   non-spatial (UI/2D) voice:
   `VoiceId play(AssetID clip, float gain, float pitch, bool looping, bool spatial)`.
2. A **clip-registration seam** — how decoded PCM reaches the backend (A3 feeds
   cooked assets; A2 tests feed generated PCM):
   ```cpp
   // Register decoded interleaved f32 PCM for a clip id, so play(clip,...) can use
   // it. Returns false on invalid data. Re-registering an id replaces it.
   virtual bool register_clip(AssetID clip, const float* interleaved,
                              std::uint64_t frames, std::uint32_t channels,
                              std::uint32_t sample_rate) = 0;
   ```

`AudioSystem::update` is updated to pass `src.spatial` to `play`. The A1
`FakeAudioBackend` (in `audio_system_test.cpp`) gains the `spatial` param and a
trivial `register_clip` (records + returns true); its play-edge assertions are
unchanged otherwise.

## `modules/audio_miniaudio`

### `miniaudio_backend.hpp` — neutral factory only

```cpp
#pragma once
#include <memory>
#include "engine/audio/audio_backend.hpp"

namespace mosaic::audio::miniaudio
{
// Constructs a miniaudio-backed AudioBackend. ma_* stays inside the .cpp.
// null_device = true forces miniaudio's null backend (no hardware) — used by
// headless tests; false auto-selects the OS device (for the app, later slices).
std::unique_ptr<mosaic::engine::audio::AudioBackend>
create_miniaudio_backend(bool null_device = false);
}
```

### `miniaudio_backend.cpp` — implementation (ma_* walled here)

- `#define MINIAUDIO_IMPLEMENTATION` + `#include "miniaudio.h"`.
- A `MiniaudioBackend : public AudioBackend` (file-local) owning a `ma_context`
  (backend list = `{ma_backend_null}` when `null_device`, else default) and a
  `ma_engine` on that context.
- **Clips:** `register_clip` stores a `Clip{ std::vector<float> pcm; ma_uint32
  channels; ma_uint32 sample_rate; ma_uint64 frames; }` keyed by `AssetID`
  (owns a copy of the PCM). Invalid (null/zero) input → `false`.
- **Voices:** `play` looks up the clip (unknown → return `0`); creates a
  per-voice `ma_audio_buffer` over the stored PCM + a `ma_sound`
  (`ma_sound_init_from_data_source`), sets volume=`gain`, pitch=`pitch`, looping,
  and `ma_sound_set_spatialization_enabled(spatial)`; `ma_sound_start`; stores the
  voice under a fresh incrementing `VoiceId`; returns it.
- `stop` → `ma_sound_stop` + uninit sound & buffer + erase. `set_gain` →
  `ma_sound_set_volume`. `set_pitch` → `ma_sound_set_pitch`.
  `set_voice_position` → `ma_sound_set_position`. `is_playing` →
  `ma_sound_is_playing` (false for an unknown voice). `set_listener` →
  `ma_engine_listener_set_position/direction/world_up(engine, 0, ...)`.
- Destructor stops/uninits all voices, then `ma_engine_uninit`, `ma_context_uninit`.
- Ownership note: each voice keeps its own `ma_audio_buffer` (heap) alongside its
  `ma_sound`, uninited together — the buffer must outlive the sound.

### `CMakeLists.txt`

```cmake
add_library(mosaic_audio_miniaudio STATIC miniaudio_backend.cpp)
target_include_directories(mosaic_audio_miniaudio
  PUBLIC  ${CMAKE_SOURCE_DIR}
  PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(mosaic_audio_miniaudio
  PUBLIC  mosaic_engine
  PRIVATE mosaic_foundation ${CMAKE_DL_LIBS} Threads::Threads)
target_compile_features(mosaic_audio_miniaudio PUBLIC cxx_std_20)
```

Top-level `CMakeLists.txt`: `FetchContent_Declare(miniaudio, GIT_REPOSITORY
https://github.com/mackron/miniaudio.git, GIT_TAG 0.11.21)` +
`FetchContent_MakeAvailable(miniaudio)` (header-only: exposes
`${miniaudio_SOURCE_DIR}`), then `add_subdirectory(modules/audio_miniaudio)`.
`Threads::Threads` via `find_package(Threads REQUIRED)` if not already present.

## Testing (headless, null device)

`tests/unit/audio/miniaudio_backend_test.cpp` (tag `[audio]`), linking
`mosaic_audio_miniaudio` into `mosaic_unit_tests`. All backends constructed with
`create_miniaudio_backend(/*null_device=*/true)`.

1. **Init** — `create_miniaudio_backend(true)` returns non-null (null-device engine
   inits).
2. **Register + play** — register a generated PCM clip under an `AssetID`; `play`
   returns a non-zero `VoiceId`; `is_playing(voice)` is true.
3. **Unknown clip** — `play` of an unregistered id returns `0`.
4. **Stop** — after `stop(voice)`, `is_playing(voice)` is false.
5. **Voice ops don't crash / unknown voice safe** — `set_gain`/`set_pitch`/
   `set_voice_position` on a live voice, and `is_playing`/`stop`/`set_gain` on
   voice `0` or an unknown id, are safe (no crash; `is_playing(0)==false`).
6. **Two voices** — two `play`s of the same clip return distinct non-zero ids, both
   playing; stopping one leaves the other playing.
7. **register_clip validation** — null data or zero frames/channels → `false`.
8. **AudioSystem drives the real backend** (integration): a `World` with an
   `AudioSource{playing=true}` + the null miniaudio backend → after `tick`, the
   backend reports one playing voice (mirrors the physics real-backend ECS test).

## Out of scope / follow-ups

- **A3:** cooked audio-clip asset (import `.wav` → asset blob + metadata, like the
  T5 PPM importer), `AssetRegistry` loading, and feeding decoded PCM to
  `register_clip`.
- **A4:** inject `create_miniaudio_backend()` (real device) at the app tier, wire
  `AudioSystem` into `setup_runtime`, add a demo `AudioSource`/`AudioListener` +
  a bundled clip.
- Listener orientation already plumbed (forward/up), but `AudioSystem` still sends
  world-default orientation until a quat·vec helper lands.
- Streaming/long clips (A2 loads fully into memory), attenuation model tuning,
  voice-count cap / stealing, `.mp3`/`.ogg` decode (miniaudio supports wav/flac/mp3).
