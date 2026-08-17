# Foundation Math + Transform System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build minimal `foundation/math` (`Vec3`/`Quat`/`Mat4`) and the first concrete `System` — a reflected `Transform` component and a `TransformSystem` that computes world transforms from local + parent chain during `World::tick`.

**Architecture:** Header-only `foundation/math` (column-major, right-handed, Y-up, radians) with only the ops transform needs. A reflected `Transform` (authored local: position/rotation/scale/parent) and a derived `WorldTransform{ Mat4 }`. `TransformSystem::update` walks each entity's parent chain (`world = parent * local`, `local = T*R*S`) and writes its `WorldTransform`.

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (via FetchContent).

**Design spec:** [docs/superpowers/specs/2026-08-16-transform-system-design.md](../specs/2026-08-16-transform-system-design.md)

## Global Constraints

- **C++20.** `target_compile_features(... cxx_std_20)`; no compiler extensions.
- **Tier/dependency contract:** `foundation/` knows nothing above it; `engine/` depends only on `foundation/` and other `engine/` code. `foundation/math` is header-only, standard-library only, hand-rolled (no GLM).
- **Math convention:** column-major storage (`element(row,col) = m[col*4+row]`), column vectors, right-handed, Y-up, radians. `world = parent * local`, `local = T * R * S`.
- **Namespaces:** math is `mosaic::foundation::math`; transform is `mosaic::engine::transform`.
- **Naming note:** `identity()` is a **static member** (`Quat::identity()`, `Mat4::identity()`) and the `Mat4` builders (`translation`/`scale`/`rotation`/`trs`) are **static members** — the spec sketched some as free functions, but two free `identity()` overloads differing only in return type are illegal, so static members resolve it cleanly. `Quat` and `Mat4` stay aggregates (static member functions don't disqualify aggregate init).
- **Style (match existing files):** 4-space indent, Allman braces, `snake_case` functions, `PascalCase` types, trailing-underscore private members.
- **Testing:** Catch2, unit tests under `tests/unit/math/` and `tests/unit/transform/`, tags `[math]` / `[transform]`; register each new `.cpp` in `tests/CMakeLists.txt`. Float comparisons use `Catch::Approx` (`#include <catch2/catch_approx.hpp>`), with `.margin(1e-5)` for near-zero. TDD — write the failing test first.
- **Build/test commands:**
  - Build: `cmake --build --preset debug`
  - Run a subset: `./build/debug/tests/mosaic_unit_tests "[math]"` / `"[transform]"`
  - Full suite: `ctest --preset debug`
- **Commit** after each task (this branch is `claude/practical-williams-bde237`).

## File Structure

| File | Responsibility |
|------|----------------|
| `foundation/math/vec3.hpp` (create) | `Vec3` + arithmetic + `reflect`. |
| `foundation/math/quat.hpp` (create) | `Quat` + `identity`/`from_axis_angle`/`normalized`/compose + `reflect`. |
| `foundation/math/mat4.hpp` (create) | `Mat4` (column-major) + `identity`/`*`/`translation`/`scale`/`rotation`/`trs`/`transform_point`. |
| `engine/transform/transform.hpp` (create) | `Transform` (reflected) + `WorldTransform`. |
| `engine/transform/transform_system.hpp` (create) | `TransformSystem : System`. Header-only. |
| `tests/unit/math/vec3_test.cpp` (create) | Task 1 test. |
| `tests/unit/math/quat_test.cpp` (create) | Task 2 test. |
| `tests/unit/math/mat4_test.cpp` (create) | Task 3 test. |
| `tests/unit/transform/transform_reflection_test.cpp` (create) | Task 4 test. |
| `tests/unit/transform/transform_system_test.cpp` (create) | Task 5 test. |
| `tests/CMakeLists.txt` (modify) | Register the five new test sources. |

No `CMakeLists.txt` library changes: all new code is header-only; tests link existing libs.

---

### Task 1: `Vec3`

**Files:**
- Create: `foundation/math/vec3.hpp`
- Test: `tests/unit/math/vec3_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct mosaic::foundation::math::Vec3 { float x, y, z; }`; `operator+`, `operator-`, `operator*` (`Vec3*float`, `float*Vec3`), `operator==`/`!=`; `template<class V> void reflect(V&, Vec3&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/math/vec3_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Vec3;

TEST_CASE("Vec3 default is zero", "[math]")
{
    Vec3 v;
    REQUIRE(v == Vec3{0.0f, 0.0f, 0.0f});
}

TEST_CASE("Vec3 arithmetic", "[math]")
{
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};

    REQUIRE((a + b) == Vec3{5.0f, 7.0f, 9.0f});
    REQUIRE((b - a) == Vec3{3.0f, 3.0f, 3.0f});
    REQUIRE((a * 2.0f) == Vec3{2.0f, 4.0f, 6.0f});
    REQUIRE((2.0f * a) == Vec3{2.0f, 4.0f, 6.0f});
    REQUIRE(a != b);
}
```

Register in `tests/CMakeLists.txt` — add inside the `add_executable(mosaic_unit_tests ...)` source list, after `unit/assets/asset_resolver_test.cpp`:

```cmake
  unit/math/vec3_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: foundation/math/vec3.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/math/vec3.hpp`:

```cpp
#pragma once

namespace mosaic::foundation::math
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }

template <class V>
void reflect(V& visitor, Vec3& v)
{
    visitor.member("x", v.x);
    visitor.member("y", v.y);
    visitor.member("z", v.z);
}

} // namespace mosaic::foundation::math
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[math]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add foundation/math/vec3.hpp tests/unit/math/vec3_test.cpp tests/CMakeLists.txt
git commit -m "Add foundation math Vec3 (#21, #26)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `Quat`

**Files:**
- Create: `foundation/math/quat.hpp`
- Test: `tests/unit/math/quat_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Vec3` (Task 1).
- Produces: `struct mosaic::foundation::math::Quat { float x, y, z, w; static Quat identity(); }`; `Quat from_axis_angle(const Vec3&, float)`, `Quat normalized(const Quat&)`, `Quat operator*(const Quat&, const Quat&)`; `template<class V> void reflect(V&, Quat&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/math/quat_test.cpp`:

```cpp
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::normalized;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

TEST_CASE("Quat default and identity are (0,0,0,1)", "[math]")
{
    const Quat def;
    REQUIRE(def.x == 0.0f);
    REQUIRE(def.w == 1.0f);
    REQUIRE(Quat::identity().w == 1.0f);
}

TEST_CASE("from_axis_angle about Z by 90 degrees", "[math]")
{
    const float pi = 3.14159265358979323846f;
    const Quat q = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, pi / 2.0f);
    REQUIRE(q.z == Approx(std::sin(pi / 4.0f)));
    REQUIRE(q.w == Approx(std::cos(pi / 4.0f)));
}

TEST_CASE("normalized yields unit length", "[math]")
{
    const Quat q = normalized(Quat{1.0f, 2.0f, 3.0f, 4.0f});
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    REQUIRE(len == Approx(1.0f));
}

TEST_CASE("identity compose leaves a quaternion unchanged", "[math]")
{
    const Quat q = from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, 1.0f);
    const Quat r = Quat::identity() * q;
    REQUIRE(r.x == Approx(q.x));
    REQUIRE(r.y == Approx(q.y));
    REQUIRE(r.z == Approx(q.z));
    REQUIRE(r.w == Approx(q.w));
}
```

Register in `tests/CMakeLists.txt`, after the Task 1 line:

```cmake
  unit/math/quat_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: foundation/math/quat.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/math/quat.hpp`:

```cpp
#pragma once

