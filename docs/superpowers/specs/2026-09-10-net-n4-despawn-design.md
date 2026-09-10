# Networking N4 — despawn / removal replication — Design

## Goal (Phase 4, Networking pillar — slice N4)

Complete the replication protocol: when a replicated entity is removed on the server, it is
removed on the client too. N3 gave create-on-first-sight + update-in-place; N4 adds the
third case — **remove-on-absence**. Because every snapshot is the **full** authoritative set
of `NetReplicated` entities, the latest snapshot is ground truth: any `NetId` the client is
tracking that is **absent** from the newest snapshot has despawned and its local entity is
destroyed. No new message type, no server-side change tracking, no explicit despawn messages.

Everything stays **pure engine + headless**, proven deterministically with two in-process
Worlds linked by `make_loopback_pair()` and with direct `apply_snapshot` calls.

## Design

### Architecture

`apply_snapshot` becomes **authoritative**: applying a snapshot makes the client World match
it exactly — add (create-on-first-sight, N3), update (`set_value` upsert, N3), and now
**remove** any tracked entity missing from the snapshot. The client `ReplicationSystem` path
(N3-follow) gains despawn for free, since it already calls `apply_snapshot`. The server and
`snapshot()` are unchanged.

### `engine/net/replication.hpp` — removal in `apply_snapshot`

Current behavior: parse the blob; for each entity node, find-or-create the local entity via
`NetIdMap` and `set_value`-upsert its components; return `true` (or `false` on malformed
JSON/shape, never throwing).

New behavior adds removal, **only on the success path**:

1. While iterating the entity nodes, record each node's `NetId` into a local
   `std::unordered_set<NetId> seen`.
2. After **all** nodes have been applied cleanly (if any node is malformed, the function
   returns `false` exactly as today and performs **no** removal — a rejected snapshot never
   mutates membership), reconcile: iterate `ids.map`, collect every key **not** in `seen`
   into a `std::vector<NetId> to_remove`, then for each such key call
   `world.destroy_entity(ids.map[key])` and erase the key from `ids.map`. (Collect first,
   erase second — never mutate the map while iterating it.)
3. Return `true`.

The set-membership key is the `NetId` read from each node (the existing uint64/int64 dual
accept in N3 is unchanged). `apply_snapshot`'s signature is unchanged:

```cpp
bool apply_snapshot(const std::string& blob, World& world,
                    const mosaic::engine::reflection::ComponentReflection& types,
                    NetIdMap& ids);
```

Add `#include <unordered_set>` and `#include <vector>` as needed.

### Deferred-destroy nuance

`World::destroy_entity` is **deferred** — the entity's components and slot are committed at
the **end of the tick**, after systems run (`world.cpp`). Consequences, both correct:

- When the client `ReplicationSystem` calls `apply_snapshot` during its `update` (a system
  step), each `destroy_entity` commits at the end of **that same client tick** — so after the
  tick the despawned entity is gone (`for_each_entity` no longer visits it, `get_component`
  returns null).
- The `ids.map` erase is **immediate** (not deferred). So `ids.map` reflects the new
  membership right after `apply_snapshot` returns, and a later snapshot that re-introduces
  the same `NetId` creates a **fresh** local entity via create-on-first-sight.

For a **direct** `apply_snapshot` unit test (no surrounding system/tick), the caller must
`world.tick(...)` once after applying to commit the deferred destruction before asserting
entity counts; the `ids.map` size can be asserted immediately.

### Data flow (loopback)

```
server: entities A(net 1) + B(net 2) replicated      -> client mirrors both, ids.map = {1,2}
server: destroy_entity(B)                             -> subsequent snapshot = {entities:[A]}
client: apply_snapshot -> seen = {1}; ids.map = {1,2}; key 2 absent ->
        destroy_entity(local_B) + erase ids.map[2]    -> after tick: client has only A, map={1}
```

### Backward compatibility & error handling

- All N3 `apply_snapshot` tests still pass: re-applying the same blob or a multi-entity blob
  records every tracked id in `seen`, so nothing is removed; the empty-world blob
  (`{entities:[]}`) removes nothing when the map is already empty.
- A malformed or partial snapshot returns `false` and performs **no** removal — membership is
  only reconciled against a snapshot that parsed cleanly in full.
- `destroy_entity` on a stale/already-invalid handle is safe — `World` guards with
  `is_valid` at commit time.

## Testing (headless, deterministic)

- **Direct despawn** (extend `tests/unit/net/replication_test.cpp`) — apply a two-entity
  blob (nets 1 and 2) to a fresh client World + `NetIdMap`; assert the map has 2 keys. Then
  apply a one-entity blob (net 1 only); assert **immediately** that `ids.map` has exactly one
  key (2 erased). Then `world.tick(0.016f)` to commit the deferred destruction and assert the
  client has exactly **one** entity, and that the surviving entity is the one that carried
  net 1's components (e.g. its `Transform.position` matches).
- **Direct: clean snapshot still removes nothing** (extend `replication_test.cpp`, or assert
  within the N3 idempotent case) — re-applying the same multi-entity blob leaves `ids.map`
  and entity count unchanged (regression guard for the reconcile path).
- **End-to-end despawn over the loopback** (extend `tests/unit/net/replication_system_test.cpp`)
  — server with two `NetReplicated` entities (nets 1 and 2); pump both Worlds so the client
  mirrors both (count == 2). Then `server_world.destroy_entity(<net-2 entity>)` and pump
  again; assert the client has exactly **one** entity, that it is the net-1 entity (its
  `Transform` matches), and that no stale second entity remains.

All in-process, no sockets, deterministic.

## Files

**Modified**
- `engine/net/replication.hpp` — removal/reconcile logic in `apply_snapshot` (+ `<unordered_set>`).
- `tests/unit/net/replication_test.cpp` — direct despawn + clean-snapshot-removes-nothing cases.
- `tests/unit/net/replication_system_test.cpp` — end-to-end despawn case.

(No new files — a focused change to `apply_snapshot` plus tests.)

## Non-goals / follow-ups

- **N4b:** server-side `NetId` assignment policy (the server stamps a unique monotonic id on
  `NetReplicated` entities created with `id == 0`, so game/script code need not hand-pick ids).
- Explicit despawn messages / delta snapshots — unnecessary with full snapshots; a later
  optimization concern only.
- **N5:** client → server input/commands.
- Real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later
  maintainer-verified slice.
