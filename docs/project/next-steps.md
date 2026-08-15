# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-08-15

## Where we are

- Docs, architecture, and the licensing/CLA groundwork are in place.
- **[RFC 0001: Core Data Model](../rfcs/0001-core-data-model.md) is Accepted.** The
  four questions that gated the foundation are settled (see the RFC's *Resolved
  decisions*), so the data-model portion of **Phase 0 — Foundation** is unblocked
  and ready to implement.

Nothing in `foundation/` or `engine/` is built yet. The next work is the first
code against the accepted model.

## What to build next

Three implementation issues are open, in the order to tackle them. All are part of
**Phase 0 — Foundation** and the **[v0.1 vertical slice](roadmap.md#the-v01-vertical-slice--the-first-mountain)**.

### 1. ECS core — [#9](https://github.com/Evol-Luci/mosaic/issues/9)  ← start here

`foundation/` + `engine/ecs/`. Everything else needs entities and components to
exist first.

- `EntityID`: 64-bit generational handle, **32 index / 32 generation** (index in
  the low half). `EntityID(0)` is the null handle. Slot is **retired**, not
  recycled, on generation exhaustion.
- Slot table with free-list recycling; generation bump on recycle; stale-handle
  detection.
- Component storage keyed by `EntityID`; components are plain reflected data.
- Systems *declare* the components they read/write (scheduling/threading is later).

### 2. Serialization — [#10](https://github.com/Evol-Luci/mosaic/issues/10)

`foundation/serialization/`. Depends on #9 (needs real components to round-trip).

- Reflection-driven, serialize **by field** — no hand-written serializers.
- **TOML** (toml++) for config/asset metadata; **JSON** for nested scenes.
- Both are lossless projections of the same schema; cross-format round-trip is an
  identity on the model.
- `References`/`Handle<T>` serialize as `AssetID`/`EntityID` and re-resolve at load.

### 3. Command / event system — [#11](https://github.com/Evol-Luci/mosaic/issues/11)

`foundation/commands/` + `foundation/events/`. Can proceed in parallel with #10
once #9 gives it entities to mutate.

- Commands: `id, parameters, required_permissions, execute(), undo(), serialize()`;
  the undoable transaction log.
- Events: synchronous in-tick by default; **double-buffered per-frame drain** for
  deferred subscribers (FIFO, one tick after emit). Deferred events emitted by a
  command enqueue on *execute*, not on undo/redo.

## Suggested sequence

```
#9 ECS core ──┬── #10 Serialization
              └── #11 Command/event system
```

Start with **#9**. Then #10 and #11 can go in either order (or in parallel);
#10 has the harder dependency on the component representation, so if working
solo, do #10 next while the ECS shape is fresh, then #11.

## Ground rules for the worker

- Honor the tier/dependency contract in
  [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): `foundation/` knows nothing
  above it; `engine/` depends only on `foundation/`.
- Each issue lists acceptance criteria and expects unit tests under `tests/unit/`.
- Reflection metadata is a soft dependency for #9/#10; a minimal shim is acceptable
  until the full reflection system lands. Flag it if it becomes a blocker.
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC.

## After these three

With entities, serialization, and commands/events in place, the next foundation
pieces (asset registry, World/Scene runtime, transform system, the runtime loop)
become buildable — the on-ramp to **Phase 1 — First World**. Reassess and update
this file when #9–#11 close.