#include <cmath>

#include "foundation/math/vec3.hpp"

namespace mosaic::foundation::math
{

struct Quat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static Quat identity() { return Quat{0.0f, 0.0f, 0.0f, 1.0f}; }
};

inline Quat normalized(const Quat& q)
{
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len == 0.0f)
    {
        return Quat::identity();
    }
    const float inv = 1.0f / len;
    return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Quat from_axis_angle(const Vec3& axis, float radians)
{
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len == 0.0f)
    {
        return Quat::identity();
    }
    const float inv = 1.0f / len;
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    return Quat{axis.x * inv * s, axis.y * inv * s, axis.z * inv * s, std::cos(half)};
}

// Hamilton product: (a * b) applies b then a.
inline Quat operator*(const Quat& a, const Quat& b)
{
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

template <class V>
void reflect(V& visitor, Quat& q)
{
    visitor.member("x", q.x);
    visitor.member("y", q.y);
    visitor.member("z", q.z);
    visitor.member("w", q.w);
}

} // namespace mosaic::foundation::math
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[math]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add foundation/math/quat.hpp tests/unit/math/quat_test.cpp tests/CMakeLists.txt
git commit -m "Add foundation math Quat (#21, #26)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `Mat4`

**Files:**
- Create: `foundation/math/mat4.hpp`
- Test: `tests/unit/math/mat4_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Vec3` (Task 1), `Quat` (Task 2).
- Produces: `struct mosaic::foundation::math::Mat4 { std::array<float,16> m; static Mat4 identity()/translation(Vec3)/scale(Vec3)/rotation(Quat)/trs(Vec3,Quat,Vec3); }`; `Mat4 operator*(const Mat4&, const Mat4&)`; `Vec3 transform_point(const Mat4&, const Vec3&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/math/mat4_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::transform_point;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

TEST_CASE("Mat4 identity is neutral under multiply", "[math]")
{
    const Mat4 t = Mat4::translation(Vec3{1.0f, 2.0f, 3.0f});
    const Mat4 r = Mat4::identity() * t;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(r.m[i] == Approx(t.m[i]));
    }
}

TEST_CASE("translation column holds the offset", "[math]")
{
    const Mat4 t = Mat4::translation(Vec3{4.0f, 5.0f, 6.0f});
    REQUIRE(t.m[12] == Approx(4.0f));
    REQUIRE(t.m[13] == Approx(5.0f));
    REQUIRE(t.m[14] == Approx(6.0f));
}

TEST_CASE("transform_point of the origin equals the translation", "[math]")
{
    const Mat4 t = Mat4::translation(Vec3{7.0f, 8.0f, 9.0f});
    const Vec3 p = transform_point(t, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(7.0f));
    REQUIRE(p.y == Approx(8.0f));
    REQUIRE(p.z == Approx(9.0f));
}

TEST_CASE("trs applies scale, then rotation, then translation", "[math]")
{
    const float pi = 3.14159265358979323846f;
    const Quat rot = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, pi / 2.0f); // 90 deg about Z
    const Mat4 m = Mat4::trs(Vec3{10.0f, 0.0f, 0.0f}, rot, Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 p = transform_point(m, Vec3{1.0f, 0.0f, 0.0f}); // (1,0,0) -> (0,1,0), + trans
    REQUIRE(p.x == Approx(10.0f).margin(1e-5));
    REQUIRE(p.y == Approx(1.0f).margin(1e-5));
    REQUIRE(p.z == Approx(0.0f).margin(1e-5));
}
```

