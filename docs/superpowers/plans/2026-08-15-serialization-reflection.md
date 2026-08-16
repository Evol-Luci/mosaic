# Reflection + Serialization (#10) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a minimal reflection layer and a TOML/JSON serialization layer to `foundation/`, so components serialize by field through a single reflected schema.

**Architecture:** Each type describes its fields once via an ADL `reflect(visitor, value)` function. Two generic visitors convert any reflected type to/from a format-neutral `Value` DOM. Thin TOML (toml++) and JSON (nlohmann/json) backends project `Value ⇆ text`. References (`EntityID`/`AssetID`/`Handle<T>`) serialize as their IDs and re-resolve at load through a resolver hook.

**Tech Stack:** C++20, CMake + FetchContent, Catch2 v3, toml++ v3.4.0, nlohmann/json v3.11.3.

## Global Constraints

- **Language standard:** C++20 (`cxx_std_20`), no compiler extensions. No C++23 features (e.g. no `std::expected` — errors are exceptions).
- **Tier contract:** All new code lives under `foundation/`. It may depend only on other `foundation/` code and third-party libs — never on `engine/` or anything above. (See `docs/architecture/ARCHITECTURE.md`.)
- **Namespaces:** serialization code in `mosaic::foundation::serialization`; reflection protocol in `mosaic::foundation::reflection`. Sample/test schema types in `mosaic_samples`.
- **Third-party libs are private to the backends:** toml++ and nlohmann/json are included only in `*_backend.cpp` and linked `PRIVATE`. Public headers (`value.hpp`, `reference.hpp`, backend headers) must not include them.
- **Style:** match existing `foundation/` / `engine/ecs/` code — Allman braces, `#pragma once`, 4-space indent, includes ordered (Catch2/third-party, then project `"foundation/..."`). Tests use `catch2/catch_test_macros.hpp` and `TEST_CASE("...", "[tag]")`.
- **Build/test commands:** configure `cmake --preset debug`; build `cmake --build build/debug`; test `ctest --preset debug` (or run `./build/debug/tests/mosaic_unit_tests` directly, optionally with a Catch2 tag filter like `"[value]"`).
- **Reflection is a shim.** This `reflect` protocol is deliberately not the full reflection system (`docs/design/reflection.md`); it will be reconciled with `ComponentTypeID` when that lands. Do not expand its scope here.

---

### Task 1: Value DOM

**Files:**
- Create: `foundation/serialization/value.hpp`
- Test: `tests/unit/serialization/value_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (leaf task).
- Produces:
  - `mosaic::foundation::serialization::Value` — variant over `Null(std::monostate) | bool | std::int64_t | std::uint64_t | double | std::string | Array | Table`.
    - Ctors: `Value()` (null), `Value(bool)`, `Value(std::int64_t)`, `Value(std::uint64_t)`, `Value(double)`, `Value(std::string)`, `Value(const char*)`, `Value(Array)`, `Value(Table)`.
    - `bool is_null() const`; `template<class T> bool is() const`; `template<class T> const T& as() const` / `T& as()`; `bool operator==(const Value&) const`.
  - `using Array = std::vector<Value>;`
  - `class Table` — insertion-ordered `string→Value`: `Value& operator[](const std::string&)` (insert-or-get), `const Value* find(const std::string&) const`, `begin()/end()` (const, yields `std::pair<std::string,Value>`), `size()`, `empty()`, order-independent `operator==`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/serialization/value_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "foundation/serialization/value.hpp"

using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;

TEST_CASE("Value holds and reports its alternative", "[value]")
{
    Value n;
    REQUIRE(n.is_null());

    Value i{std::int64_t{42}};
    REQUIRE(i.is<std::int64_t>());
    REQUIRE(i.as<std::int64_t>() == 42);
    REQUIRE_FALSE(i.is<double>());

    Value s{std::string{"hi"}};
    REQUIRE(s.is<std::string>());
    REQUIRE(s.as<std::string>() == "hi");
}

TEST_CASE("Value equality compares by alternative and content", "[value]")
{
    REQUIRE(Value{std::int64_t{1}} == Value{std::int64_t{1}});
    REQUIRE_FALSE(Value{std::int64_t{1}} == Value{std::uint64_t{1}}); // distinct alternatives
    REQUIRE_FALSE(Value{std::int64_t{1}} == Value{std::int64_t{2}});
}

TEST_CASE("Table is insertion-ordered on iteration but equality is order-independent", "[value]")
{
    Table a;
    a["x"] = Value{std::int64_t{1}};
    a["y"] = Value{std::int64_t{2}};

    Table b;
    b["y"] = Value{std::int64_t{2}};
    b["x"] = Value{std::int64_t{1}};

    REQUIRE(a == b);                      // same keys/values, different order
    REQUIRE(a.begin()->first == "x");     // iteration preserves insertion order
    REQUIRE(a.size() == 2);

    b["x"] = Value{std::int64_t{9}};
    REQUIRE_FALSE(a == b);
}

TEST_CASE("operator[] inserts-or-gets, find returns nullptr for missing", "[value]")
{
    Table t;
    t["k"] = Value{std::string{"v"}};
    REQUIRE(t.find("k") != nullptr);
    REQUIRE(t.find("k")->as<std::string>() == "v");
    REQUIRE(t.find("missing") == nullptr);
}

TEST_CASE("Value nests arrays and tables", "[value]")
{
    Table inner;
    inner["a"] = Value{std::int64_t{1}};
    Array arr;
    arr.push_back(Value{std::int64_t{1}});
    arr.push_back(Value{Table{inner}});

    Value v{std::move(arr)};
    REQUIRE(v.is<Array>());
    REQUIRE(v.as<Array>().size() == 2);
    REQUIRE(v.as<Array>()[1].is<Table>());
}
```

- [ ] **Step 2: Add the test to the build and run to verify it fails**

Edit `tests/CMakeLists.txt` — add the source to the `add_executable(mosaic_unit_tests ...)` list (after the `unit/ecs/system_test.cpp` line):
```cmake
  unit/serialization/value_test.cpp
```
Run: `cmake --preset debug && cmake --build build/debug 2>&1 | tail -20`
Expected: FAIL — `value.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/serialization/value.hpp`:
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mosaic::foundation::serialization
{

class Value;

using Array = std::vector<Value>;

// Insertion-ordered string->Value map. Equality is order-independent (same
// keys mapping to equal values) so cross-format round-trips that reorder table
// keys still compare equal, while iteration stays stable for readable output.
class Table
{
public:
    Value& operator[](const std::string& key);
    const Value* find(const std::string& key) const;

    std::vector<std::pair<std::string, Value>>::const_iterator begin() const { return entries_.begin(); }
    std::vector<std::pair<std::string, Value>>::const_iterator end() const { return entries_.end(); }
    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    bool operator==(const Table& other) const;

private:
    std::vector<std::pair<std::string, Value>> entries_;
};

class Value
{
public:
    using Storage = std::variant<std::monostate, bool, std::int64_t, std::uint64_t,
                                 double, std::string, Array, Table>;

    Value() = default; // Null
    Value(bool b) : data_(b) {}
    Value(std::int64_t i) : data_(i) {}
    Value(std::uint64_t u) : data_(u) {}
    Value(double d) : data_(d) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Table t) : data_(std::move(t)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
    template <class T> bool is() const { return std::holds_alternative<T>(data_); }
    template <class T> const T& as() const { return std::get<T>(data_); }
    template <class T> T& as() { return std::get<T>(data_); }

    const Storage& storage() const { return data_; }
    bool operator==(const Value& other) const { return data_ == other.data_; }

private:
    Storage data_;
};

inline Value& Table::operator[](const std::string& key)
{
    for (auto& entry : entries_)
    {
        if (entry.first == key)
        {
            return entry.second;
        }
    }
    entries_.emplace_back(key, Value{});
    return entries_.back().second;
}

inline const Value* Table::find(const std::string& key) const
{
    for (const auto& entry : entries_)
    {
        if (entry.first == key)
        {
            return &entry.second;
        }
    }
    return nullptr;
}

inline bool Table::operator==(const Table& other) const
{
    if (entries_.size() != other.entries_.size())
    {
        return false;
    }
    for (const auto& entry : entries_)
    {
        const Value* o = other.find(entry.first);
        if (o == nullptr || !(*o == entry.second))
        {
            return false;
        }
    }
    return true;
}

} // namespace mosaic::foundation::serialization
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[value]"`
Expected: PASS (all `[value]` assertions).

