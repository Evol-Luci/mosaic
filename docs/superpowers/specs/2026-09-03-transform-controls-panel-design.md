# Transform & Material Controls Panel — Design

## Problem

The Inspector edits every component field through generic reflection
(`ImGuiFieldVisitor::field_float` → `ImGui::DragFloat`). That means the
selected entity's **rotation** is presented as four raw quaternion drags
(x/y/z/w) — unusable — and its **color** (`Material.base_color`) as three
bare float drags with no picker. The user asked for "a visual slider panel
that allows the user to move the object's rotation, position, and colors and
see how these changes affect the scene."

## Goal

A dedicated **Controls** panel that gives the *selected* entity ergonomic
manipulators — position/scale drags, **Euler-angle rotation sliders (in
degrees)**, and a **color picker** — writing straight into its `Transform`
and `Material` components. It sits alongside (not replacing) the generic
Inspector.

## Approach

Follow the established studio pattern: **pure-logic model + thin ImGui glue.**

### Pure logic — `studio/transform_controls.hpp` (new, header-only)

The only real logic is Euler↔Quat conversion plus stable slider state.

```cpp
struct EulerAngles { float pitch = 0.0f; float yaw = 0.0f; float roll = 0.0f; }; // radians

// Intrinsic Y(yaw) * X(pitch) * Z(roll) composition.
Quat        euler_to_quat(const EulerAngles& e);
EulerAngles quat_to_euler(const Quat& q);

// Keeps the sliders stable while dragging: re-seed the working Euler from the
// stored quat ONLY when the selected entity changes, never every frame (which
// would fight the user / flip through gimbal singularities).
struct TransformControlsModel {
    EntityID entity{};
    EulerAngles euler{};
    bool active = false;
    void sync(EntityID e, const Quat& stored); // reseeds euler iff e != entity (or !active)
};
```

- `euler_to_quat` composes `from_axis_angle(Y,yaw) * from_axis_angle(X,pitch) * from_axis_angle(Z,roll)` using the existing `quat.hpp` primitives.
- `quat_to_euler` is the matching extraction (YXZ order), clamping the pitch
  asin argument to [-1,1] for numerical safety.

### GUI glue — `StudioApp::draw_controls(World&)` in `studio_app.cpp`

```
selected = selection_.selected();  if none or no Transform -> "(no selection)"; return false
Transform& t = world.get_component<Transform>(selected);
controls_.sync(selected, t.rotation);         // reseed on selection change
DragFloat3 "Position" -> t.position
SliderAngle x3 "Pitch/Yaw/Roll" -> controls_.euler ; on change t.rotation = euler_to_quat(euler)
DragFloat3 "Scale" -> t.scale
if world.has_component<Material>: ColorEdit3 "Base Color" -> material.base_color
return changed;  // caller marks scene dirty
```

`ImGui::SliderAngle` stores radians and displays degrees — exactly the
Euler-in-degrees UX requested, no manual conversion.

### Wiring

- `StudioLayout` default panel list gains `{"Controls", true}`.
- Panel dispatch chain in `draw_ui` gains an `else if (panel.name == "Controls")`
  branch that calls `draw_controls(world)` and marks the scene dirty on change
  (same shape as the Inspector branch).
- `StudioApp` gains a `TransformControlsModel controls_;` member and a
  `bool draw_controls(World&)` method.

## Why a separate panel (not enhancing the Inspector)

The generic `ImGuiFieldVisitor` is type-erased and field-by-field; teaching it
to recognise "these three floats are a color" or "this quat is a rotation"
means a semantic reflection refactor. A focused manipulator that keys off the
known `Transform`/`Material` types is smaller, matches the user's literal
"slider panel" request, and leaves the reflection Inspector untouched.

## Testing (headless)

`tests/unit/studio/transform_controls_test.cpp`:
- **Round-trip:** `quat_to_euler(euler_to_quat(e)) ≈ e` for several non-singular angle sets (tolerance ~1e-4).
- **Composition:** `euler_to_quat({0, π/2, 0})` equals `from_axis_angle(Y, π/2)` component-wise (validates yaw axis/order without depending on a rotate-vector helper).
- **Identity:** `euler_to_quat({0,0,0})` ≈ identity quat.
- **sync reseeds on entity change only:** first `sync(e1, q)` seeds euler from q; a second `sync(e1, q2)` with the *same* entity leaves euler untouched; `sync(e2, q2)` reseeds.

Pure math + state — no ImGui, fully headless.

## Out of scope (YAGNI)

- Undo/redo for these edits (Inspector edits aren't undoable either yet).
- Gizmo overlay in the viewport.
- Per-axis bounded slider ranges for position/scale (drags are fine).
