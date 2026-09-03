# Studio Script Editor — in-editor script hot-reload (design)

**Phase:** 3 (Programming) — the first hot-reload slice.
**Builds on:** the Scripting pillar S1–S5 + the runtime integration: `ScriptComponent`
(`engine/script/script_component.hpp`), `ScriptSystem` (recompiles on source change),
`ScriptEngine` (neutral), the Studio shell (#37), `SelectionModel`.
**Date:** 2026-09-02.

## Summary

A **Script Editor** panel in `mosaic editor`: select a scripted entity, edit its `ScriptComponent`
source in a multiline box, press **Apply** — the panel **test-compiles** the new source and shows a
green "compiled ✓" or a red error message, and on success writes it into the component, which makes
`ScriptSystem` **recompile it live**. This turns the recompile-on-source-change seam that already
exists (S4) into an actual "edit the script, watch the behavior change live" experience — script
hot-reload, in-editor. It is the first Phase-3 hot-reload slice; external `.py`-file editing is the
documented next step.

## Scope

**In:**
- A pure-logic `ScriptEditorModel` (`studio/script_editor.hpp`) that **validates** a source string
  (test-compile in a throwaway engine) and, on success, **writes** it into a selected entity's
  `ScriptComponent`.
- A **Script Editor** Studio panel that edits the *selected* entity's script with validate-on-Apply
  and inline OK/error feedback; live reload via `ScriptSystem`'s existing recompile.

**Deferred (documented — the "both eventually" the user asked to record):**
- **External `.py` files:** a `ScriptComponent` referencing a `.py` file (a path or a `.py`-`AssetID`)
  that reloads on disk change (a file poll / watcher feeding the same recompile seam). The headline
  future hot-reload workflow (edit in your own editor, save, live). Not in this slice.
- **Attaching a script to an unscripted entity** (a "New Script" action) — needs the deferred
  *generic add/remove-component in the Inspector* feature; this slice only edits entities that
  already have a `ScriptComponent`.

## Architecture

### 1. `ScriptEditorModel` (`studio/script_editor.hpp`) — Python-free logic, no ImGui

Holds only the injected engine factory; the panel owns the edit buffer.

```cpp
namespace mosaic::studio
{
// The outcome of a validate-on-apply.
struct ScriptApplyResult
{
    enum class Kind { None, Ok, Error };
    Kind        kind = Kind::None;
    std::string message; // captured stdout on Ok (usually empty); the error text on Error
};

class ScriptEditorModel
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ScriptEditorModel(EngineFactory factory = {}) : factory_(std::move(factory)) {}

    // Test-compile `source` against a throwaway engine bound to the live world (exactly as
    // ScriptSystem compiles a script). On success, write `source` into entity `e`'s ScriptComponent
    // (→ ScriptSystem recompiles it live next tick) and return Ok. On a compile/top-level error,
    // return Error{error} and leave the component untouched. Never throws.
    ScriptApplyResult apply(const std::string& source, mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection,
                            mosaic::foundation::EntityID e);

private:
    EngineFactory factory_;
};
}
```

`apply` mirrors `ScriptSystem`'s compile step exactly:

```cpp
if (!factory_) return {Kind::Error, "scripting is not available in this build"};
auto engine = factory_();
engine->bind_world(world, reflection);
engine->set_global("entity", Value(static_cast<std::uint64_t>(e.value())));
const ScriptResult r = engine->run_string(source);      // compiles + runs module top-level
if (!r.ok) return {Kind::Error, r.error};               // syntax / top-level error -> DO NOT write
ScriptComponent* sc = world.get_component<ScriptComponent>(e);
if (sc == nullptr) return {Kind::Error, "selected entity has no ScriptComponent"};
sc->source = source;                                    // commit -> ScriptSystem recompiles live
return {Kind::Ok, r.output};
```

### 2. The panel (`draw_script_editor` in `studio/studio_app.{hpp,cpp}`)

- Reuses `SelectionModel` (`selection_`). If `!selection_.has_selection()` or the selected entity has
  no `ScriptComponent` → a hint: *"Select a scripted entity to edit its script."* Nothing else.
- **Selection sync:** when the selected scripted entity differs from the last one loaded
  (`script_edit_entity_`), copy its `source` into the edit buffer and clear the last result. (Editing
  then switching selection loads the newly-selected script; unsaved edits to the previous are
  dropped — acceptable for v0.1, documented.)
- Renders `ImGui::InputTextMultiline("##script_edit", script_edit_buf_, …)`, an **Apply** button →
  `script_apply_result_ = script_editor_.apply(script_edit_buf_, world, reflection, e)`, and the
  result: green **"compiled ✓"** (Kind::Ok) or a red error line (Kind::Error). Core ImGui only (no
  `imgui_stdlib`), a fixed `char` buffer — same pattern as the Script Console.
- New `StudioApp` members: `ScriptEditorModel script_editor_` (constructed with a **copy** of the
  same injected factory the console uses), `char script_edit_buf_[8192] = {}`,
  `std::optional<mosaic::foundation::EntityID> script_edit_entity_`,
  `ScriptApplyResult script_apply_result_`. Ctor passes a **copy** of the factory to each
  (`script_console_(script_factory), script_editor_(script_factory)`) — order-independent and safe
  (the factory is a no-capture lambda; don't `std::move` into one and leave the other empty). A
  `"Script Editor"` panel added to `StudioLayout`'s defaults and dispatched in the `draw_ui` panel
  loop (beside the Inspector/Console).

### 3. Validation semantics (the honest limits)

- **Catches:** syntax errors and any error in the script's **top-level** code (imports, `def`, module
  init), because `run_string(source)` compiles and runs the module body.
- **Does not catch:** logic errors **inside** `on_update` — it is only *defined* during validation,
  not called; those surface at runtime (unchanged from today).
- **No new semantics vs. `ScriptSystem`:** the engine already runs a script's top-level with the real
  world bound on every recompile, so validation behaves identically — a script that mutates the world
  at top level does so during validation exactly as it would on any recompile. Convention (documented
  in the panel/hint): keep world mutation inside `on_update`.
- **Live reload resets per-entity script state:** `ScriptSystem`'s recompile recreates the engine, so
  module globals (e.g. the orbit demo's `_t`) restart at 0 — expected for a reload.
- **Play-state:** the validation result shows **immediately** on Apply (independent of play state);
  the *behavior* change (recompile) applies on the next `World::tick`, i.e. while **Play** is running
  (or on a Step). Documented in the panel copy.

## Data flow

```
select a scripted entity  ->  panel loads sc.source into script_edit_buf_
edit the buffer  ->  Apply
  -> ScriptEditorModel.apply: factory_() -> bind_world -> set_global("entity") -> run_string(source)
       error?  -> ScriptApplyResult{Error, msg};  component untouched;  red line in the panel
       ok?     -> write source into ScriptComponent;  {Ok};  "compiled ✓"
next World::tick (Playing)  ->  ScriptSystem sees source changed  ->  recompiles  ->  new behavior live
```

## Error handling

- Never throws into the UI: `run_string` never throws (S1); a null factory → `Error{"scripting is
  not available in this build"}`; a selected entity without a `ScriptComponent` → the hint (the panel
  never calls `apply` in that state), and `apply` itself guards with an `Error` if called anyway.
- A compile error **does not** modify the component — the running behavior is unchanged until a valid
  Apply, so a bad edit can't break the live script.

## Testability

- **`ScriptEditorModel` unit** (`tests/unit/studio/script_editor_test.cpp`; the unit-test exe already
  links `mosaic_studio` + `mosaic_script_python`):
  - Apply a **valid** source (`def on_update(dt): pass`) to an entity with a `ScriptComponent` →
    `Kind::Ok`; `get_component<ScriptComponent>(e)->source` equals the new source.
  - Apply a **syntax-error** source (`def on_update(dt) ???`) → `Kind::Error`, `message` contains
    `"SyntaxError"`, and the component's source is **unchanged** (validation rejected the write).
  - Apply to an entity **without** a `ScriptComponent` → `Kind::Error`.
  - A **null-factory** model → `Kind::Error` ("not available").
- The ImGui `draw_script_editor` glue is thin and verified by the build + full suite green + the
  user's visual acceptance (edit the orbit cube's script in `mosaic editor`, Apply, watch it change
  while Playing). `mosaic run --window` smoke unaffected.

## Ground rules honored

- **Tier wall:** `studio/` stays Python-free — `ScriptEditorModel` uses the neutral `ScriptEngine` +
  a `std::function` factory + the reflected `ScriptComponent`; CPython enters only via the
  app-injected factory (`app/run/editor.cpp`), the same seam as the Script Console.
- **Reuses existing seams:** `SelectionModel` (selection), `ScriptComponent` (the edited data),
  `ScriptSystem` (the live recompile) — no new engine mechanism; the model/draw split matches
  `PlayState`/`ScriptConsoleModel`.
- No new third-party deps. Catch2 names avoid a leading `--`. EntityID crosses as `uint64` for the
  `entity` global.

## Acceptance

- [ ] `ScriptEditorModel.apply` validates a source (test-compile) and writes it into the selected
      entity's `ScriptComponent` only on success — unit-tested (ok / syntax-error-rejected /
      no-component / null-factory).
- [ ] A Script Editor panel edits the selected scripted entity's source with an Apply button and
      inline green-OK / red-error feedback; a bad edit never modifies the live script.
- [ ] Applying a valid edit changes the entity's behavior live on Play (via `ScriptSystem` recompile);
      full suite green.
- [ ] The external-`.py`-file and attach-to-unscripted-entity extensions are recorded as deferred.

## Depends on / relates

- **Depends on:** S1–S5 (`ScriptEngine`/`ScriptComponent`/`ScriptSystem`), the runtime integration
  (`ScriptSystem` in `setup_runtime`), `SelectionModel`, the Studio shell. **Relates:** the deferred
  *generic add/remove-component in the Inspector* (would let this panel attach a script to an
  unscripted entity) and the external-`.py`-file hot-reload (the next slice, same recompile seam).