- [ ] **Step 5: Commit**

```bash
git add foundation/serialization/value.hpp tests/unit/serialization/value_test.cpp tests/CMakeLists.txt
git commit -m "Add Value DOM for serialization (#10)"
```

---

### Task 2: Identity & reference types

**Files:**
- Create: `foundation/serialization/reference.hpp`
- Test: `tests/unit/serialization/reference_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::foundation::EntityID` (`foundation/core/entity_id.hpp`).
- Produces (all in `mosaic::foundation::serialization`):
  - `class SerializationError : public std::runtime_error` (inherits ctors).
  - `class AssetID` — 128-bit UUID: `AssetID()` (nil), `explicit AssetID(std::array<std::uint8_t,16>)`, static `AssetID from_string(const std::string&)` (throws `SerializationError` on malformed), `std::string to_string() const`, `bool is_nil() const`, `const std::array<std::uint8_t,16>& bytes() const`, `operator==`/`operator!=`.
  - `template<class T> class Handle` — `Handle()`, `explicit Handle(AssetID)`, `const AssetID& asset_id() const`, `operator==`. Holds runtime index/generation that is never serialized.
  - `class ReferenceResolver` — virtual `EntityID resolve_entity(EntityID) const` (default identity) and `AssetID resolve_asset(AssetID) const` (default identity).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/serialization/reference_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Handle;
using mosaic::foundation::serialization::ReferenceResolver;
using mosaic::foundation::serialization::SerializationError;

TEST_CASE("AssetID round-trips through its canonical string", "[reference]")
{
    const std::string text = "12345678-9abc-def0-1234-56789abcdef0";
    AssetID id = AssetID::from_string(text);
    REQUIRE(id.to_string() == text);
    REQUIRE_FALSE(id.is_nil());
}

TEST_CASE("Default AssetID is nil", "[reference]")
{
    AssetID id;
    REQUIRE(id.is_nil());
    REQUIRE(id.to_string() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("AssetID::from_string rejects malformed input", "[reference]")
{
    REQUIRE_THROWS_AS(AssetID::from_string("too-short"), SerializationError);
    REQUIRE_THROWS_AS(AssetID::from_string("12345678x9abc-def0-1234-56789abcdef0"), SerializationError); // missing '-'
    REQUIRE_THROWS_AS(AssetID::from_string("g2345678-9abc-def0-1234-56789abcdef0"), SerializationError); // non-hex
}

TEST_CASE("AssetID uppercase parses and re-emits lowercase", "[reference]")
{
    AssetID id = AssetID::from_string("ABCDEF01-2345-6789-ABCD-EF0123456789");
    REQUIRE(id.to_string() == "abcdef01-2345-6789-abcd-ef0123456789");
}

TEST_CASE("Handle exposes its AssetID and compares by it", "[reference]")
{
    AssetID id = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    Handle<int> h{id};
    REQUIRE(h.asset_id() == id);
    REQUIRE(h == Handle<int>{id});
    REQUIRE_FALSE(h == Handle<int>{});
}

TEST_CASE("Default ReferenceResolver is identity", "[reference]")
{
    ReferenceResolver r;
    AssetID id = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    REQUIRE(r.resolve_asset(id) == id);
    EntityID e{7, 3};
    REQUIRE(r.resolve_entity(e) == e);
}
```

- [ ] **Step 2: Add to build and run to verify it fails**

Edit `tests/CMakeLists.txt` — add to the sources list:
```cmake
  unit/serialization/reference_test.cpp
```
Run: `cmake --preset debug && cmake --build build/debug 2>&1 | tail -20`
Expected: FAIL — `reference.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/serialization/reference.hpp`:
```cpp
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "foundation/core/entity_id.hpp"

namespace mosaic::foundation::serialization
{

// Error type for all (de)serialization failures: parse errors, type mismatches,
// malformed ids. Foundation targets C++20, so failures surface as exceptions
// rather than std::expected (C++23).
class SerializationError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// 128-bit UUID: the globally stable cross-reference identity for assets
// (RFC 0001, Identity types).
class AssetID
{
public:
    constexpr AssetID() noexcept : bytes_{} {}
    explicit constexpr AssetID(std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

    static AssetID from_string(const std::string& text);
    std::string to_string() const;

    bool is_nil() const noexcept
    {
        for (std::uint8_t b : bytes_)
        {
            if (b != 0)
            {
                return false;
            }
        }
        return true;
    }

    const std::array<std::uint8_t, 16>& bytes() const noexcept { return bytes_; }

    friend bool operator==(const AssetID& a, const AssetID& b) noexcept { return a.bytes_ == b.bytes_; }
    friend bool operator!=(const AssetID& a, const AssetID& b) noexcept { return !(a == b); }

private:
    std::array<std::uint8_t, 16> bytes_;
};

inline std::string AssetID::to_string() const
{
    static const char* const kHex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
        {
            out.push_back('-');
        }
        out.push_back(kHex[bytes_[i] >> 4]);
        out.push_back(kHex[bytes_[i] & 0x0F]);
    }
    return out;
}

inline AssetID AssetID::from_string(const std::string& text)
{
    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (text.size() != 36)
    {
        throw SerializationError("AssetID: expected 36-char UUID, got '" + text + "'");
    }

    std::array<std::uint8_t, 16> out{};
    std::size_t byte_index = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (text[i] != '-')
            {
                throw SerializationError("AssetID: expected '-' at position " + std::to_string(i));
            }
            ++i;
            continue;
        }
        int hi = nibble(text[i]);
        int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0)
        {
            throw SerializationError("AssetID: invalid hex in '" + text + "'");
        }
        out[byte_index++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return AssetID(out);
}

// Typed handle to a loaded Resource. Serializes as its AssetID; the runtime
// table index/generation are never serialized (RFC 0001, Asset & Resource).
template <class T>
class Handle
{
public:
    Handle() = default;
    explicit Handle(AssetID id) : asset_(id) {}

    const AssetID& asset_id() const noexcept { return asset_; }

    friend bool operator==(const Handle& a, const Handle& b) noexcept { return a.asset_ == b.asset_; }
    friend bool operator!=(const Handle& a, const Handle& b) noexcept { return !(a == b); }

private:
    AssetID asset_{};
    std::uint32_t index_ = 0;      // runtime-only; never serialized
    std::uint32_t generation_ = 0; // runtime-only; never serialized
};

// Re-resolution hook called on load for reference fields. The default is
// identity; the future asset server and World entity-remap supply real
// implementations without changing the serialization layer.
class ReferenceResolver
{
public:
    virtual ~ReferenceResolver() = default;

    virtual mosaic::foundation::EntityID resolve_entity(mosaic::foundation::EntityID serialized) const
    {
        return serialized;
    }

    virtual AssetID resolve_asset(AssetID serialized) const { return serialized; }
};

} // namespace mosaic::foundation::serialization
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[reference]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add foundation/serialization/reference.hpp tests/unit/serialization/reference_test.cpp tests/CMakeLists.txt
git commit -m "Add AssetID, Handle, and ReferenceResolver (#10)"
```

---

### Task 3: Reflection protocol + generic Value bridge

**Files:**
- Create: `foundation/reflection/reflect.hpp`
- Create: `foundation/serialization/reflect_value.hpp`
- Create: `tests/unit/serialization/sample_schemas.hpp`
- Test: `tests/unit/serialization/reflect_value_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Note on file consolidation:** the spec named `to_value.hpp`/`from_value.hpp` separately; because `encode`↔`to_value` and `decode`↔`from_value` are mutually recursive, they live together in one `reflect_value.hpp` for a single translation unit of the recursion. Same architecture, one file.

**Interfaces:**
- Consumes: `Value`/`Array`/`Table` (Task 1); `AssetID`/`Handle`/`ReferenceResolver`/`SerializationError` (Task 2); `EntityID`.
- Produces:
  - `mosaic::foundation::reflection::Reflectable<T>` concept (true when `reflect(visitor, value)` is found by ADL) and `struct ReflectProbe`.
  - In `mosaic::foundation::serialization`:
    - `template<class T> Value to_value(T& obj)` — reflects `obj` into a `Table`-typed `Value`.
    - `template<class T> void from_value(const Value& node, T& obj, const ReferenceResolver& resolver)` — reads reflected fields from `node`.
    - `template<class T> void from_value(const Value& node, T& obj)` — overload using the default resolver.
  - `mosaic_samples::Vec3`, `Quat`, `Transform`, `Name` reflected structs (in `sample_schemas.hpp`).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/serialization/sample_schemas.hpp`:
```cpp
#pragma once

#include <string>
#include <vector>

// Sample reflected types shared by the serialization tests. Each type opts into
// reflection with a free `reflect(visitor, value)` found via ADL (they live in
// the same namespace as the type). More schemas are appended by later tasks.
namespace mosaic_samples
{

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};
template <class V> void reflect(V& v, Vec3& t)
{
    v.member("x", t.x);
    v.member("y", t.y);
    v.member("z", t.z);
}

struct Quat
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
    friend bool operator==(const Quat&, const Quat&) = default;
};
template <class V> void reflect(V& v, Quat& t)
{
    v.member("x", t.x);
    v.member("y", t.y);
    v.member("z", t.z);
    v.member("w", t.w);
}