Register in `tests/CMakeLists.txt`, after the Task 2 line:

```cmake
  unit/math/mat4_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: foundation/math/mat4.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/math/mat4.hpp`:

```cpp
#pragma once

#include <array>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::foundation::math
{

// Column-major 4x4: element(row, col) = m[col * 4 + row].
struct Mat4
{
    std::array<float, 16> m{};

    static Mat4 identity()
    {
        Mat4 out;
        out.m[0] = 1.0f;
        out.m[5] = 1.0f;
        out.m[10] = 1.0f;
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 translation(const Vec3& t)
    {
        Mat4 out = identity();
        out.m[12] = t.x;
        out.m[13] = t.y;
        out.m[14] = t.z;
        return out;
    }

    static Mat4 scale(const Vec3& s)
    {
        Mat4 out;
        out.m[0] = s.x;
        out.m[5] = s.y;
        out.m[10] = s.z;
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 rotation(const Quat& q)
    {
        const float x = q.x;
        const float y = q.y;
        const float z = q.z;
        const float w = q.w;
        Mat4 out;
        out.m[0] = 1.0f - 2.0f * (y * y + z * z);
        out.m[1] = 2.0f * (x * y + w * z);
        out.m[2] = 2.0f * (x * z - w * y);
        out.m[3] = 0.0f;
        out.m[4] = 2.0f * (x * y - w * z);
        out.m[5] = 1.0f - 2.0f * (x * x + z * z);
        out.m[6] = 2.0f * (y * z + w * x);
        out.m[7] = 0.0f;
        out.m[8] = 2.0f * (x * z + w * y);
        out.m[9] = 2.0f * (y * z - w * x);
        out.m[10] = 1.0f - 2.0f * (x * x + y * y);
        out.m[11] = 0.0f;
        out.m[15] = 1.0f;
        return out;
    }

    // Defined out-of-line below (needs operator*).
    static Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s);
};

inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
    Mat4 out;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

inline Mat4 Mat4::trs(const Vec3& t, const Quat& r, const Vec3& s)
{
    return translation(t) * rotation(r) * scale(s);
}

inline Vec3 transform_point(const Mat4& m, const Vec3& p)
{
    return Vec3{
        m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

} // namespace mosaic::foundation::math
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[math]"`
Expected: PASS (all `[math]` cases).

