# Networking N3 — state replication (snapshot + client upsert) — Design

## Goal (Phase 4, Networking pillar — slice 3)

The core of server-authoritative networking: the server serializes replicated **component
state** into a snapshot; a client **applies** it, mirroring the server's entities. Reuses
the existing reflection + JSON serialization layers (`ComponentReflection` +
`to_json`/`from_json`) — exactly the "network replication" the reflection design
anticipated.

A `NetReplicated { NetId }` marker tags server entities to replicate. `snapshot()` emits
each such entity's net id + all its reflected components as JSON; `apply_snapshot()`
upserts them into a client World keyed by net id (**create-on-first-sight**, then
update-in-place). Proven **headless and deterministic** with two Worlds (bytes passed
directly in-test).

**Scope (YAGNI):** generic reflected-component snapshots for `NetReplicated` entities,
JSON-encoded, client upsert keyed by `NetId`. **No** `NetworkSystem` wiring (auto-send
each tick), **no** despawn/removal, **no** delta/binary/compression, **no**
interpolation/prediction — all later (N3-follow send-wiring, N4 despawn + net-id
assignment, optimization slices).

## Design

### `engine/net/net_replicated.hpp` — marker + net id

```cpp
#pragma once

#include <cstdint>

namespace mosaic::engine::net
{
using NetId = std::uint32_t; // stable network identity of a replicated entity (0 = unset)

// Server-side marker: tag an entity to replicate, with its stable net id. Reflected +
// registered as "NetReplicated" (authorable in scenes/inspector). The marker itself is
// NOT replicated — the net id is the snapshot key. N4 formalizes net-id assignment; for
// now the id is set by whoever creates the entity.
struct NetReplicated
{
    std::uint32_t id = 0;
};

template <class V>
void reflect(V& visitor, NetReplicated& n)
{
    visitor.member("id", n.id);
}
} // namespace mosaic::engine::net
```

Registered `"NetReplicated"` in `builtin_components.hpp`.

### `engine/net/replication.hpp` — snapshot + apply

```cpp
// Server: JSON snapshot of every NetReplicated entity's reflected components (the marker
// excluded — net id is the key). Shape:
//   { "entities": [ { "net": <u>, "components": { "<Name>": <Value>, ... } }, ... ] }
std::string snapshot(mosaic::engine::World& world,
                     const mosaic::engine::reflection::ComponentReflection& types);

// Client: stable NetId -> local EntityID map (create-on-first-sight).
struct NetIdMap
{
    std::unordered_map<NetId, mosaic::engine::World::EntityID> map;
};

// Client: apply a snapshot — for each entity node, find-or-create its local entity via the
// map, then set_value() each component (upsert). Returns false on malformed JSON/shape;
// never throws.
bool apply_snapshot(const std::string& blob, mosaic::engine::World& world,
                    const mosaic::engine::reflection::ComponentReflection& types,
                    NetIdMap& ids);
```

**`snapshot`** (`for_each<NetReplicated>`): for each entity, a node `{ "net":
uint64(nr.id), "components": Table }`; the components table = for each
`types.registered_names()` **except `"NetReplicated"`**, `types.component_value(world, e,
name)` — insert the present ones. Root `{ "entities": Array }` → `to_json`. Uses `Value`
`Table`/`Array` builders exactly like `scene::to_value`.

**`apply_snapshot`**: `try { root = from_json(blob); } catch (...) { return false; }` (a
malformed blob throws `SerializationError`). Then guard the shape with `is<Table>` /
`is<Array>` (return false on mismatch). For each entity node: read `net` (accept **either
`std::uint64_t` or `std::int64_t`** — a JSON round-trip may narrow an unsigned to signed),
`local = ids.map[net]` if present else `world.create_entity()` recorded into `ids.map`;
then for each `{name, value}` in the node's `components` table, `types.set_value(world,
local, name, value)` (upsert — update-in-place or add). Return true.

The `NetIdMap` is **client-held state** (in tests, the test owns it; a client system owns
it when send-wiring lands). The marker is server-side only — the client's mirror entity is
identified by the map, and does **not** get a `NetReplicated` component.

### Data flow (two Worlds, in-test)

```
server: entity + NetReplicated{1} + Transform + Material
        snapshot(server, types) -> JSON bytes
client: apply_snapshot(bytes, client, types, ids)
        -> ids.map[1] = new client entity; Transform/Material set to match
server moves the entity; snapshot again
client: apply -> ids.map[1] hit -> SAME entity updated (upsert), no duplicate
```

## Testing (headless, deterministic — two Worlds, bytes passed directly)

New unit tests under `tests/unit/net/` (build a `TypeRegistry` + `ComponentReflection` via
`register_builtin_components`, which registers `Transform`, `Material`, and the new
`NetReplicated`):

- **Single entity replicates** — server: one entity with `NetReplicated{1}` +
  `Transform{pos=(1,2,3)}` + `Material{color=(…)}`; `snapshot` → `apply_snapshot` to a
  fresh client World. Assert: the client has exactly one entity; its `Transform.position`
  and `Material.base_color` match the server's; it has **no** `NetReplicated` component.
- **Idempotent upsert** — re-apply the same blob → still one client entity. Then move the
  server entity, re-`snapshot`, re-`apply` → the client entity's `Transform` updates and
  it is **still the same single entity** (map hit, no duplicate).
- **Multiple + selectivity** — two `NetReplicated` entities (ids 1, 2) plus a
  **non-`NetReplicated`** server entity → the client mirrors ids 1 and 2 only (two client
  entities; the unmarked one is absent).
- **Malformed / empty** — `apply_snapshot("not json", …)` returns false, no crash;
  `apply_snapshot("{}", …)` (no `entities`) returns false; a snapshot of an empty server
  world applies cleanly (zero client entities), returns true.
- **`NetReplicated` reflects + registers** — round-trip via reflection; `types.has(
  "NetReplicated")`.

All in-process; no sockets/`NetworkSystem`; deterministic.

## Files

**New**
- `engine/net/net_replicated.hpp` — `NetId`, `NetReplicated`.
- `engine/net/replication.hpp` — `snapshot`, `NetIdMap`, `apply_snapshot`.
- `tests/unit/net/replication_test.cpp`.

**Modified**
- `engine/reflection/builtin_components.hpp` — register `NetReplicated`.
- `tests/CMakeLists.txt` — new test source.

## Non-goals / follow-ups

- **Send-wiring:** a `ReplicationSystem` where the server builds+sends a snapshot each tick
  over the N2 `NetworkSystem`/`Transport` and the client applies received snapshots (keyed
  by session) — a small follow-up now that the mechanism exists.
- **N4:** entity despawn/removal (entity gone on the server → removed on the client),
  authoritative spawn/despawn messages, and server-side `NetId` assignment policy.
- **N5:** client → server input/commands.
- Later: delta snapshots, binary `Value` codec + compression, relevancy/interest
  management, interpolation/prediction, per-component replication opt-in.
