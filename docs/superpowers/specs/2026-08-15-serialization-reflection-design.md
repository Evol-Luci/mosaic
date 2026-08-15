# Design: Reflection + Serialization (#10)

- **Date:** 2026-08-15
- **Issue:** [#10 — Implement serialization: TOML/JSON reflection round-trip](https://github.com/Evol-Luci/mosaic/issues/10)
- **Implements:** [RFC 0001 — Core Data Model](../../rfcs/0001-core-data-model.md) (Accepted), *Serialization stance* + *Component contract*.
- **Tier:** `foundation/` only. Depends on nothing above it; honors the
  [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md) tier contract.

## Summary

Add a minimal, forward-compatible **reflection layer** and a **serialization layer**
to `foundation/`. Components describe their fields once via an ADL `reflect` function;
that single description drives conversion to a generic **`Value` DOM**, which two thin
text backends project to/from **TOML** (config/asset metadata) and **JSON** (scenes).
`References`/`Handle<T>`/`EntityID` serialize as their IDs and re-resolve at load through
a resolver hook. No hand-written, per-type or per-format serializers — as RFC 0001
requires.

## Motivation

RFC 0001 makes serialization a lossless projection of a single reflected schema:
"both [TOML and JSON] are lossless projections of the same reflected schema; a value
authored in one can be re-emitted in the other without semantic change." #10 is the
first code to realize that stance. The ECS core (#9) now supplies real components to
round-trip. There is no full reflection system yet — only a `ComponentTypeID` identity
shim — so this design also stands up the minimal field-reflection seam the full system
will later reconcile with.

## Design decisions (settled during brainstorming)

1. **Reflection mechanism: ADL visitor + member pointers.** One `reflect(V&, T&)` free
   function per type, discovered via ADL. Backend-agnostic; a single description feeds
   every consumer. Chosen over macro registries (macro-heavy, premature type erasure)
   and per-type/per-backend traits (forbidden by the RFC).
2. **Format bridge: intermediate `Value` DOM (Approach A).** Reflection converts a type
   to/from one generic `Value` tree; TOML and JSON backends only convert `Value ⇆ text`.
   The DOM *is* "the model," so cross-format equivalence is structural, not merely tested.
   Chosen over direct per-backend visitors (two impls to keep in lockstep).
3. **Type breadth: acceptance + refs.** Scalars, nested reflected structs,
   `std::vector<T>`, and identity/reference types (`EntityID`, `AssetID`, `Handle<T>`).
   Maps/optional/variant/enums deferred (YAGNI) until a real component needs them.

## Architecture

```text
   component type  ──reflect()──►  to_value / from_value  ──►  Value DOM
                                                                  │
                                          ┌───────────────────────┴──────────────────────┐
                                          ▼                                               ▼
                                  toml_backend (toml++)                          json_backend (nlohmann)
                                          ▼                                               ▼
                                    TOML text                                        JSON text
```

Only `to_value`/`from_value` are reflection-aware. Backends know nothing but `Value`.

### 1. Reflection layer — `foundation/reflection/`

Header-only, part of `mosaic_foundation`.

- **`reflect.hpp`** — the opt-in protocol. A type provides a free function found by ADL:
  ```cpp
  template <class V> void reflect(V& visitor, Transform& t) {
      visitor.member("position", t.position);
      visitor.member("rotation", t.rotation);
      visitor.member("scale",    t.scale);
  }
  ```
  Provides a `has_reflection<T>` concept. The visitor is a *concept* (duck-typed template
  parameter), not a base class — each consumer supplies its own visitor type. The
  visitor's `member(name, ref)` dispatches on the **static** member type:
  - scalar (`bool`, integral, floating, `std::string`) → leaf
  - `has_reflection<T>` → recurse
  - `std::vector<T>` → sequence of elements
  - `EntityID` / `AssetID` / `Handle<T>` → reference encoding (see §4)

  Non-const overloads for reading (mutating the object), const for writing. This is the
  seam the future full reflection system reconciles with, mirroring the `ComponentTypeID`
  shim note in `docs/design/reflection.md`.

### 2. Value DOM + generic bridge — `foundation/serialization/`

- **`value.hpp`** — `Value`, a variant tree:
  `Null | Bool | Int64 | UInt64 | Double | String | Array(std::vector<Value>) |
  Table(ordered map<std::string, Value>)`. The table preserves insertion order so output
  is stable and diff-friendly. `Value` has value equality (drives the equivalence test).
- **`to_value.hpp` / `from_value.hpp`** — the two visitors that walk `reflect` and
  build/consume a `Value`:
  - `Value to_value(const T&)` — a const visitor whose `member` writes into a `Table`.
  - `void from_value(const Value&, T&)` — a visitor whose `member` reads from a `Table`.
  These are the only reflection-aware code in the serialization layer.

### 3. Text backends

- **`toml_backend.{hpp,cpp}`** — `Value ⇆ TOML` via **toml++**. For config/asset metadata.
- **`json_backend.{hpp,cpp}`** — `Value ⇆ JSON` via **nlohmann/json**. For scenes.
  (nlohmann over simdjson: simdjson is parse-only; we need emit too.)

Both are pure `Value`↔text adapters with no reflection knowledge. TOML has no distinct
null; an absent key represents `Null` on the TOML side (documented in the backend).

### 4. Identity & references — `foundation/serialization/reference.hpp`

- **`AssetID`** — 128-bit UUID (`std::array<std::uint8_t, 16>`), with canonical
  `to_string`/`from_string` (`8-4-4-4-12` hex). A `nil` constant. No RNG/generation for
  #10 (sample UUIDs are literals in tests).
- **`Handle<T>`** — carries an `AssetID` plus a runtime index/generation that is **never**
  serialized. Serializes as its `AssetID` string; re-resolves at load via the resolver.
- **`EntityID`** — encodes as its 64-bit value; re-mapped through the resolver on load
  (stands in for the future World entity-remap).
- **`ReferenceResolver`** — a small hook interface the read path calls to re-resolve
  `AssetID`/`EntityID` at load. Default is an identity resolver. A test stub proves
  re-resolution actually runs. This stands in for the not-yet-built asset server / World
  remap; when those land, the resolver interface is where they plug in.

### 5. Sample schemas (drive acceptance tests)

- **`AssetMetadata`** — normative RFC fields: `uuid (AssetID)`, `type (string)`,
  `version (int)`, `source (string)`, `dependencies (std::vector<AssetID>)`,
  `metadata (Value::Table)`, `import_settings (Value::Table)`. The two open-ended maps are
  modeled as pass-through `Value` (a reflected field of type `Value` copies through the DOM
  unchanged).
- **`ProjectConfig`** — a flat-ish config struct for the second TOML round-trip.
- **`Scene`** — `std::vector<SceneEntity>`; `SceneEntity { EntityID id; components:
  Value::Table (typeName → Value) }`. Sample components exercise nested fields and a
  reference field: `Vec3 { x,y,z }`, `Quat { x,y,z,w }`, `Transform { Vec3 position; Quat
  rotation; Vec3 scale; }`, `Name { std::string }`, and a component holding a `Handle<T>`
  / `EntityID` reference.

### 6. Build

- Add FetchContent declarations for **tomlplusplus** and **nlohmann/json** in the root
  `CMakeLists.txt` (alongside Catch2).
- New compiled static library **`mosaic_serialization`** under `foundation/serialization/`
  (its own `CMakeLists.txt`, added via `add_subdirectory`). Links `mosaic_foundation` +
  the two third-party libs. Reflection stays header-only inside `mosaic_foundation`
  (include-only).
- Add `tests/unit/serialization/*` sources to `mosaic_unit_tests`; link
  `mosaic_serialization`.

## Data flow

**Write:** `AssetMetadata a → to_value(a) → Value v → toml_backend::to_string(v) → TOML`.
**Read:** `TOML → toml_backend::parse → Value v → from_value(v, a) → AssetMetadata a`,
then `resolver` re-resolves any `AssetID`/`EntityID` fields.
**Cross-format:** `TOML → Value v1`, `json_backend::to_string(v1) → JSON → Value v2`;
`v1 == v2` by construction of the shared DOM.

## Error handling

- Parse failures (malformed TOML/JSON) surface as a thrown/`expected` error from the
  backend; the exact mechanism (exception vs `std::expected`) is a plan-level detail, kept
  consistent across both backends.
- `from_value` type mismatches (e.g., a string where a number is expected) are reported,
  not silently coerced.
- `AssetID::from_string` rejects malformed UUIDs.
- Missing required fields on read: reported; open-ended `Value::Table` fields default to an
  empty table.

## Testing (TDD; maps 1:1 to issue acceptance)

- **TOML round-trip** — `AssetMetadata` and `ProjectConfig`: `x == read(write(x))`.
- **JSON round-trip** — `Scene` (array of entities, component maps, nested fields).
- **Cross-format equivalence** — `TOML→Value→JSON→Value` is identity on the schema.
- **Reference/handle** — `AssetID`/`Handle<T>`/`EntityID` fields serialize as IDs and
  re-resolve through a stub resolver.
- **Reflection units** — `to_value`/`from_value` for scalars, nesting, and vectors.
- **`AssetID`** — `to_string`/`from_string` round-trip and rejection of malformed input.

Each unit lives under `tests/unit/serialization/`, following the existing
`tests/unit/ecs/` layout and Catch2 conventions.

## Risks & open items

- **Network at configure time.** toml++ and nlohmann/json are fetched via FetchContent,
  which needs network at CMake configure (same as Catch2 today). If the build environment
  is offline this is a blocker to surface, not work around.
- **Reflection is a shim.** This minimal `reflect` protocol is deliberately not the full
  reflection system (`docs/design/reflection.md`). When the full system lands, `reflect`
  and `ComponentTypeID` are reconciled together. Flag if it becomes a blocker rather than
  silently diverging (per the next-steps ground rules).
- **`EntityID`/`AssetID` resolution is stubbed.** The `ReferenceResolver` proves the
  re-resolution seam runs, but real asset-server and World-remap resolvers are future work.

## Out of scope

Binary "cooked" runtime resources (RFC mentions them; deferred), the full reflection
system, the asset server/Resource lifecycle, and the World/Scene *runtime* load path
(this issue serializes the Scene *asset*, it does not instantiate into a World).