- [ ] **Step 5: Commit**

```bash
git add foundation/math/mat4.hpp tests/unit/math/mat4_test.cpp tests/CMakeLists.txt
git commit -m "Add foundation math Mat4 (column-major) (#21, #26)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `Transform` + `WorldTransform` (reflected)

**Files:**
- Create: `engine/transform/transform.hpp`
- Test: `tests/unit/transform/transform_reflection_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Vec3`/`Quat`/`Mat4` (Tasks 1–3), `EntityID`, and (in the test) `ComponentReflection`/`World`.
- Produces:
  - `struct mosaic::engine::transform::Transform { math::Vec3 position; math::Quat rotation; math::Vec3 scale{1,1,1}; foundation::EntityID parent; }` + `reflect`.
  - `struct mosaic::engine::transform::WorldTransform { math::Mat4 matrix; }` (not reflected).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/transform/transform_reflection_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;
using EntityID = World::EntityID;

TEST_CASE("Transform reflects and generically constructs", "[transform]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<Transform>(registry, "Transform");

    World world;
    const EntityID e = world.create_entity();

    Transform t;
    t.position = Vec3{1.0f, 2.0f, 3.0f};
    t.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    t.scale = Vec3{2.0f, 2.0f, 2.0f};

    const Value value = to_value(t);
    REQUIRE(types.construct(world, e, "Transform", value));

    const Transform* got = world.get_component<Transform>(e);
    REQUIRE(got != nullptr);
    REQUIRE(got->position == Vec3{1.0f, 2.0f, 3.0f});
    REQUIRE(got->scale == Vec3{2.0f, 2.0f, 2.0f});
    REQUIRE(got->rotation.w == Approx(1.0f));
}
```

Register in `tests/CMakeLists.txt`, after the Task 3 line:

```cmake
  unit/transform/transform_reflection_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: engine/transform/transform.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/transform/transform.hpp`:

