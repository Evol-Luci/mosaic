# Sequencer (director timeline) — Design

## Goal (Phase 4, subsystem 2)

A director timeline that orchestrates animation across many entities on one
master clock, plus event markers that fire named script events — cutscenes and
gameplay timelines. Compounds on the animation slice (reuses `sample()` /
`set_scalar_at` / the clip write path). Maintainer-approved: **Director model**
(a Sequence owns clips) + **event tracks** included.

## Design

### 0. Shared clip-apply helper (`engine/animation/animation_system.hpp`)

Extract the per-clip write loop from `AnimationSystem` into a free function both
systems use (DRY — no duplicated reflection write path):

```cpp
inline void apply_clip_at(const reflection::ComponentReflection& types, World& world,
                          mosaic::foundation::EntityID e, const AnimationClip& clip, float time);
// for each track: split target -> component + path; component_value; set_scalar_at(sample); set_value.
```
`AnimationSystem::update` calls `apply_clip_at(types_, world, e, player.clip, player.time)`
(behavior unchanged — existing tests still pass).

### 1. `engine/sequencer/sequence.hpp` (new)

```cpp
// A clip bound to a target entity, starting at `start` on the master timeline.
struct SequenceClipTrack
{
    std::uint64_t            target = 0;   // target entity id
    float                    start  = 0.0f;
    animation::AnimationClip clip;
};

// A marker: emits a named script event once, when the timeline crosses `time`.
struct SequenceEventTrack
{
    float       time = 0.0f;
    std::string event;    // ScriptEvent name (empty payload)
};

// A director timeline on an entity. Drives clip tracks (any target entity) + fires markers.
struct Sequence
{
    float                            time     = 0.0f;
    float                            duration = 0.0f;
    bool                             playing  = true;
    bool                             loop     = false; // cutscenes usually play once
    std::vector<SequenceClipTrack>   clips;
    std::vector<SequenceEventTrack>  events;
};
// reflect() on each (nested AnimationClip already reflects) -> serializes with the scene.
```

Registered in `register_builtin_components` as `"Sequence"`.

### 2. `engine/sequencer/sequence_system.hpp` (new)

`SequenceSystem(const ComponentReflection& types)`. Each `update(world, dt)`, for
every entity with a `playing` `Sequence`:

1. `prev = time`; `time += dt`; if `loop && duration > 0 && time >= duration`,
   `time = fmod(time, duration)` (mark `wrapped`).
2. **Clip tracks:** for each `SequenceClipTrack`, `local = time - start`; if
   `0 <= local <= clip.duration`, `apply_clip_at(types, world, EntityID(target), clip, local)`
   — writes the *target* entity.
3. **Event markers:** fire each marker crossed this tick:
   - not wrapped: `prev < T <= time`
   - wrapped: `T > prev || T <= time` (covers `(prev, duration]` ∪ `[0, time]`)
   Firing = `world.events().emit(script::ScriptEvent{marker.event, Value{}})` →
   delivered to scripts' `on_event` one tick later (reuses the event bus).

Wired into `setup_runtime` after `AnimationSystem` (a marker's effect lands the
next tick regardless of order; clips are independent of animation players).

## Testing (headless)

1. **Reflection round-trip** (`sequence_test`): a `Sequence` with a clip track
   (bound target + start + nested clip) and an event marker survives
   `to_value`/`from_value`.
2. **`apply_clip_at`** (`animation` test): the extracted helper animates a
   Transform (guards `AnimationSystem` behavior is preserved).
3. **Director drives a target** (`sequence_system_test`): a director entity with
   a `Sequence` whose clip track targets *another* entity's
   `Transform.position.x` (start=0, clip 0→10 over 1s); after ticking to master
   t=0.5, the **target's** x ≈ 5. A track with `start=1.0` is inactive at t=0.5.
4. **Event marker fires** (`sequence_system_test`): a marker at t=0.5 emitting
   `'boom'`; a scripted entity's `on_event` records it; after crossing t=0.5 (+1
   tick for delivery), the script saw `'boom'` exactly once (doesn't re-fire while
   time stays past it, non-looping).

## Out of scope / follow-ups

- Auto-stop / clamp a non-looping sequence at `duration`; playhead scrubbing UI.
- Event payloads on markers; blending overlapping clips on one property.
- Sequence-as-asset; a timeline editor panel.
