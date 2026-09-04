# Transform & Material Controls Panel — Implementation Plan

> **For agentic workers:** implement task-by-task, TDD, commit after each task.

**Goal:** Add a studio "Controls" panel giving the selected entity ergonomic
position/scale drags, Euler-angle rotation sliders (degrees), and a color
picker, writing into its `Transform`/`Material`.

**Architecture:** Pure-logic `transform_controls.hpp` (Euler↔Quat + stable
slider state) + thin ImGui glue `StudioApp::draw_controls`. Follows the
existing studio model+glue pattern. Reflection Inspector untouched.

**Tech Stack:** C++20, Catch2, ImGui, existing `foundation/math` quat/mat4.

**Spec:** `docs/superpowers/specs/2026-09-03-transform-controls-panel-design.md`

## Global Constraints

- Tier wall: `foundation/` → `engine/` → `modules/` → `studio/`. New code lives
  in `studio/`; it may include `engine/` + `foundation/` only.
- Header-only studio models (match `pick.hpp`, `play_state.hpp`).
- Catch2 `TEST_CASE` names must not start with `--` (breaks ctest discovery).
- No new dependencies.

---

### Task 1: Euler↔Quat conversion + stable slider state (pure logic)

**Files:**
- Create: `studio/transform_controls.hpp`
- Test: `tests/unit/studio/transform_controls_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source under the studio block, ~line 91 after `unit/studio/pick_test.cpp`)

**Interfaces produced (later task consumes):**
- `mosaic::studio::EulerAngles { float pitch, yaw, roll; }` (radians)
- `mosaic::studio::Quat euler_to_quat(const EulerAngles&)`
- `mosaic::studio::EulerAngles quat_to_euler(const Quat&)`
- `struct TransformControlsModel { foundation::EntityID entity{}; EulerAngles euler{}; bool active=false; void sync(EntityID, const Quat& stored); }`

Use `mosaic::foundation::math::{Quat, Vec3, from_axis_angle, normalized, operator*}`.

- [ ] **Step 1: Write the failing test** `tests/unit/studio/transform_controls_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "studio/transform_controls.hpp"

using namespace mosaic::studio;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::Vec3;

namespace
{
constexpr float kPi = 3.14159265358979323846f;
void approx_quat(const Quat& a, const Quat& b, float eps = 1e-4f)
{
    // Quats double-cover: q and -q are the same rotation.
    const float s = (a.w * b.w < 0.0f) ? -1.0f : 1.0f;
    REQUIRE_THAT(a.x, Catch::Matchers::WithinAbs(s * b.x, eps));
    REQUIRE_THAT(a.y, Catch::Matchers::WithinAbs(s * b.y, eps));
    REQUIRE_THAT(a.z, Catch::Matchers::WithinAbs(s * b.z, eps));
    REQUIRE_THAT(a.w, Catch::Matchers::WithinAbs(s * b.w, eps));
}
} // namespace

TEST_CASE("euler_to_quat of zero is identity")
{
    approx_quat(euler_to_quat(EulerAngles{0.0f, 0.0f, 0.0f}), Quat::identity());
}

TEST_CASE("euler_to_quat yaw matches Y axis-angle")
{
    approx_quat(euler_to_quat(EulerAngles{0.0f, kPi * 0.5f, 0.0f}),
                from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi * 0.5f));
}

TEST_CASE("euler/quat round trips for non-singular angles")
{
    for (const EulerAngles e : {EulerAngles{0.3f, -0.7f, 1.1f},
                                EulerAngles{-0.5f, 0.9f, -0.2f}})
    {
        const EulerAngles r = quat_to_euler(euler_to_quat(e));
        REQUIRE_THAT(r.pitch, Catch::Matchers::WithinAbs(e.pitch, 1e-4f));
        REQUIRE_THAT(r.yaw, Catch::Matchers::WithinAbs(e.yaw, 1e-4f));
        REQUIRE_THAT(r.roll, Catch::Matchers::WithinAbs(e.roll, 1e-4f));
    }
}

