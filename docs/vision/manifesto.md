# The Mosaic Manifesto

> Build worlds.
>
> Model them.
> Program them.
> Simulate them.
> Animate them.
> Render them.
> Play them.
> Film them.
> Automate them.
>
> The editor is a tool.
> The runtime is a tool.
> The language is a tool.
> AI is a tool.
>
> No single workflow is mandatory.
>
> Everything should be inspectable.
> Everything should be scriptable.
> Everything should be replaceable.
> Everything should be extensible.

## The thesis

Mosaic is a **programmable 3D operating environment** — a place where the engine,
editor, content pipeline, scripting system, and automation layer all operate on
the same underlying project model.

Its defining rule:

> **The user and the agent are simply two different clients of the same
> underlying systems.**

A human clicking a button, a Python script, a CLI invocation, and an AI agent all
reach the *same* command and automation APIs. There is no separate "AI mode"
bolted onto the side — automation is native, and it is just as useful **without**
AI as with it.

## What this implies

- **No mandatory workflow.** You can drive Mosaic entirely from the GUI, entirely
  from code, entirely from the CLI, or any mix. None is privileged.
- **Everything is inspectable.** Every meaningful action has a structured
  representation. State is data you can read, not pixels you must interpret.
- **Everything is replaceable.** Blender, Box3D, the renderer backend, the
  scripting language — each sits behind an interface and can be swapped.
- **Everything is extensible.** The plugin architecture matters more than any
  single built-in feature; the community grows the platform, not just the core
  team.
- **The tools are peers, not a hierarchy.** The editor is a tool. The runtime is a
  tool. The language is a tool. AI is a tool. Remove any one and the rest still
  stand.

## What Mosaic is closer to

Not a clone of any single product. Conceptually it is nearer to:

> **Blender + Unreal + Godot + VS Code + Houdini + Git + an automation API**

…with the deliberate constraint that the human and the agent are two clients of
one set of systems. That is a more interesting target than "open-source Unreal,"
and it is the project worth spending years on.
