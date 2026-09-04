# Scripting API: events (emit + on_event hook) — Design

## Problem

Scripts can't communicate via events — no way for one script to signal another,
or to react to named game events. The engine has a capable event bus with a
**deferred (one-tick), FIFO** discipline, but it's typed (`emit<E>` /
`subscribe_deferred<E>`), so scripts (which speak dynamic name+payload) can't
use it directly.

## Decisions (maintainer-approved)

- **Delivery = an `on_event(name, payload)` hook** the script optionally defines
  (like `on_update`). `ScriptSystem` drains and calls it — no Python callbacks
  stored in the engine (no lifetime/threading/re-entrancy hazards).
- **Scope = script↔script custom events** this slice. Bridging engine
  `ContactEvent`s into scripts is a fast follow-up (small once the hook exists).

## Design

A neutral dynamic event rides the existing deferred bus.

### 1. `engine/script/script_event.hpp` (new)

```cpp
#pragma once
#include <string>
#include "foundation/serialization/value.hpp"
namespace mosaic::engine::script
{
// A dynamic, named event scripts emit() and receive via on_event(name, payload).
// Rides the World event bus (deferred: delivered exactly one tick later, FIFO).
struct ScriptEvent
{
    std::string                              name;
    mosaic::foundation::serialization::Value payload; // null when omitted
};
} // namespace mosaic::engine::script
```

### 2. `emit(name, payload=None)` global — `bind_world` (Python backend)

```cpp
globals_["emit"] = py::cpp_function(
    [this](const std::string& name, py::object payload)
    {
        world_->events().emit(
            mosaic::engine::script::ScriptEvent{name, py_to_value(payload)});
    },
    py::arg("name"), py::arg("payload") = py::none());
```

Inline mode only (needs `world_`). The threaded `bind_context` sandbox has no
`emit` (would touch the bus off-thread) — consistent with its get/set-only rule.

### 3. Delivery — `ScriptSystem::update_inline`

- **Subscribe once** (lazily, first `update_inline`): `world.events().subscribe_deferred<ScriptEvent>([this](const ScriptEvent& e){ pending_events_.push_back(e); })` guarded by a `bool subscribed_`. The deferred handler fires at each tick's drain (before systems), queueing that tick's delivered events (i.e. emitted the previous tick).
- **Deliver**, at the top of `update_inline`: move `pending_events_` into a local and clear the member. Then, in the existing per-entity loop (after the engine is ensured, before `on_update`), for each event:
  ```cpp
  entry.engine->set_global("__evt_name", Value(ev.name));
  entry.engine->set_global("__evt_payload", ev.payload);
  entry.engine->run_string(
      "if 'on_event' in globals(): on_event(__evt_name, __evt_payload)\n");
  ```
  Broadcast: every scripted entity that defines `on_event` receives every event; scripts filter by `name`. Undefined `on_event` is a guarded no-op (no error spam). `entity` is already bound per-engine, so `on_event` can act on its own entity.

Timing: emit in tick N → deferred drain at tick N+1 → queued → delivered at the top of `ScriptSystem`'s tick-N+1 update, before `on_update`. Exactly one tick, deterministic (the bus's documented discipline).

**Threaded mode:** `on_event` is not delivered (the worker runs against a snapshot; `emit` isn't in the sandbox). Documented, consistent with the other threaded-mode limitations.

### Lifetime note

The deferred subscriber captures `this` (the `ScriptSystem`). `World` owns both
the bus and the systems; nothing emits during teardown, so the handler never
fires after the system dies. Same pattern as `setup_runtime`'s `ContactEvent`
logger.

## Testing (headless)

1. **`emit` publishes** (`script_events_test.cpp`): bind a `World`; a C++
   `subscribe<ScriptEvent>` (sync) handler; script `emit('hit', {'dmg': 5})` →
   handler sees `name=="hit"` and `payload` table with `dmg==5`. Also
   `emit('bare')` → null payload.
2. **`on_event` delivery** (`script_system_test`): entity A's `on_update` calls
   `emit('ping', {'v': 1.0})`; entity B defines `on_event` that writes
   `payload['v']` into B's Transform.x on `name=='ping'`. After **2** ticks
   (emit tick 1 → deliver tick 2), B's `Transform.x == 1.0`. Proves the full
   emit→deferred→on_event path via `ScriptSystem`.

## Out of scope / follow-ups

- **ContactEvent → `'contact'` script-event bridge** (the gameplay payoff) — next slice.
- Targeted (per-entity) delivery / subscriptions by name; event filtering in-engine.
- Events in threaded mode.