TEST_CASE("controls model reseeds euler only when entity changes")
{
    using mosaic::foundation::EntityID;
    TransformControlsModel m;
    const EntityID e1{1};
    const EntityID e2{2};

    m.sync(e1, euler_to_quat(EulerAngles{0.2f, 0.0f, 0.0f}));
    REQUIRE_THAT(m.euler.pitch, Catch::Matchers::WithinAbs(0.2f, 1e-4f));

    // Same entity, different stored quat: euler must NOT be clobbered mid-edit.
    m.euler.pitch = 0.9f;
    m.sync(e1, euler_to_quat(EulerAngles{0.0f, 0.0f, 0.0f}));
    REQUIRE_THAT(m.euler.pitch, Catch::Matchers::WithinAbs(0.9f, 1e-4f));

    // New entity: reseed from its stored quat.
    m.sync(e2, euler_to_quat(EulerAngles{-0.4f, 0.0f, 0.0f}));
    REQUIRE_THAT(m.euler.pitch, Catch::Matchers::WithinAbs(-0.4f, 1e-4f));
}
```

Add to `tests/CMakeLists.txt` after the `unit/studio/pick_test.cpp` line:
```
  unit/studio/transform_controls_test.cpp
```

- [ ] **Step 2: Run to verify it fails** (compile error: no such header). `ctest --test-dir build/debug -R transform_controls`

- [ ] **Step 3: Implement** `studio/transform_controls.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <cmath>

#include "foundation/core/entity_id.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::studio
{

using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

struct EulerAngles
{
    float pitch = 0.0f; // rotation about X, radians
    float yaw   = 0.0f; // rotation about Y, radians
    float roll  = 0.0f; // rotation about Z, radians
};

// Intrinsic composition Ry(yaw) * Rx(pitch) * Rz(roll).
inline Quat euler_to_quat(const EulerAngles& e)
{
    using mosaic::foundation::math::from_axis_angle;
    const Quat qy = from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, e.yaw);
    const Quat qx = from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, e.pitch);
    const Quat qz = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, e.roll);
    return normalized(qy * qx * qz);
}

// Inverse of euler_to_quat (YXZ order). Derived from the rotation matrix of q.
inline EulerAngles quat_to_euler(const Quat& qin)
{
    const Quat q = normalized(qin);
    const float x = q.x, y = q.y, z = q.z, w = q.w;

    // Rotation-matrix element m[1][2] = 2(yz - wx) => pitch = asin(-m12) form.
    // For Ry*Rx*Rz, pitch = asin(clamp(2*(w*x + y*z))).
    const float sin_pitch = std::clamp(2.0f * (w * x + y * z), -1.0f, 1.0f);
    EulerAngles e;
    e.pitch = std::asin(sin_pitch);

    const float cos_pitch = std::cos(e.pitch);
    if (std::abs(cos_pitch) > 1e-4f)
    {
        e.yaw  = std::atan2(2.0f * (w * y - x * z), 1.0f - 2.0f * (x * x + y * y));
        e.roll = std::atan2(2.0f * (w * z - x * y), 1.0f - 2.0f * (x * x + z * z));
    }
    else
    {
        // Gimbal lock: fold roll into yaw.
        e.yaw  = std::atan2(-2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z));
        e.roll = 0.0f;
    }
    return e;
}

// Working slider state: reseed the Euler triple from the stored quat only when
// the selected entity changes, so dragging one slider doesn't get overwritten.
struct TransformControlsModel
{
    mosaic::foundation::EntityID entity{};
    EulerAngles                  euler{};
    bool                         active = false;