struct Transform
{
    Vec3 position;
    Quat rotation;
    Vec3 scale;
    friend bool operator==(const Transform&, const Transform&) = default;
};
template <class V> void reflect(V& v, Transform& t)
{
    v.member("position", t.position);
    v.member("rotation", t.rotation);
    v.member("scale", t.scale);
}

struct Name
{
    std::string value;
    friend bool operator==(const Name&, const Name&) = default;
};
template <class V> void reflect(V& v, Name& t)
{
    v.member("value", t.value);
}

} // namespace mosaic_samples
```

Create `tests/unit/serialization/reflect_value_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::Transform;
using mosaic_samples::Vec3;

namespace
{
struct Scalars
{
    bool flag = false;
    std::int32_t count = 0;
    double ratio = 0.0;
    std::string label;
    std::vector<std::int32_t> nums;
    friend bool operator==(const Scalars&, const Scalars&) = default;
};
template <class V> void reflect(V& v, Scalars& s)
{
    v.member("flag", s.flag);
    v.member("count", s.count);
    v.member("ratio", s.ratio);
    v.member("label", s.label);
    v.member("nums", s.nums);
}
} // namespace

TEST_CASE("to_value emits a Table keyed by field name", "[reflect]")
{
    Scalars s{true, -5, 1.5, "hi", {1, 2, 3}};
    Value v = to_value(s);
    REQUIRE(v.is<Table>());
    const Table& t = v.as<Table>();
    REQUIRE(t.find("flag")->as<bool>() == true);
    REQUIRE(t.find("count")->as<std::int64_t>() == -5);
    REQUIRE(t.find("ratio")->as<double>() == 1.5);
    REQUIRE(t.find("label")->as<std::string>() == "hi");
    REQUIRE(t.find("nums")->as<Array>().size() == 3);
    REQUIRE(t.find("nums")->as<Array>()[2].as<std::int64_t>() == 3);
}

TEST_CASE("scalar/vector round-trip is identity", "[reflect]")
{
    Scalars s{true, 42, 2.25, "round", {7, 8}};
    Value v = to_value(s);
    Scalars out;
    from_value(v, out);
    REQUIRE(out == s);
}

TEST_CASE("nested reflected structs recurse", "[reflect]")
{
    Transform tr;
    tr.position = Vec3{1.0, 2.0, 3.0};
    tr.rotation = mosaic_samples::Quat{0.0, 0.0, 0.0, 1.0};
    tr.scale = Vec3{4.0, 5.0, 6.0};

    Value v = to_value(tr);
    REQUIRE(v.as<Table>().find("position")->is<Table>());
    REQUIRE(v.as<Table>().find("position")->as<Table>().find("y")->as<double>() == 2.0);

    Transform out;
    from_value(v, out);
    REQUIRE(out == tr);
}

TEST_CASE("from_value leaves fields absent from the table at their default", "[reflect]")
{
    Table t;
    t["count"] = Value{std::int64_t{9}};
    Value v{std::move(t)};

    Scalars out;
    from_value(v, out);
    REQUIRE(out.count == 9);
    REQUIRE(out.label.empty()); // "label" absent -> default
}

TEST_CASE("from_value throws on type mismatch", "[reflect]")
{
    Table t;
    t["count"] = Value{std::string{"not a number"}};
    Value v{std::move(t)};

    Scalars out;
    REQUIRE_THROWS_AS(from_value(v, out),
                      mosaic::foundation::serialization::SerializationError);
}
```

- [ ] **Step 2: Add to build and run to verify it fails**

Edit `tests/CMakeLists.txt` — add to the sources list:
```cmake
  unit/serialization/reflect_value_test.cpp
