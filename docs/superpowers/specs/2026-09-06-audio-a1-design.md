# Audio subsystem — A1 (neutral backend + AudioSource + AudioSystem) — Design

## Goal (Phase 4, subsystem: Audio — first slice)

Stand up the **Audio pillar's architecture**, headless and deterministic, following
the exact neutral-backend pattern the engine already uses for physics (Box3D),
scripting (CPython), and rendering (Vulkan): a **neutral `AudioBackend` interface**,
reflected **`AudioSource`/`AudioListener` components**, and an **`AudioSystem`** that
translates ECS state into backend commands. A concrete device backend (miniaudio /
OpenAL / SDL_audio) is **deliberately out of scope for A1** — it needs an audio
device we cannot exercise in CI/sandbox — exactly as physics P1 began behind a wall
and navigation shipped headless. A1 is proven with a **recording fake backend**,
mirroring `FakeRenderDevice` (render) and the physics fake-backend sync test.

Scope guardrails: header-only under `engine/audio/`, **no new third-party
dependency**, no real device, **not wired into `setup_runtime`** yet (a game/app
supplies a backend later — same deferral navigation used for its grid). The
`AudioSystem` **writes nothing back to components**; playing voices are transient
system-owned state keyed by entity id (the `NavigationSystem`/`ParticleSystem`
pattern).

## Design

### `engine/audio/audio_backend.hpp` — neutral interface (no third-party types)

```cpp
namespace mosaic::engine::audio
{
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

// Handle to a playing sound instance. 0 = none/invalid.
using VoiceId = std::uint64_t;

// Listener (the "ears") pose for spatialization.
struct ListenerState { Vec3 position; Vec3 forward; Vec3 up; };

// Implemented by a device module (miniaudio/OpenAL/...) later; A1 ships only a
// recording fake used in tests. b*/ma_*/al* types never leak past this wall.
class AudioBackend
{
public:
    virtual ~AudioBackend() = default;
    virtual VoiceId play(AssetID clip, float gain, float pitch, bool looping) = 0;
    virtual void    stop(VoiceId voice) = 0;
    virtual void    set_gain(VoiceId voice, float gain) = 0;
    virtual void    set_pitch(VoiceId voice, float pitch) = 0;
    virtual void    set_voice_position(VoiceId voice, const Vec3& position) = 0;
    virtual bool    is_playing(VoiceId voice) const = 0;
    virtual void    set_listener(const ListenerState& listener) = 0;
};
}
```

### `engine/audio/audio_components.hpp` — reflected components

```cpp
struct AudioSource
{
    AssetID clip{};          // which sound asset (nil = nothing)
    float   gain    = 1.0f;  // linear volume
    float   pitch   = 1.0f;  // playback rate multiplier
    bool    looping = false;
    bool    playing = false; // authored intent: should this be playing?
    bool    spatial = true;  // 3D-positioned at the entity's Transform, vs 2D/UI
};

// Marker: the entity carrying this (with a Transform) is the audio listener.
struct AudioListener {};
```

Both reflected and registered (`"AudioSource"`, `"AudioListener"`) in
`register_builtin_components`, so they serialize with scenes and show in the
Inspector. `AudioListener` has no fields (an empty `reflect`).

### `engine/audio/audio_system.hpp` — `AudioSystem : ecs::System`

Injected backend (`explicit AudioSystem(std::unique_ptr<AudioBackend>)`), like
`PhysicsSystem`. Transient per-entity voice state:

```cpp
struct Voice { VoiceId id = 0; bool was_playing = false; float gain = 1.0f; float pitch = 1.0f; };
std::unordered_map<std::uint64_t, Voice> voices_;
```

Each `update(world, dt)`:

1. **Listener:** find the first entity with `AudioListener` + `Transform`; call
   `backend_->set_listener({ tf.position, {0,0,-1}, {0,1,0} })`. (A1 uses
   world-default orientation; deriving forward/up from the Transform's rotation is a
   documented follow-up — there is no quat·vec helper yet.) If no listener entity,
   skip.
2. Per entity with an `AudioSource` (mark `seen`):
   - **Start edge** — `src.playing && !st.was_playing`: `st.id = backend_->play(src.clip,
     src.gain, src.pitch, src.looping)`; record `st.gain/st.pitch`.
   - **Stop edge** — `!src.playing && st.was_playing`: if `st.id != 0`
     `backend_->stop(st.id)`; `st.id = 0`.
   - **While active** (`st.id != 0`): if `src.gain != st.gain` → `set_gain` + update;
     if `src.pitch != st.pitch` → `set_pitch` + update; if `src.spatial` →
     `set_voice_position(st.id, tf.position)` (needs a `Transform`; skip if none).
   - `st.was_playing = src.playing`.
3. **Prune:** for `voices_` whose entity was not `seen`, if `id != 0`
   `backend_->stop(id)`, then erase (stop a destroyed entity's voice).

The system does **not** poll `is_playing` (kept in the interface for the real backend
and tests); a finished one-shot simply stays "played" until the author toggles
`playing` off→on again — so tick logic is edge-based and fully deterministic without
depending on real audio time.

### Registration & wiring

- `engine/reflection/builtin_components.hpp` — include `engine/audio/audio_components.hpp`
  and register `"AudioSource"` / `"AudioListener"`.
- **Not** added to `setup_runtime` (no device backend in A1). A follow-up wires a real
  backend module and adds `AudioSystem` to the runtime.

## Testing (headless, TDD)

New `tests/unit/audio/audio_system_test.cpp` (tag `[audio]`), added to
`tests/CMakeLists.txt`. A **`FakeAudioBackend`** (defined in the test) records calls:
`play` returns an incrementing `VoiceId` and logs `(clip,gain,pitch,looping)`; `stop`,
`set_gain`, `set_pitch`, `set_voice_position`, `set_listener` log their args;
`is_playing` returns a settable flag (default true).

1. **Play on start edge** — `playing=true`, one tick → exactly one `play` with the
   source's clip/gain/pitch/looping; the voice is tracked.
2. **No duplicate play** — ticking again with `playing` still true → still one `play`.
3. **Stop on stop edge** — set `playing=false`, tick → one `stop` for the voice.
4. **Spatial position pushed** — `spatial=true` → `set_voice_position` called each
   active tick with the entity's `Transform.position`.
5. **Non-spatial** — `spatial=false` → no `set_voice_position`.
6. **Gain change pushed** — change `gain` while active → `set_gain` recorded with the
   new value (pitch analogous).
7. **Listener** — an `AudioListener` entity with a `Transform` → `set_listener`
   recorded with that entity's position.
8. **Prune stops the voice** — destroy the source entity, tick → `stop` recorded and
   the voice state dropped.
9. **`AudioSource` reflection round-trip** — every field survives `to_value` →
   `from_value`.
10. **Builtin registration** — `"AudioSource"` and `"AudioListener"` are registered by
    `register_builtin_components` (mirrors the `ParticleEmitter` builtin test).

## Out of scope / follow-ups

- **Real device backend** — a `modules/audio_*` implementing `AudioBackend`
  (miniaudio is a strong candidate: single-header, permissive, cross-platform), plus
  a cooked audio-clip asset type and `AssetRegistry` loading; then wire `AudioSystem`
  into `setup_runtime` and add a demo sound.
- Listener **orientation** from the Transform's rotation (needs a quat·vec rotate).
- Distance attenuation / rolloff model, doppler, per-source min/max distance.
- One-shot auto-cleanup by polling `is_playing`; voice stealing / max-voices cap.
- Script bindings (`play_sound(entity)` etc.) via the existing component path.
