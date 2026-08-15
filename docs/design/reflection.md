# Reflection

Reflection may be the single most strategically important subsystem in Mosaic. It
is the reason the platform can be **human-friendly and agent-friendly without
building two separate systems.**

## The idea

Every type that carries data gets metadata describing its fields:

```cpp
struct Transform
{
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};
```

Reflection knows:

```text
Transform
 ├── position : Vec3
 ├── rotation : Quaternion
 └── scale    : Vec3
```

## One source of truth, many consumers

From that single metadata description, the engine generates — automatically —
everything below:

```text
Inspector UI
Serialization
Python bindings
Documentation
Undo / redo
Automation
Agent tools
Network replication
Animation tracks
Editor search
```

Without reflection, each of those is a hand-maintained mapping that drifts out of
sync. With reflection, adding a field to a component makes it appear in the
inspector, serialize correctly, become scriptable from Python, become animatable,
and become manipulable by an agent — all at once.

## Why this is the strategic investment

Two properties fall out of it directly:

1. **Humans and agents share one API.** An inspector edits a reflected property; a
   Python script sets the same reflected property; an agent issues a command that
   targets the same reflected property. There is no parallel "agent data model."
2. **The foundation stops needing rewrites.** New components and assets plug into
   serialization, tooling, and automation for free, so the core does not have to
   grow a special case each time a subsystem is added.

Reflection underpins the [data model](../architecture/data-model.md), the
[command/event system](command-event-system.md), and the
[automation API](automation-and-agents.md). It belongs early in the roadmap
precisely because so much depends on it.