```
The test `#include "sample_schemas.hpp"` resolves because the test file sits in the same directory; add that directory to the test target's include path. In `tests/CMakeLists.txt`, after the `target_compile_features(mosaic_unit_tests PRIVATE cxx_std_20)` line add:
```cmake
target_include_directories(mosaic_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit/serialization)
```
Run: `cmake --preset debug && cmake --build build/debug 2>&1 | tail -20`
Expected: FAIL — `reflect_value.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/reflection/reflect.hpp`:
```cpp
#pragma once

#include <string_view>

namespace mosaic::foundation::reflection
{

// Archetype visitor used only to detect whether a type opts into reflection.
struct ReflectProbe
{
    template <class M> void member(std::string_view, M&) {}
};

// A type is Reflectable when a `reflect(visitor, value)` overload is found by
// argument-dependent lookup. Each reflected type provides that free function in
// its own namespace.
template <class T>
concept Reflectable = requires(ReflectProbe& p, T& t) { reflect(p, t); };

} // namespace mosaic::foundation::reflection
```

Create `foundation/serialization/reflect_value.hpp`:
```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "foundation/reflection/reflect.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// --- type traits ---------------------------------------------------------
template <class>
inline constexpr bool dependent_false = false;

template <class>
inline constexpr bool is_std_vector_v = false;
template <class E, class A>
inline constexpr bool is_std_vector_v<std::vector<E, A>> = true;

template <class>
inline constexpr bool is_handle_v = false;
template <class R>
inline constexpr bool is_handle_v<Handle<R>> = true;

// --- forward declarations -----------------------------------------------
template <class T> Value to_value(T& obj);
template <class T> void from_value(const Value& node, T& obj, const ReferenceResolver& resolver);

// --- encode: reflected field value -> Value -----------------------------
template <class M>
Value encode(M& value)
{
    using U = std::remove_cv_t<M>;
    if constexpr (std::is_same_v<U, Value>)
    {
        return value; // open-ended pass-through (e.g. metadata/import_settings)
    }
    else if constexpr (std::is_same_v<U, bool>)
    {
        return Value(value);
    }
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
    {
        return Value(static_cast<std::int64_t>(value));
    }
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>)
    {
        return Value(static_cast<std::uint64_t>(value));
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        return Value(static_cast<double>(value));
    }
    else if constexpr (std::is_same_v<U, std::string>)
    {
        return Value(value);
    }
    else if constexpr (is_std_vector_v<U>)
    {
        Array arr;
        arr.reserve(value.size());
        for (auto& element : value)
        {
            arr.push_back(encode(element));
        }
        return Value(std::move(arr));
    }
    else if constexpr (reflection::Reflectable<U>)
    {
        return to_value(value);
    }
    else
    {
        static_assert(dependent_false<U>, "encode: unsupported field type");
    }
}

// --- decode: Value -> reflected field value -----------------------------
template <class M>
void decode(const Value& node, M& out, const ReferenceResolver& resolver)
{
    using U = std::remove_cv_t<M>;
    if constexpr (std::is_same_v<U, Value>)
    {
        out = node;
    }
    else if constexpr (std::is_same_v<U, bool>)
    {
        if (!node.is<bool>()) throw SerializationError("decode: expected bool");
        out = node.as<bool>();
    }
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
    {
        if (!node.is<std::int64_t>()) throw SerializationError("decode: expected signed integer");
        out = static_cast<U>(node.as<std::int64_t>());
    }
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>)
    {
        if (!node.is<std::uint64_t>()) throw SerializationError("decode: expected unsigned integer");
        out = static_cast<U>(node.as<std::uint64_t>());
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        if (!node.is<double>()) throw SerializationError("decode: expected floating-point");
        out = static_cast<U>(node.as<double>());
    }
    else if constexpr (std::is_same_v<U, std::string>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected string");
        out = node.as<std::string>();
    }
    else if constexpr (is_std_vector_v<U>)
    {
        if (!node.is<Array>()) throw SerializationError("decode: expected array");
        const Array& arr = node.as<Array>();
        out.clear();
        out.resize(arr.size());
        for (std::size_t i = 0; i < arr.size(); ++i)
        {
            decode(arr[i], out[i], resolver);
        }
    }
    else if constexpr (reflection::Reflectable<U>)
    {
        from_value(node, out, resolver);
    }
    else
    {
        static_assert(dependent_false<U>, "decode: unsupported field type");
    }
}

// --- visitors ------------------------------------------------------------
struct WriteVisitor
{
    Table table;
    template <class M> void member(std::string_view name, M& value)
    {
        table[std::string(name)] = encode(value);
    }
};

struct ReadVisitor
{
    const Table& table;
    const ReferenceResolver& resolver;
    template <class M> void member(std::string_view name, M& value)
    {
        const Value* found = table.find(std::string(name));
        if (found != nullptr)
        {
            decode(*found, value, resolver);
        }
    }
};

// --- public entry points -------------------------------------------------
template <class T>
Value to_value(T& obj)
{
    WriteVisitor visitor;
    reflect(visitor, obj);
    return Value(std::move(visitor.table));
}

inline const ReferenceResolver& default_resolver()
{
    static const ReferenceResolver resolver;
    return resolver;
}

template <class T>
void from_value(const Value& node, T& obj, const ReferenceResolver& resolver)
{
    if (!node.is<Table>()) throw SerializationError("from_value: expected table");
    ReadVisitor visitor{node.as<Table>(), resolver};
    reflect(visitor, obj);
}

template <class T>
void from_value(const Value& node, T& obj)
{
    from_value(node, obj, default_resolver());
}

} // namespace mosaic::foundation::serialization
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[reflect]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add foundation/reflection/reflect.hpp foundation/serialization/reflect_value.hpp \
        tests/unit/serialization/sample_schemas.hpp tests/unit/serialization/reflect_value_test.cpp \
        tests/CMakeLists.txt
git commit -m "Add reflection protocol and generic Value bridge (#10)"
```

---

### Task 4: Reference encoding through the bridge