```cpp
#pragma once

#include "foundation/core/entity_id.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::transform
{

// Authored local transform. Reflected, so it serializes and constructs generically
// (#18/#19); `parent` (nil = root) remaps through the scene resolver like any
// EntityID.
struct Transform
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Quat rotation;
    mosaic::foundation::math::Vec3 scale{1.0f, 1.0f, 1.0f};
    mosaic::foundation::EntityID parent;
};

template <class V>
void reflect(V& visitor, Transform& t)
{
    visitor.member("position", t.position);
    visitor.member("rotation", t.rotation);
    visitor.member("scale", t.scale);
    visitor.member("parent", t.parent);
}

// Derived world transform, written by TransformSystem. Not reflected (never
// serialized — recomputed each tick).
struct WorldTransform
{
    mosaic::foundation::math::Mat4 matrix;
};

} // namespace mosaic::engine::transform
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[transform]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/transform/transform.hpp tests/unit/transform/transform_reflection_test.cpp tests/CMakeLists.txt
git commit -m "Add reflected Transform + WorldTransform components (#21)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: `TransformSystem`

**Files:**
- Create: `engine/transform/transform_system.hpp`
- Test: `tests/unit/transform/transform_system_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Transform`/`WorldTransform` (Task 4), `Mat4` (Task 3), `World` (`for_each`/`get_component`/`add_component`/`is_valid`), `ecs::System`.
- Produces: `class mosaic::engine::transform::TransformSystem : public ecs::System` with `TransformSystem()` (declares `reads<Transform>().writes<WorldTransform>()`) and `void update(World&, float) override`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/transform/transform_system_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::transform_point;
using mosaic::foundation::math::Vec3;
using EntityID = World::EntityID;

TEST_CASE("single entity local transform becomes world transform", "[transform]")
{
    World world;
    world.add_system<TransformSystem>();
    const EntityID e = world.create_entity();
    Transform t;
    t.position = Vec3{1.0f, 2.0f, 3.0f};
    world.add_component<Transform>(e, t);

    world.tick(0.0f);

    const WorldTransform* wt = world.get_component<WorldTransform>(e);
    REQUIRE(wt != nullptr);
    const Vec3 p = transform_point(wt->matrix, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(1.0f));
    REQUIRE(p.y == Approx(2.0f));
    REQUIRE(p.z == Approx(3.0f));
}

TEST_CASE("child world transform composes with its parent", "[transform]")
{
    World world;
    world.add_system<TransformSystem>();
    const EntityID parent = world.create_entity();
    Transform pt;
    pt.position = Vec3{10.0f, 0.0f, 0.0f};
    world.add_component<Transform>(parent, pt);

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{1.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    world.tick(0.0f);

    const WorldTransform* cw = world.get_component<WorldTransform>(child);
    REQUIRE(cw != nullptr);
    const Vec3 p = transform_point(cw->matrix, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(11.0f));
    REQUIRE(p.y == Approx(0.0f).margin(1e-5));
    REQUIRE(p.z == Approx(0.0f).margin(1e-5));
}

TEST_CASE("a rotated parent rotates the child offset", "[transform]")
{
    const float pi = 3.14159265358979323846f;
    World world;
    world.add_system<TransformSystem>();
    const EntityID parent = world.create_entity();
    Transform pt;
    pt.rotation = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, pi / 2.0f); // 90 deg about Z
    world.add_component<Transform>(parent, pt);

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{1.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    world.tick(0.0f);

    const Vec3 p = transform_point(world.get_component<WorldTransform>(child)->matrix,
                                   Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(0.0f).margin(1e-5));
    REQUIRE(p.y == Approx(1.0f).margin(1e-5));
    REQUIRE(p.z == Approx(0.0f).margin(1e-5));
}

TEST_CASE("moving a parent updates its children on the next tick", "[transform]")
{
    World world;
    world.add_system<TransformSystem>();
    const EntityID parent = world.create_entity();
    world.add_component<Transform>(parent, Transform{}); // origin

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{1.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    world.tick(0.0f);
    REQUIRE(transform_point(world.get_component<WorldTransform>(child)->matrix,
                            Vec3{0.0f, 0.0f, 0.0f})
                .x == Approx(1.0f));

    world.get_component<Transform>(parent)->position = Vec3{5.0f, 0.0f, 0.0f};
    world.tick(0.0f);
    REQUIRE(transform_point(world.get_component<WorldTransform>(child)->matrix,
                            Vec3{0.0f, 0.0f, 0.0f})
                .x == Approx(6.0f));
}
```

Register in `tests/CMakeLists.txt`, after the Task 4 line:

```cmake
  unit/transform/transform_system_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: engine/transform/transform_system.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/transform/transform_system.hpp`:

