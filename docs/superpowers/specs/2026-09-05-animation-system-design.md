# Animation System (property-path keyframes) — Design

## Goal (Phase 4, subsystem 1)

Keyframe animation that drives **any reflected scalar property** over time via a
neutral `AnimationSystem` — the first Phase-4 subsystem, foundational for the
sequencer/VFX. Generic property-path targeting (maintainer-approved), v0.1:
linear + step interpolation, inline clips, loop + speed.

## Architecture

Everything is reflection/`Value`-driven (scripts already animate via
`t['position']['x']`), so a track targets a scalar by **property path** and the
system writes it through `ComponentReflection` — one system animates Transform,
Material color, light intensity, anything, with zero per-property code.

### 1. `engine/animation/animation_clip.hpp` (new)

```cpp
struct Keyframe { float time = 0.0f; double value = 0.0; };

struct Track
{
    std::string           target;         // "Transform.position.x", "Material.base_color.r"
    bool                  step = false;   // false = linear, true = hold (step)
    std::vector<Keyframe> keys;           // sorted by time (author's responsibility)
};

struct AnimationClip
{
    std::string        name;
    float              duration = 0.0f;
    std::vector<Track> tracks;
};

// reflect() on each (Keyframe/Track/AnimationClip) so a clip serializes with the scene.

// Sample a track at `time`: empty -> 0; clamp outside [first,last]; step -> left key's
// value; linear -> interpolate between the bracketing keys.
double sample(const Track& track, float time);
```

`bool step` (not an enum) avoids enum-reflection risk; `vector<struct>` and
nested reflected structs are confirmed to round-trip via `reflect_value`.

### 2. `engine/animation/animation_player.hpp` (new) — component

```cpp
struct AnimationPlayer
{
    AnimationClip clip;
    float         time    = 0.0f;
    bool          playing = true;
    bool          loop    = true;
    float         speed   = 1.0f;
};
// reflect: clip, time, playing, loop, speed
```

Registered in `register_builtin_components` as `"AnimationPlayer"` — serializes
with the scene and appears in the Inspector.

### 3. Property-path write (in `animation_system.hpp` or a small helper)

```cpp
// Set the scalar leaf at a dotted path within a component Value table. false if any
// segment is missing or not a table/leaf. e.g. path {"position","x"} on a Transform value.
bool set_scalar_at(Value& root, const std::vector<std::string>& path, double v);
```
Target parsing: split `"Transform.position.x"` on `.` → component `"Transform"`,
path `["position","x"]`.

### 4. `engine/animation/animation_system.hpp` (new)

`AnimationSystem(const ComponentReflection& types)`. Each `update(world, dt)`:
for every entity with an `AnimationPlayer` that is `playing`:
1. `time += dt * speed`; if `loop && duration > 0`, wrap `time = fmod(time, duration)`.
2. For each track: split the target; `component_value(world, e, comp)` → read the
   `Value`; `set_scalar_at(value, path, sample(track, time))`; `set_value(world, e, comp, value)`.

Per-track read/write (correct; grouping by component is a later optimization).

### 5. Wiring — `app/run/demo_scene.hpp`

`setup_runtime` adds `AnimationSystem` **after** `ScriptSystem`, **before**
`PhysicsSystem` (animation feeds transforms; physics write-back still wins on a
shared body). So `mosaic run` / the editor play animations.

## Testing (headless)

1. **`sample`** (`animation_clip_test`): empty→0; clamp before-first / after-last;
   linear midpoint; step holds left value; exact-key value.
2. **Reflection round-trip** (`animation_player_test`): an `AnimationPlayer` with
   a 2-track clip survives `to_value`/`from_value` (nested tracks/keys intact);
   registered as a builtin component.
3. **`set_scalar_at`**: sets `position.x` in a Transform-shaped `Value`; false on
   a bad path.
4. **`AnimationSystem`** (`animation_system_test`): an entity with a Transform +
   an `AnimationPlayer` animating `Transform.position.x` linearly 0→10 over
   duration 1; after ticking to t=0.5, `x ≈ 5`; loop wraps; `playing=false` freezes.
5. **Integration** (`script_runtime`/anim): `setup_runtime` + an animated entity →
   ticks move it (proves the system is wired).

## Out of scope / follow-ups

- Clip-as-asset (`AssetID` import, like `.py`/textures).
- Cubic/bezier easing; per-keyframe interpolation modes.
- Skeletal animation, blend trees, animation events, the **sequencer** (drives
  clips on a timeline — the natural next Phase-4 slice).