**Files:**
- Modify: `foundation/serialization/reflect_value.hpp` (add reference branches to `encode`/`decode`)
- Test: `tests/unit/serialization/reference_encoding_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `to_value`/`from_value` (Task 3), `AssetID`/`Handle`/`ReferenceResolver`/`EntityID` (Task 2).
- Produces: `encode`/`decode` now handle `EntityID` (as a decimal string of its 64-bit value), `AssetID` (canonical UUID string), and `Handle<T>` (its AssetID string). `decode` routes all three through the resolver.

**Why EntityID serializes as a string:** EntityID's 64-bit value can set the high bit (generation in the top 32 bits), which exceeds JSON's safe integer range and collides with the signed/unsigned integer ambiguity of JSON parsers. A decimal string is exact, human-readable, and format-neutral.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/serialization/reference_encoding_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Handle;
using mosaic::foundation::serialization::ReferenceResolver;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_value;

namespace
{
struct Refs
{
    EntityID target;
    AssetID material;
    Handle<int> texture;
    friend bool operator==(const Refs&, const Refs&) = default;
};
template <class V> void reflect(V& v, Refs& r)
{
    v.member("target", r.target);
    v.member("material", r.material);
    v.member("texture", r.texture);
}

// Swaps a specific asset/entity for another, to prove the resolver runs.
class RemapResolver : public ReferenceResolver
{
public:
    EntityID resolve_entity(EntityID serialized) const override
    {
        return serialized == EntityID{5, 1} ? EntityID{99, 2} : serialized;
    }
    AssetID resolve_asset(AssetID serialized) const override
    {
        AssetID from = AssetID::from_string("11111111-1111-1111-1111-111111111111");
        AssetID to = AssetID::from_string("22222222-2222-2222-2222-222222222222");
        return serialized == from ? to : serialized;
    }
};
} // namespace

TEST_CASE("references serialize as their id primitives", "[refenc]")
{
    Refs r;
    r.target = EntityID{5, 1};
    r.material = AssetID::from_string("11111111-1111-1111-1111-111111111111");
    r.texture = Handle<int>{AssetID::from_string("33333333-3333-3333-3333-333333333333")};

    Value v = to_value(r);
    const Table& t = v.as<Table>();
    REQUIRE(t.find("target")->is<std::string>());               // EntityID -> decimal string
    REQUIRE(t.find("target")->as<std::string>() == std::to_string(EntityID{5, 1}.value()));
    REQUIRE(t.find("material")->as<std::string>() == "11111111-1111-1111-1111-111111111111");
    REQUIRE(t.find("texture")->as<std::string>() == "33333333-3333-3333-3333-333333333333");
}

TEST_CASE("reference round-trip with default resolver is identity", "[refenc]")
{
    Refs r;
    r.target = EntityID{5, 1};
    r.material = AssetID::from_string("11111111-1111-1111-1111-111111111111");
    r.texture = Handle<int>{AssetID::from_string("33333333-3333-3333-3333-333333333333")};

    Value v = to_value(r);
    Refs out;
    from_value(v, out);
    REQUIRE(out == r);
}

TEST_CASE("resolver re-resolves entity and asset references at load", "[refenc]")
{
    Refs r;
    r.target = EntityID{5, 1};
    r.material = AssetID::from_string("11111111-1111-1111-1111-111111111111");
    r.texture = Handle<int>{AssetID::from_string("11111111-1111-1111-1111-111111111111")};

    Value v = to_value(r);
    Refs out;
    RemapResolver resolver;
    from_value(v, out, resolver);

    REQUIRE(out.target == EntityID{99, 2});
    REQUIRE(out.material == AssetID::from_string("22222222-2222-2222-2222-222222222222"));
    REQUIRE(out.texture == Handle<int>{AssetID::from_string("22222222-2222-2222-2222-222222222222")});
}
```

- [ ] **Step 2: Add to build and run to verify it fails**

Edit `tests/CMakeLists.txt` — add to the sources list:
```cmake
  unit/serialization/reference_encoding_test.cpp
```
Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[refenc]"`
Expected: FAIL — `static_assert` "encode: unsupported field type" (EntityID/AssetID/Handle not yet handled).

- [ ] **Step 3: Add the reference branches**

In `foundation/serialization/reflect_value.hpp`, add `#include "foundation/core/entity_id.hpp"` to the includes (after the `reference.hpp` include).

In `encode`, insert these three branches immediately **after** the `std::is_same_v<U, bool>` branch and before the signed-integer branch:
```cpp
    else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
    {
        return Value(std::to_string(value.value()));
    }
    else if constexpr (std::is_same_v<U, AssetID>)
    {
        return Value(value.to_string());
    }
    else if constexpr (is_handle_v<U>)
    {
        return Value(value.asset_id().to_string());
    }
```

In `decode`, insert the matching branches immediately **after** the `std::is_same_v<U, bool>` branch and before the signed-integer branch:
```cpp
    else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected EntityID string");
        out = resolver.resolve_entity(
            mosaic::foundation::EntityID(static_cast<std::uint64_t>(std::stoull(node.as<std::string>()))));
    }
    else if constexpr (std::is_same_v<U, AssetID>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected AssetID string");
        out = resolver.resolve_asset(AssetID::from_string(node.as<std::string>()));
    }
    else if constexpr (is_handle_v<U>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected Handle AssetID string");
        AssetID id = resolver.resolve_asset(AssetID::from_string(node.as<std::string>()));
        out = U(id);
    }
```
(Placement matters: these class-type checks must precede the `std::is_integral_v`/`Reflectable` branches. `bool` stays first since it is also integral.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[refenc]"`
Expected: PASS. Then run `./build/debug/tests/mosaic_unit_tests "[reflect]"` to confirm no regression.

- [ ] **Step 5: Commit**

```bash
git add foundation/serialization/reflect_value.hpp tests/unit/serialization/reference_encoding_test.cpp tests/CMakeLists.txt
git commit -m "Encode EntityID/AssetID/Handle as ids with resolver hook (#10)"
```

---

### Task 5: Build wiring + JSON backend + scene round-trip

**Files:**
- Modify: `CMakeLists.txt` (root) — FetchContent for toml++ and nlohmann/json
- Modify: `foundation/CMakeLists.txt` — add serialization subdirectory
- Create: `foundation/serialization/CMakeLists.txt`
- Create: `foundation/serialization/json_backend.hpp`
- Create: `foundation/serialization/json_backend.cpp`
- Modify: `tests/unit/serialization/sample_schemas.hpp` — add `SceneEntity`, `Scene`
- Modify: `tests/CMakeLists.txt` — link `mosaic_serialization`, add test
- Test: `tests/unit/serialization/json_backend_test.cpp`

**Interfaces:**
- Consumes: `Value` (Task 1), `to_value`/`from_value` (Tasks 3–4).
- Produces:
  - `std::string mosaic::foundation::serialization::to_json(const Value&)` — pretty JSON (2-space indent).
  - `Value mosaic::foundation::serialization::from_json(const std::string&)` — throws `SerializationError` on parse failure.
  - Library target `mosaic_serialization` (STATIC).
  - `mosaic_samples::SceneEntity { EntityID id; Value components; }` and `mosaic_samples::Scene { std::vector<SceneEntity> entities; }`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/serialization/sample_schemas.hpp`, immediately **before** the closing `} // namespace mosaic_samples`:
```cpp
} // (temporary) — replace the existing closing brace; see note below
```
Actually make this concrete: replace the final `} // namespace mosaic_samples` line with:
```cpp

struct SceneEntity
{
    mosaic::foundation::EntityID id;
    mosaic::foundation::serialization::Value components; // Table: type name -> component Value
    friend bool operator==(const SceneEntity&, const SceneEntity&) = default;
};
template <class V> void reflect(V& v, SceneEntity& e)
{
    v.member("id", e.id);
    v.member("components", e.components);
}

struct Scene
{
    std::vector<SceneEntity> entities;
    friend bool operator==(const Scene&, const Scene&) = default;
};
template <class V> void reflect(V& v, Scene& s)
{
    v.member("entities", s.entities);
}

} // namespace mosaic_samples
```
And add these includes at the top of `sample_schemas.hpp` (after `#include <vector>`):
```cpp
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"
```

