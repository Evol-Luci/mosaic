# Audio A4 — runtime wiring + demo sound — Design

## Goal (Phase 4, Audio pillar — slice 4, final)

Make the engine **actually produce sound**: wire `AudioSystem` + a real miniaudio
device backend into the runtime, register cooked project clips (A3) with the
backend, and add an audible demo (a procedural tone on the orbiting cube) so
`mosaic run --window` plays. This closes the Audio pillar (A1 architecture → A2
backend → A3 asset → **A4 it plays**).

**Verification honesty:** the sandbox has no audio device, so I verify **wiring +
build + headless-no-crash + the play path on miniaudio's null device**; **audible
output on real hardware is the maintainer's check.** The default backend param is
null in every test, so no test opens a device.

## Design

### `setup_runtime` gains an injected audio backend (defaulted null)

Add a final parameter `std::unique_ptr<mosaic::engine::audio::AudioBackend>
audio_backend = nullptr`. Every existing caller (the editor, all tests) compiles
unchanged and stays silent. `setup_runtime`:

- Adds `AudioSystem(std::move(audio_backend))` (after `ParticleSystem`, near the
  other gameplay systems; ordering is not load-bearing — audio reads Transforms,
  writes nothing back). `AudioSystem` already no-ops when its backend is null.
- **When the backend is non-null**, before moving it in: generate a procedural
  demo tone (`make_demo_tone()` → `shared_ptr<AudioClip>`), `register_clip` it with
  the backend under a well-known `kToneClipId`, and `register_resource<AudioClip>`
  it in the `AssetRegistry` (so the id resolves like `kCheckerTextureId` /
  `kCubeMeshId`). This mirrors how `setup_runtime` registers built-in meshes and
  demo textures.

`kToneClipId` is a fixed `AssetID` constant beside `kCheckerTextureId`.
`make_demo_tone()` synthesizes a short (~0.5 s) stereo 440 Hz sine at 48 kHz, low
amplitude — deterministic, no committed binary.

### Demo scene (`build_default_scene`) — attach audio to existing entities

- `AudioListener` on the **camera** entity (the "ears" follow the camera).
- `AudioSource{ clip = kToneClipId, gain = 0.5, looping = true, spatial = true,
  playing = true }` on the **orbiter** cube — a moving 3D sound (pans as it
  orbits). Both are attached to **existing** entities, so default-scene
  entity/component counts are unchanged. Both components are reflected (A1), so the
  scene still round-trips.

### `register_audio_clips` helper (testable seam for project clips)

Project `.wav` clips (A3) are loaded into the `AssetRegistry` after
`setup_runtime`, so they must be handed to the backend separately. Rather than
inline untested logic in `main.cpp`, add a small helper in `app/asset/asset_io.hpp`:

```cpp
// Register every loaded "audio" asset in `registry` with `backend` (so play(clip)
// can find it). Returns the count registered. Skips clips that fail to load.
int register_audio_clips(mosaic::engine::audio::AudioBackend& backend,
                         mosaic::engine::assets::AssetRegistry& registry);
```

It uses `AssetRegistry::for_each` (metadata) + `load<AudioClip>(uuid)` for each
`type == "audio"`, calling `backend.register_clip(...)`. Unit-tested directly.

### `main.cpp` — create the device backend and wire it (windowed run only)

- Before `setup_runtime`: `auto audio = options.window ?
  mosaic::audio::miniaudio::create_miniaudio_backend() : nullptr;` (headless
  `mosaic run` opens no device); keep a raw `AudioBackend* audio_raw = audio.get();`
  and pass `std::move(audio)` as the new `setup_runtime` argument.
- After `load_project_assets`: `if (audio_raw) { register_audio_clips(*audio_raw,
  asset_registry); }` — so a real project's imported clips are playable.
- The `mosaic` executable links `mosaic_audio_miniaudio` (only `main.cpp` calls
  `create_miniaudio_backend`).

The editor (`app/run/editor.cpp`) is intentionally left silent this slice (its
`setup_runtime` call uses the default null backend) — a trivial follow-up.

## Testing (headless; null device / null backend)

Extend `tests/unit/app/audio_asset_test.cpp` (or a new `[app]` test file) — the
target already links `mosaic_audio_miniaudio`.

1. **Demo tone plays through the runtime** — `setup_runtime(world, registry, types,
   python-factory, {}, false, create_miniaudio_backend(/*null_device=*/true))`
   (keep a raw backend pointer), then `build_default_scene(world)`, then
   `world.tick(0.0f)`. The orbiter's `AudioSource` (clip `kToneClipId`, registered
   by `setup_runtime`) started a voice: `raw->is_playing(1)` is true.
2. **Null backend is safe** — same, but pass `nullptr` for the backend;
   `build_default_scene` + `world.tick` runs without crashing (AudioSystem no-ops;
   the demo `AudioSource` simply produces no sound).
3. **`register_audio_clips`** — import a `.wav` (A3) into a temp project,
   `load_project_assets`, then `register_audio_clips(*create_miniaudio_backend(true),
   registry)` returns 1 and the clip is playable (`play(id, …) != 0`).
4. **Default-scene counts unchanged** — the existing `demo_scene`/`populate_world`/
   `bundle`/`scene_save_roundtrip` count tests still pass (audio components were
   attached to existing entities; no new entities).

Plus: full suite green; `mosaic run --window --ticks N` builds and exits 0 with no
new validation errors (functional check — audio init on the real device is not
asserted here).

## Out of scope / follow-ups

- Editor audio (`app/run/editor.cpp` inject a backend) — trivial follow-up.
- Listener orientation from the camera Transform's rotation (needs a quat·vec
  helper); the listener currently uses world-default forward/up.
- Distance attenuation/rolloff tuning, doppler; one-shot auto-cleanup via
  `is_playing`; voice-count cap / stealing.
- Bundling a real `.wav` in the demo project (procedural tone covers A4).
- Script bindings for audio (`play_sound(entity)` via the component path).