```cpp
#pragma once

#include "engine/ecs/system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/mat4.hpp"

namespace mosaic::engine::transform
{

// The first concrete System: composes each entity's world transform from its local
// TRS and its parent chain, writing a WorldTransform. Hierarchies must be acyclic
// (a depth guard bounds a malformed cycle).
class TransformSystem : public ecs::System
{
public:
    TransformSystem() { access_.reads<Transform>().writes<WorldTransform>(); }

    void update(World& world, float /*dt*/) override
    {
        world.for_each<Transform>([&](mosaic::foundation::EntityID e, Transform&)
        {
            const mosaic::foundation::math::Mat4 world_matrix = compute_world(world, e, 0);
            if (WorldTransform* existing = world.get_component<WorldTransform>(e))
            {
                existing->matrix = world_matrix;
            }
            else
            {
                world.add_component<WorldTransform>(e, WorldTransform{world_matrix});
            }
        });
    }

private:
    static constexpr int kMaxDepth = 256;

    static mosaic::foundation::math::Mat4 compute_world(World& world,
                                                        mosaic::foundation::EntityID e, int depth)
    {
        using mosaic::foundation::math::Mat4;
        Transform* t = world.get_component<Transform>(e);
        if (t == nullptr)
        {
            return Mat4::identity();
        }
        const Mat4 local = Mat4::trs(t->position, t->rotation, t->scale);
        if (depth < kMaxDepth && static_cast<bool>(t->parent) && world.is_valid(t->parent)
            && world.get_component<Transform>(t->parent) != nullptr)
        {
            return compute_world(world, t->parent, depth + 1) * local;
        }
        return local;
    }
};

} // namespace mosaic::engine::transform
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[transform]"`
Expected: PASS (all `[transform]` cases). Then the full suite:
Run: `ctest --preset debug`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/transform/transform_system.hpp tests/unit/transform/transform_system_test.cpp tests/CMakeLists.txt
git commit -m "Add TransformSystem: compose world transforms over World::tick (#21)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update next-steps**

In `docs/project/next-steps.md`:
- Add a "built and merged" paragraph for foundation math + the transform system (#21, #26) under *Where we are*: `foundation/math/{vec3,quat,mat4}.hpp` (column-major, right-handed, Y-up), the reflected `Transform` + derived `WorldTransform`, and `TransformSystem` (the first concrete `System`, driven by `World::tick`, composing world = parent * local). Note the conventions are now fixed for rendering/physics.
- Remove the "Foundation still owed for v0.1" **Math** bullet (now built); keep logging and jobs.
- In *What to build next*, make **Runtime loop (#22)** the suggested next target — the last engine piece of the vertical slice.
- Update the *Suggested sequence* diagram to mark `#21 transform system` done (✓).
- Bump **Last updated** to note transform system #21 built.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: foundation math + transform system built (#21)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `foundation/math` Vec3/Quat/Mat4 + ops → Tasks 1–3.
- `reflect` for Vec3/Quat (so Transform serializes) → Tasks 1–2, exercised in Task 4.
- Reflected `Transform` (position/rotation/scale/parent) → Task 4.
- Derived `WorldTransform` (separate component, not reflected) → Task 4.
- `TransformSystem` computes world = parent * local, driven by `World::tick` → Task 5.
- Hierarchy compose + moving-parent-updates-children → Task 5 tests.
- Column-major / right-handed / Y-up convention → Task 3 impl + Global Constraints.
- Depth guard for cycles, parent-missing → root → Task 5 impl.
- Out-of-scope (general linear algebra, memoization, GLM) → not built; recorded in spec + Task 6 handoff.
- Unit tests under `tests/unit/math/` + `tests/unit/transform/` → Tasks 1–5.

**Placeholder scan:** No TBD/TODO in production or test steps. Task 6 is prose documentation. The `identity()`-as-static-member resolution (naming conflict) is called out in Global Constraints and applied consistently in Tasks 2–3 and every test/consumer.

**Type consistency:** `Vec3`/`Quat`/`Mat4` and their ops (`operator+/-/*`, `Quat::identity`, `from_axis_angle`, `normalized`, `Mat4::identity/translation/scale/rotation/trs`, `transform_point`) are named identically in Tasks 1–3, their tests, and the transform consumers (Tasks 4–5). `Transform`/`WorldTransform` fields (Task 4) match `TransformSystem` (`t->position/rotation/scale/parent`, `WorldTransform{matrix}`) in Task 5 and the tests. `TransformSystem` uses `World::for_each`/`get_component`/`add_component`/`is_valid` and `System::access_` exactly as those exist in the codebase. `reflect(V&, T&)` overloads match the visitor `member(name, field)` contract used across the serialization bridge.
</content>