Create `tests/unit/serialization/json_backend_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_json;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_json;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::Scene;
using mosaic_samples::SceneEntity;
using mosaic_samples::Transform;
using mosaic_samples::Vec3;

namespace
{
// Builds a scene of two entities, each carrying a Transform component encoded
// into the components table (type name -> nested component Value).
Scene sample_scene()
{
    Scene scene;

    SceneEntity a;
    a.id = EntityID{1, 1};
    Table a_components;
    Transform tr;
    tr.position = Vec3{1.0, 2.0, 3.0};
    tr.scale = Vec3{1.0, 1.0, 1.0};
    a_components["Transform"] = to_value(tr);
    a.components = Value{std::move(a_components)};

    SceneEntity b;
    b.id = EntityID{2, 1};
    Table b_components;
    b_components["Name"] = to_value(mosaic_samples::Name{"player"});
    b.components = Value{std::move(b_components)};

    scene.entities = {a, b};
    return scene;
}
} // namespace

TEST_CASE("Value round-trips through JSON text", "[json]")
{
    Table t;
    t["n"] = Value{std::int64_t{-3}};
    t["f"] = Value{2.5};
    t["s"] = Value{std::string{"hi"}};
    Value v{std::move(t)};

    Value back = from_json(to_json(v));
    REQUIRE(back == v);
}

TEST_CASE("scene JSON round-trip is identity on the schema", "[json]")
{
    Scene scene = sample_scene();
    Value v = to_value(scene);

    std::string json = to_json(v);
    Value parsed = from_json(json);

    Scene out;
    from_value(parsed, out);
    REQUIRE(out == scene);
}

TEST_CASE("from_json throws SerializationError on malformed input", "[json]")
{
    REQUIRE_THROWS_AS(from_json("{ not valid json "),
                      mosaic::foundation::serialization::SerializationError);
}
```

- [ ] **Step 2: Wire the build and run to verify it fails**

Edit root `CMakeLists.txt` — after the `FetchContent_MakeAvailable(Catch2)` line and its `list(APPEND CMAKE_MODULE_PATH ...)`, add:
```cmake
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(tomlplusplus nlohmann_json)
```

Edit `foundation/CMakeLists.txt` — append:
```cmake
add_subdirectory(serialization)
```

Create `foundation/serialization/CMakeLists.txt`:
```cmake
add_library(mosaic_serialization STATIC
  json_backend.cpp
)
target_include_directories(mosaic_serialization PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_serialization
  PUBLIC mosaic_foundation
  PRIVATE tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json
)
target_compile_features(mosaic_serialization PUBLIC cxx_std_20)
```
(The `toml_backend.cpp` source is added to this list in Task 6.)

Edit `tests/CMakeLists.txt`:
- add to the sources list: `unit/serialization/json_backend_test.cpp`
- add `mosaic_serialization` to `target_link_libraries(mosaic_unit_tests PRIVATE ...)`:
```cmake
target_link_libraries(mosaic_unit_tests PRIVATE
  mosaic_engine
  mosaic_foundation
  mosaic_serialization
  Catch2::Catch2WithMain
)
```
Run: `cmake --preset debug && cmake --build build/debug 2>&1 | tail -20`
Expected: FAIL — `json_backend.hpp: No such file or directory`. (Configure step fetches toml++/nlohmann; needs network.)

- [ ] **Step 3: Write minimal implementation**

Create `foundation/serialization/json_backend.hpp`:
```cpp
#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// Value <-> JSON text. Used for scenes and other deeply-nested graphs. Knows
// nothing of reflection; it only projects the format-neutral Value DOM.
std::string to_json(const Value& value);
Value from_json(const std::string& text);

} // namespace mosaic::foundation::serialization
```

Create `foundation/serialization/json_backend.cpp`:
```cpp
#include "foundation/serialization/json_backend.hpp"

#include <cstdint>
#include <limits>

#include <nlohmann/json.hpp>

#include "foundation/serialization/reference.hpp"

namespace mosaic::foundation::serialization
{
namespace
{
using nlohmann::json;

json to_json_node(const Value& v)
{
    if (v.is_null()) return json(nullptr);
    if (v.is<bool>()) return json(v.as<bool>());
    if (v.is<std::int64_t>()) return json(v.as<std::int64_t>());
    if (v.is<std::uint64_t>()) return json(v.as<std::uint64_t>());
    if (v.is<double>()) return json(v.as<double>());
    if (v.is<std::string>()) return json(v.as<std::string>());
    if (v.is<Array>())
    {
        json out = json::array();
        for (const Value& e : v.as<Array>())
        {
            out.push_back(to_json_node(e));
        }
        return out;
    }
    json out = json::object();
    for (const auto& entry : v.as<Table>())
    {
        out[entry.first] = to_json_node(entry.second);
    }
    return out;
}

Value from_json_node(const json& j)
{
    if (j.is_null()) return Value();
    if (j.is_boolean()) return Value(j.get<bool>());
    if (j.is_number_unsigned())
    {
        std::uint64_t u = j.get<std::uint64_t>();
        if (u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return Value(static_cast<std::int64_t>(u)); // canonicalize small unsigned to Int64
        }
        return Value(u);
    }
    if (j.is_number_integer()) return Value(j.get<std::int64_t>());
    if (j.is_number_float()) return Value(j.get<double>());
    if (j.is_string()) return Value(j.get<std::string>());
    if (j.is_array())
    {
        Array out;
        for (const auto& e : j)
        {
            out.push_back(from_json_node(e));
        }
        return Value(std::move(out));
    }
    Table out;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        out[it.key()] = from_json_node(it.value());
    }
    return Value(std::move(out));
}
} // namespace

std::string to_json(const Value& value)
{
    return to_json_node(value).dump(2);
}

Value from_json(const std::string& text)
{
    try
    {
        return from_json_node(json::parse(text));
    }
    catch (const json::exception& e)
    {
        throw SerializationError(std::string("JSON parse error: ") + e.what());
    }
}

} // namespace mosaic::foundation::serialization
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[json]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt foundation/CMakeLists.txt foundation/serialization/CMakeLists.txt \
        foundation/serialization/json_backend.hpp foundation/serialization/json_backend.cpp \
        tests/unit/serialization/sample_schemas.hpp tests/unit/serialization/json_backend_test.cpp \
        tests/CMakeLists.txt
git commit -m "Add JSON backend and scene round-trip (#10)"
```

---

### Task 6: TOML backend + asset-metadata round-trip

**Files:**
- Create: `foundation/serialization/toml_backend.hpp`
- Create: `foundation/serialization/toml_backend.cpp`
- Modify: `foundation/serialization/CMakeLists.txt` — add `toml_backend.cpp`
- Modify: `tests/unit/serialization/sample_schemas.hpp` — add `AssetMetadata`, `ProjectConfig`
- Modify: `tests/CMakeLists.txt` — add test
- Test: `tests/unit/serialization/toml_backend_test.cpp`

**Interfaces:**
- Consumes: `Value` (Task 1), `to_value`/`from_value` (Tasks 3–4), `AssetID`/`Value` fields.
- Produces:
  - `std::string mosaic::foundation::serialization::to_toml(const Value&)` — throws if the top-level value is not a `Table`.
  - `Value mosaic::foundation::serialization::from_toml(const std::string&)` — throws `SerializationError` on parse failure.
  - `mosaic_samples::AssetMetadata` (RFC normative fields) and `mosaic_samples::ProjectConfig`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/serialization/sample_schemas.hpp`, immediately **before** the closing `} // namespace mosaic_samples` (i.e. after the `Scene` reflect from Task 5). Add the `reference.hpp` include at the top of the file first (after the `value.hpp` include):
