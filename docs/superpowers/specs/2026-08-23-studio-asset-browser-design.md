# Studio Asset Browser — Browse + Assign (design)

**Tracking issue:** [#37 Studio shell](https://github.com/Evol-Luci/mosaic/issues/37) (post-v0.1 Studio work).
**Builds on:** #20 asset registry, #18 reflection, 37b (panel/selection patterns), 37c
(reflection inspector + `FieldVisitor`), 37d/37e (editor viewport + panels).
**Date:** 2026-08-23.

## Summary

An **Assets** panel that lists the registered assets and lets you **assign** a selected
asset to a component's `AssetID` field — e.g. swap the cube's `Material.texture` and watch
the viewport update. This is the reflected-property principle (18) extended to *references*:
the same path a human uses in the inspector is the one an agent would use. It was deferred
as post-v0.1 Studio work in the [studio decomposition](2026-08-22-studio-decomposition.md);
this is its own brainstorm → spec → plan → TDD slice.

## Scope

**In:**
- `AssetRegistry` **enumeration** (list registered assets; the registry today only does
  lookup-by-id).
- A **mutable asset field** in the reflected-property surface: `FieldVisitor::field_asset`,
  with the adapter routing `AssetID` fields to it (was read-only).
- A headless **`AssetBrowserModel`** (asset list + selected id).
- An **Assets panel** (list + click-to-select), and inspector integration (an "Assign"
  affordance on `AssetID` fields that writes the browser's selection).
- **Three procedural demo textures** registered in `build_demo_scene` so assignment is
  demonstrable.

**Deferred (post-slice / YAGNI):** type-aware filtering (a texture field cannot advertise
that it wants a texture — reflection carries no per-field type, so assignment is
unrestricted for now; a mismatch degrades to the default white texture via `TextureCache`),
asset thumbnails/previews, importing assets from disk, folders/search/rename, and a
clear-to-nil / remove action.

## Architecture

Studio-tier panel plus small `engine/` additions, mirroring 37b/37c. **Tier contract
holds:** the browser model and panel live in `studio/`; `engine/` gains only neutral,
ImGui-free reflection/registry surface; `foundation/`/`engine/` never depend on `studio/`.
No new third-party dependencies.

### 1. `AssetRegistry` enumeration (engine — `engine/assets/asset_registry.hpp`)

The registry exposes `contains`/`metadata(id)`/`load<T>` but no way to list what is
registered. Add:

```cpp
// Invoke fn(const AssetMetadata&) for every registered asset, in unspecified order.
template <class F>
void for_each(F&& fn) const
{
    for (const auto& [id, entry] : entries_)
    {
        fn(entry.metadata);
    }
}

std::size_t size() const { return entries_.size(); }
```

Iteration order is unspecified (it is a hash map); the browser model sorts for a stable UI
(see below). No behavioral change to existing lookups.

### 2. Mutable asset field in the reflected-property surface (engine — `engine/reflection/field_visitor.hpp`)

`FieldVisitor` today presents `AssetID` read-only. Add a **mutable** visitor entry:

```cpp
// A reference-to-asset field. The visitor may reassign it (e.g. from an asset browser).
// Default falls back to the read-only display so existing visitors keep compiling.
virtual void field_asset(std::string_view name, mosaic::foundation::serialization::AssetID& value)
{
    field_readonly(name, value.to_string());
}
```

And in `FieldVisitorAdapter::member`, route the `AssetID` branch to it:

```cpp
else if constexpr (std::is_same_v<U, ser::AssetID>)
{
    visitor.field_asset(name, value);   // was: field_readonly(name, value.to_string())
}
```

Making `field_asset` **non-pure with a read-only default** keeps every other `FieldVisitor`
implementer (and the 37c tests) compiling unchanged; only `ImGuiFieldVisitor` overrides it.
`Handle<R>` fields stay read-only for this slice (they carry a resource type; unrestricted
reassignment is less safe — a follow-up).

### 3. `AssetBrowserModel` (studio — headless)

```cpp
struct AssetEntry
{
    mosaic::foundation::serialization::AssetID id;
    std::string                                type;   // AssetMetadata::type
    std::string                                source; // AssetMetadata::source (may be empty)
};

// The asset browser's list + selection. Rebuilt each frame from the registry via
// refresh(); selection persists across refreshes while the id still exists.
class AssetBrowserModel
{
public:
    void refresh(const mosaic::engine::assets::AssetRegistry& registry); // repopulate, sorted by id string
    const std::vector<AssetEntry>& entries() const;

    void select(mosaic::foundation::serialization::AssetID id);
    bool has_selection() const;
    mosaic::foundation::serialization::AssetID selected() const; // nil AssetID if none
};
```

`refresh` collects entries via `registry.for_each`, sorts by `id.to_string()` for a stable
order, and drops the selection if the selected id is no longer present. Unit-tested.

### 4. Assets panel + inspector assignment (studio — `studio/studio_app.{hpp,cpp}`, `studio/inspector.*`)

- `StudioApp` gains an `AssetBrowserModel assets_;` (+ `assets()` accessor) and registers an
  `"Assets"` panel in the layout.
- **Assets panel body** (`draw_assets`): `assets_.refresh(registry)` then a `Selectable` per
  entry labelled `"<type>  <short-id>"` (or `source` when present); click sets
  `assets_.select(id)`. The registry reaches the studio the same way the world/reflection do
  — passed into `draw_ui` (the editor already owns the `AssetRegistry`).
- **Inspector assignment:** `ImGuiFieldVisitor` is constructed each frame with the browser's
  current selection; its `field_asset` override renders the field's current id (read-only
  label) plus an **"Assign"** button — enabled only when the browser has a selection — that
  writes `selected()` into the live field. Assignment is unrestricted (any id).

### Data flow

```
Assets panel: click asset  ->  AssetBrowserModel.select(id)  (StudioApp owns it)
Inspector: AssetID field "Assign" button  ->  field_asset writes selected() into
    Material.texture (live component)
Next frame: RenderSystem -> TextureCache.get(Material.texture, registry, device)
    resolves the new AssetID (uploaded + cached once) -> viewport shows the new texture
```

No new render plumbing: `TextureCache` already resolves `Material.texture` through the
registry each frame, so reassigning the id is sufficient.

### Demo assets (`app/run/demo_scene.hpp`)

`build_demo_scene` registers **three** procedural `Image` textures (distinct `AssetID`s +
`type = "texture"`): the existing checkerboard, plus e.g. a solid tint and a stripes/gradient
pattern. The cube's `Material.texture` starts on the checkerboard; the other two are
assignable targets. `mosaic run` is unaffected (it just gains two unused registered assets).

## Testability boundary

Consistent with prior render/studio slices; GUI verified functionally (no screenshot).

- **Unit-tested (no window):**
  - `AssetRegistry::for_each`/`size`: enumerate registered metadata; count matches; empty
    registry yields nothing.
  - The adapter's mutable asset path: a fake `FieldVisitor` capturing `field_asset` receives
    a mutable `AssetID` and reassigning it through the visitor mutates the source field
    (reflect → adapter → assign round-trip). Confirm non-`AssetID` fields are unaffected and
    the default `field_asset` still renders read-only.
  - `AssetBrowserModel`: `refresh` populates sorted entries; `select`/`selected`/
    `has_selection`; selection dropped when the id vanishes after a `refresh`.
- **Verified functionally:** `mosaic editor` launches, exits 0, 0 validation errors; the
  Assets panel lists the demo textures and assigning one to the cube's `Material.texture`
  changes the texture in the viewport (maintainer confirms visually).

## Ground rules honored

- **Tier contract:** studio depends on engine/foundation; engine reflection/registry stay
  ImGui-free; only the studio panel/visitor touch ImGui.
- **Reflected-property surface is the one editing path** — assignment goes through
  `FieldVisitor::field_asset`, not a `Material`-specific editor, so humans and the future
  agent API share it.
- **No new third-party dependencies.** Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] `AssetRegistry::for_each`/`size` unit tests green.
- [ ] Mutable `field_asset` reflection-path unit test green (assign round-trip; default
      read-only fallback intact).
- [ ] `AssetBrowserModel` unit tests green (refresh/sort/select/selection-drop).
- [ ] `mosaic editor` shows an Assets panel listing the demo textures; assigning one to the
      cube's `Material.texture` updates the viewport; launches, exits 0, 0 validation errors.
- [ ] Full unit suite green.

## Depends on / relates

- **Depends on:** #20 asset registry (enumeration + resolution), #18 reflection
  (`FieldVisitor`), 37c inspector (the `field_asset` extension), 37d viewport (to see the
  change), the `TextureCache` (resolves the reassigned id).
- **Relates:** the deferred type-aware filtering would build on per-field type metadata in
  reflection; the agent/automation API shares the `field_asset` assignment path.