    void sync(mosaic::foundation::EntityID e, const Quat& stored)
    {
        if (!active || e != entity)
        {
            entity = e;
            euler  = quat_to_euler(stored);
            active = true;
        }
    }
};

} // namespace mosaic::studio
```

> NOTE for implementer: the exact `quat_to_euler` element signs must satisfy
> the round-trip + yaw tests above. If a sign is off, derive the extraction
> from `Mat4::rotation(q)` (foundation/math/mat4.hpp) for the YXZ order rather
> than guessing — the tests are the oracle. Verify `EntityID` supports `!=`
> (it does — used elsewhere in studio); if only `==` exists, use `!(e == entity)`.

- [ ] **Step 4: Run to verify pass** `ctest --test-dir build/debug -R transform_controls` → all pass.

- [ ] **Step 5: Commit** `git add studio/transform_controls.hpp tests/unit/studio/transform_controls_test.cpp tests/CMakeLists.txt && git commit -m "feat(studio): Euler<->Quat conversion + stable controls slider state"`

---

### Task 2: Controls panel GUI + wiring

**Files:**
- Modify: `studio/studio_layout.cpp` (add `{"Controls", true}` to the default panel list)
- Modify: `studio/studio_app.hpp` (include `transform_controls.hpp`; add `bool draw_controls(World&)` decl + `TransformControlsModel controls_;` member)
- Modify: `studio/studio_app.cpp` (dispatch branch + `draw_controls` impl)

**Interfaces consumed:** Task 1's `euler_to_quat`, `TransformControlsModel`.

This task is ImGui glue (no headless unit test — same as other panel draws).
Verification is: builds clean, full suite still green, `mosaic run --window` launches (exit 0).

- [ ] **Step 1:** In `studio/studio_layout.cpp`, add `{"Controls", true},` to the `panels_` initializer (after `{"Inspector", true},`).

- [ ] **Step 2:** In `studio/studio_app.hpp`:
  - add `#include "studio/transform_controls.hpp"`
  - add include for the transform + material components if not already pulled in transitively: `#include "engine/transform/transform.hpp"` and `#include "engine/render/material.hpp"`
  - declare in the private methods block: `bool draw_controls(mosaic::engine::World& world);`
  - add member near `selection_`: `TransformControlsModel controls_;`

- [ ] **Step 3:** In `studio/studio_app.cpp` dispatch chain (after the `Inspector` branch, before `Assets`), add:

```cpp
            else if (panel.name == "Controls")
            {
                if (draw_controls(world))
                {
                    scene_file_.mark_dirty();
                }
            }
```

- [ ] **Step 4:** In `studio/studio_app.cpp`, implement `draw_controls` (place near `draw_inspector`). Use the actual namespaces:

```cpp
bool StudioApp::draw_controls(mosaic::engine::World& world)
{
    using mosaic::engine::render::Material;
    using mosaic::engine::transform::Transform;

    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(no selection)");
        return false;
    }
    const mosaic::foundation::EntityID e = selection_.selected();
    if (!world.has_component<Transform>(e))
    {
        ImGui::TextUnformatted("(selection has no Transform)");
        return false;
    }

    bool       changed = false;
    Transform& t       = world.get_component<Transform>(e);
    controls_.sync(e, t.rotation);

    if (ImGui::DragFloat3("Position", &t.position.x, 0.05f)) { changed = true; }

    bool rot = false;
    rot |= ImGui::SliderAngle("Pitch (X)", &controls_.euler.pitch);
    rot |= ImGui::SliderAngle("Yaw (Y)", &controls_.euler.yaw);
    rot |= ImGui::SliderAngle("Roll (Z)", &controls_.euler.roll);
    if (rot)
    {
        t.rotation = euler_to_quat(controls_.euler);
        changed    = true;
    }

    if (ImGui::DragFloat3("Scale", &t.scale.x, 0.05f)) { changed = true; }

    if (world.has_component<Material>(e))
    {
        Material& m = world.get_component<Material>(e);
        if (ImGui::ColorEdit3("Base Color", &m.base_color.x)) { changed = true; }
    }

    return changed;
}
```

> NOTE: confirm `Vec3` exposes `.x/.y/.z` contiguously (it does) so `&v.x` is a
> valid `float[3]` for the ImGui `*3` widgets. Confirm `World::get_component<T>`
> returns a mutable `T&` (it does — Inspector writes through it).

- [ ] **Step 5: Build** `cmake --build build/debug -j` → clean.

- [ ] **Step 6: Run full suite** `ctest --test-dir build/debug` → all pass (previous count + 4 new from Task 1).

- [ ] **Step 7: Commit** `git add studio/studio_layout.cpp studio/studio_app.hpp studio/studio_app.cpp && git commit -m "feat(studio): Controls panel — Euler rotation sliders, position/scale drags, color picker"`

---

### Task 3: next-steps doc update (rides in this branch)

**Files:** Modify `docs/project/next-steps.md`.

- [ ] Mark the Editor-UX "controls panel (sliders/color-picker/Euler-rotation)" item DONE; reconcile the Editor-UX menu bullet so add/remove, mouse-picking, and this controls panel are all shown complete, leaving File→Reload as the remaining Editor-UX item. Commit: `docs: mark controls panel done in next-steps`.