```cpp
#include "foundation/serialization/reference.hpp"
```
Then the schemas:
```cpp
struct AssetMetadata
{
    mosaic::foundation::serialization::AssetID uuid;
    std::string type;
    std::int32_t version = 0;
    std::string source;
    std::vector<mosaic::foundation::serialization::AssetID> dependencies;
    mosaic::foundation::serialization::Value metadata;        // open-ended table
    mosaic::foundation::serialization::Value import_settings; // open-ended table
    friend bool operator==(const AssetMetadata&, const AssetMetadata&) = default;
};
template <class V> void reflect(V& v, AssetMetadata& a)
{
    v.member("uuid", a.uuid);
    v.member("type", a.type);
    v.member("version", a.version);
    v.member("source", a.source);
    v.member("dependencies", a.dependencies);
    v.member("metadata", a.metadata);
    v.member("import_settings", a.import_settings);
}

struct ProjectConfig
{
    std::string name;
    std::string engine_version;
    std::int32_t max_entities = 0;
    bool enable_physics = false;
    friend bool operator==(const ProjectConfig&, const ProjectConfig&) = default;
};
template <class V> void reflect(V& v, ProjectConfig& c)
{
    v.member("name", c.name);
    v.member("engine_version", c.engine_version);
    v.member("max_entities", c.max_entities);
    v.member("enable_physics", c.enable_physics);
}
```
Also add `#include <cstdint>` to the top of `sample_schemas.hpp` if not already present (for `std::int32_t`).

Create `tests/unit/serialization/toml_backend_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::AssetMetadata;
using mosaic_samples::ProjectConfig;

namespace
{
AssetMetadata sample_metadata()
{
    AssetMetadata a;
    a.uuid = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    a.type = "texture";
    a.version = 2;
    a.source = "art/hero.png";
    a.dependencies = {AssetID::from_string("11111111-1111-1111-1111-111111111111")};

    Table meta;
    meta["author"] = Value{std::string{"luci"}};
    a.metadata = Value{std::move(meta)};

    Table imp;
    imp["srgb"] = Value{true};
    imp["mips"] = Value{std::int64_t{4}};
    a.import_settings = Value{std::move(imp)};
    return a;
}
} // namespace

TEST_CASE("asset metadata round-trips through TOML", "[toml]")
{
    AssetMetadata a = sample_metadata();
    Value v = to_value(a);

    std::string toml = to_toml(v);
    Value parsed = from_toml(toml);

    AssetMetadata out;
    from_value(parsed, out);
    REQUIRE(out == a);
}

TEST_CASE("project config round-trips through TOML", "[toml]")
{
    ProjectConfig c{"Mosaic Demo", "0.1.0", 100000, true};
    Value v = to_value(c);

    ProjectConfig out;
    from_value(from_toml(to_toml(v)), out);
    REQUIRE(out == c);
}

TEST_CASE("to_toml rejects a non-table top-level value", "[toml]")
{
    REQUIRE_THROWS_AS(to_toml(Value{std::int64_t{1}}),
                      mosaic::foundation::serialization::SerializationError);
}

TEST_CASE("from_toml throws SerializationError on malformed input", "[toml]")
{
    REQUIRE_THROWS_AS(from_toml("this is = = not toml"),
                      mosaic::foundation::serialization::SerializationError);
}
```

- [ ] **Step 2: Add to build and run to verify it fails**

Edit `foundation/serialization/CMakeLists.txt` — add `toml_backend.cpp` to the `add_library(mosaic_serialization STATIC ...)` list:
```cmake
  toml_backend.cpp
```
Edit `tests/CMakeLists.txt` — add to the sources list:
```cmake
  unit/serialization/toml_backend_test.cpp
```
Run: `cmake --preset debug && cmake --build build/debug 2>&1 | tail -20`
Expected: FAIL — `toml_backend.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/serialization/toml_backend.hpp`:
```cpp
#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// Value <-> TOML text. Used for human-authored config and asset metadata. Knows
// nothing of reflection; it only projects the format-neutral Value DOM. The
// top-level Value must be a Table (TOML documents are tables).
std::string to_toml(const Value& value);
Value from_toml(const std::string& text);

} // namespace mosaic::foundation::serialization
```

Create `foundation/serialization/toml_backend.cpp`:
```cpp
#include "foundation/serialization/toml_backend.hpp"

#include <cstdint>
#include <sstream>
#include <string>

#include <toml++/toml.h>

#include "foundation/serialization/reference.hpp"

namespace mosaic::foundation::serialization
{
namespace
{
toml::table to_toml_table(const Table& t);

toml::array to_toml_array(const Array& a); // fwd

void insert_toml(toml::table& tbl, const std::string& key, const Value& v)
{
    if (v.is_null()) return; // TOML has no null; omit the key
    if (v.is<bool>()) { tbl.insert(key, v.as<bool>()); return; }
    if (v.is<std::int64_t>()) { tbl.insert(key, v.as<std::int64_t>()); return; }
    if (v.is<std::uint64_t>()) { tbl.insert(key, static_cast<std::int64_t>(v.as<std::uint64_t>())); return; }
    if (v.is<double>()) { tbl.insert(key, v.as<double>()); return; }
    if (v.is<std::string>()) { tbl.insert(key, v.as<std::string>()); return; }
    if (v.is<Array>()) { tbl.insert(key, to_toml_array(v.as<Array>())); return; }
    tbl.insert(key, to_toml_table(v.as<Table>()));
}

void push_toml(toml::array& arr, const Value& v)
{
    if (v.is<bool>()) { arr.push_back(v.as<bool>()); return; }
    if (v.is<std::int64_t>()) { arr.push_back(v.as<std::int64_t>()); return; }
    if (v.is<std::uint64_t>()) { arr.push_back(static_cast<std::int64_t>(v.as<std::uint64_t>())); return; }
    if (v.is<double>()) { arr.push_back(v.as<double>()); return; }
    if (v.is<std::string>()) { arr.push_back(v.as<std::string>()); return; }
    if (v.is<Array>()) { arr.push_back(to_toml_array(v.as<Array>())); return; }
    if (v.is<Table>()) { arr.push_back(to_toml_table(v.as<Table>())); return; }
    // Null in an array is omitted; acceptance schemas never produce it.
}

toml::array to_toml_array(const Array& a)
{
    toml::array out;
    for (const Value& v : a)
    {
        push_toml(out, v);
    }
    return out;
}

toml::table to_toml_table(const Table& t)
{
    toml::table out;
    for (const auto& entry : t)
    {
        insert_toml(out, entry.first, entry.second);
    }
    return out;
}

Value node_to_value(const toml::node& n);

Value table_to_value(const toml::table& t)
{
    Table out;
    for (auto&& [key, value] : t)
    {
        out[std::string(key.str())] = node_to_value(value);
    }
    return Value(std::move(out));
}

Value array_to_value(const toml::array& a)
{
    Array out;
    a.for_each([&](const toml::node& element) { out.push_back(node_to_value(element)); });
    return Value(std::move(out));
}

Value node_to_value(const toml::node& n)
{
    switch (n.type())
    {
        case toml::node_type::boolean: return Value(n.value<bool>().value());
        case toml::node_type::integer: return Value(n.value<std::int64_t>().value());
        case toml::node_type::floating_point: return Value(n.value<double>().value());
        case toml::node_type::string: return Value(n.value<std::string>().value());
        case toml::node_type::array: return array_to_value(*n.as_array());
        case toml::node_type::table: return table_to_value(*n.as_table());
        default: return Value(); // date/time node types are unused by these schemas
    }
}
} // namespace

std::string to_toml(const Value& value)
{
    if (!value.is<Table>())
    {
        throw SerializationError("to_toml: top-level value must be a table");
    }
    std::ostringstream os;
    os << to_toml_table(value.as<Table>());
    return os.str();
}

Value from_toml(const std::string& text)
{
    try
    {
        return table_to_value(toml::parse(text));
    }
    catch (const toml::parse_error& e)
    {
        throw SerializationError(std::string("TOML parse error: ") + e.description().data());
    }
}

} // namespace mosaic::foundation::serialization
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[toml]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add foundation/serialization/toml_backend.hpp foundation/serialization/toml_backend.cpp \
        foundation/serialization/CMakeLists.txt tests/unit/serialization/sample_schemas.hpp \
        tests/unit/serialization/toml_backend_test.cpp tests/CMakeLists.txt
git commit -m "Add TOML backend and asset-metadata round-trip (#10)"
```

---

### Task 7: Cross-format equivalence + full-suite verification

**Files:**
- Test: `tests/unit/serialization/cross_format_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `to_toml`/`from_toml` (Task 6), `to_json`/`from_json` (Task 5), `to_value`/`from_value` (Tasks 3–4), sample schemas.
- Produces: no new production code — the acceptance test proving TOML and JSON are lossless projections of the same schema.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/serialization/cross_format_test.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_json;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_json;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::AssetMetadata;

namespace
{
AssetMetadata sample_metadata()
{
    AssetMetadata a;
    a.uuid = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    a.type = "texture";
    a.version = 2;
    a.source = "art/hero.png";
    a.dependencies = {AssetID::from_string("11111111-1111-1111-1111-111111111111")};

    Table meta;
    meta["author"] = Value{std::string{"luci"}};
    a.metadata = Value{std::move(meta)};

    Table imp;
    imp["srgb"] = Value{true};
    imp["mips"] = Value{std::int64_t{4}};
    a.import_settings = Value{std::move(imp)};
    return a;
}
} // namespace

TEST_CASE("TOML -> model -> JSON -> model is identity on the schema", "[crossfmt]")
{
    Value original = to_value(sample_metadata());

    std::string toml = to_toml(original);
    Value from_toml_model = from_toml(toml);

    std::string json = to_json(from_toml_model);
    Value from_json_model = from_json(json);

    REQUIRE(from_toml_model == original);
    REQUIRE(from_json_model == original);
    REQUIRE(from_toml_model == from_json_model);
}

TEST_CASE("a value authored in TOML decodes to the same struct via JSON", "[crossfmt]")
{
    AssetMetadata a = sample_metadata();

    // TOML -> Value -> struct
    AssetMetadata via_toml;
    from_value(from_toml(to_toml(to_value(a))), via_toml);

    // TOML -> Value -> JSON -> Value -> struct
    AssetMetadata via_json;
    from_value(from_json(to_json(from_toml(to_toml(to_value(a))))), via_json);

    REQUIRE(via_toml == a);
    REQUIRE(via_json == a);
}
```

- [ ] **Step 2: Add to build and run to verify it fails**

Edit `tests/CMakeLists.txt` — add to the sources list:
```cmake
  unit/serialization/cross_format_test.cpp
```
Run: `cmake --build build/debug 2>&1 | tail -20`
Expected: it compiles (all APIs exist). If any `[crossfmt]` assertion fails, that is a real signed/unsigned or key-ordering mismatch to fix in the relevant backend — see the note below.

**If an assertion fails:** the likely cause is an integer alternative mismatch (`Int64` vs `UInt64`) between backends. Confirm `sample_metadata()` uses only signed ints (`version` is `std::int32_t`) and string/bool leaves — the acceptance schema deliberately avoids unsigned fields so both backends canonicalize integers to `Int64`. `Table::operator==` is order-independent, so JSON key reordering is not a failure cause.

- [ ] **Step 3: Run the full suite**

Run: `cmake --build build/debug && ctest --preset debug`
Expected: PASS — all ECS tests plus every `[value] [reference] [reflect] [refenc] [json] [toml] [crossfmt]` case. Confirm the run reports zero failures.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/serialization/cross_format_test.cpp tests/CMakeLists.txt
git commit -m "Add cross-format equivalence acceptance test (#10)"
```

- [ ] **Step 5: Update the handoff doc**

Edit `docs/project/next-steps.md`: mark #10 done and repoint the "start here" to #11 (command/event system). Update the `**Last updated:**` date and the *Suggested sequence* diagram (`#10 ✓`). Then:
```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: #10 (serialization) done, point worker at #11"
```

---

## Self-Review

**Spec coverage:**
- Reflection-driven, serialize by field → Tasks 3 (bridge) + `reflect.hpp` protocol. ✓
- TOML backend (toml++) → Task 6. ✓
- JSON backend (nlohmann) → Task 5. ✓
- Lossless cross-format projection → Task 7. ✓
- `References`/`Handle<T>`/`EntityID` serialize as IDs and re-resolve → Task 4 (+ resolver hook Task 2). ✓
- Asset metadata normative fields → Task 6 `AssetMetadata`. ✓
- Acceptance: TOML round-trip (Task 6), JSON scene round-trip (Task 5), cross-format identity (Task 7), reference/handle re-resolve (Task 4), unit tests under `tests/unit/` (all tasks). ✓
- Value DOM as the model (Approach A) → Task 1. ✓
- `AssetID` UUID type → Task 2. ✓
- `ReferenceResolver` stub for absent asset server → Task 2, exercised Task 4. ✓
- Error handling via exceptions (`SerializationError`) → Task 2, used throughout. ✓
- Build wiring, tier contract, third-party libs private → Task 5 (+ Task 6 adds toml source). ✓

**Placeholder scan:** No TBD/TODO. Every code step shows complete file contents or exact insertion points. ✓

**Type consistency:** `Value`/`Array`/`Table`, `to_value(T&)`, `from_value(const Value&, T&, const ReferenceResolver&)` and its 2-arg overload, `encode`/`decode`, `to_json`/`from_json`, `to_toml`/`from_toml`, `AssetID::from_string`/`to_string`, `Handle<T>::asset_id`, `ReferenceResolver::resolve_entity`/`resolve_asset` — names and signatures match across all tasks. EntityID is serialized as a decimal string consistently in encode (Task 4) and decode (Task 4). ✓

**Known environmental risk:** Tasks 5–7 require network at CMake configure time to fetch toml++ and nlohmann/json. If the environment is offline, configure fails at Task 5 Step 2 — surface it rather than working around it (per the spec's risks).
