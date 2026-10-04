# Deferred Work Backlog

A point-in-time survey, originally compiled 2026-09-19 immediately after
Phase 6 (Agent ecosystem) completed, closing out every phase
`docs/project/roadmap.md` currently defines. **Refreshed 2026-09-19** after
the project-health/bulk-editing arc (all 4 sub-projects) shipped, to mark
what that arc closed out and fold in what it surfaced along the way — see
"Refresh notes" at the end of the Agent API section. Originally compiled by
grepping `docs/superpowers/specs/*.md`'s "Non-goals" sections (a convention
every spec in this repo has followed since Phase 6 began),
`docs/superpowers/plans/*.md`, `docs/design/*.md`, `docs/architecture/*.md`,
and `docs/project/next-steps.md` for "deferred"/"future work"/"not solved
here"/"accepted limitation"/"non-goal"/etc.

**This is a snapshot, not a live index.** Items here may already be
picked up, superseded, or stale by the time you read this — cross-check
against `next-steps.md`'s most recent entries and open PRs before treating
anything below as still-accurate. Re-run the survey (grep the same sources)
rather than trust this file blindly once significant time has passed.

## How to read this

Grouped first by subsystem (what area of the engine/tooling it touches),
then within each subsystem area a rough effort tier is noted. See the
**Effort vs. impact** section below for the cross-cutting prioritization
view that was actually used to pick what to build next as of this writing.

## Agent API / MCP (Phase 6 — complete, these are follow-on extensions)

- ~~**Wrap remaining CLI verbs as MCP tools**~~ — decomposed 2026-09-19
  into one-tool-per-slice sub-projects (this bundled 7-8 separate
  wrappers, too big for one slice): `plugin_list` **SHIPPED** (MCP
  tool 28), `plugin_install`/`plugin_remove` **SHIPPED** (MCP tools
  29-30), `plugin_source_{add,list,remove,pull}` **SHIPPED** (MCP
  tools 31-34). The last item, a `mosaic docs`-regeneration tool, was
  **dropped** (not built) after discovering `mosaic docs` always
  regenerates the Mosaic *engine repo's own* `docs/reference/`
  (`MOSAIC_DOCS_DIR` is baked in at compile time from
  `CMAKE_SOURCE_DIR`) -- completely independent of whatever project a
  `mosaic mcp <project-dir>` session is serving. Every other MCP tool
  here is meant for an agent building a *game* with Mosaic; this one
  would only ever matter to an agent doing self-hosted Mosaic engine
  development (like this session) -- a scope mismatch with the
  server's actual purpose, confirmed with the user rather than built
  anyway. **With this, the whole arc is closed** (3 of its 4 slices
  shipped, the 4th correctly dropped). *Source:*
  `2026-09-19-project-maintenance-build-project-design.md`
- ~~**Verify AssetID referencing via `set_component`**~~ — **CONFIRMED**
  2026-09-20, no code change needed. `2026-09-18-asset-generation-
  import-asset-design.md`'s Non-goals speculated this was "arguably
  already possible" through the existing generic `set_component` tool
  but left it unverified. It works exactly as speculated: `set_value`'s
  default `ReferenceResolver::resolve_asset` is a plain identity
  passthrough (`foundation/serialization/reference.hpp`), so a bare
  `AssetID` field accepts any UUID string verbatim, no registry
  validation at set-time (matching `broken_references`'s own
  validate-on-read design, not a gap). Already extensively exercised
  by existing tests (`mcp_tools_test.cpp`'s `broken_references`/
  `orphaned_assets` test suites both use `set_component` on
  `Material.texture` as their own setup, both with stray and
  real-registered ids) -- re-confirmed end-to-end via the real CLI:
  imported a real PPM asset, `set_component`'d `Material.texture` to
  its id, `get_component` read it back correctly, `broken_references`
  did not flag it. *Source:* `2026-09-18-asset-generation-import-
  asset-design.md`
- ~~**Spatial queries**~~ — **SHIPPED** 2026-09-20, `entities_near`/
  `scene_bounds`, MCP tools 35-36. Computes each entity's world
  position live via `TransformSystem::compute_world()` (made public)
  rather than the cached `WorldTransform`, which never gets
  recomputed in a headless `mosaic mcp` session after startup and
  would have been silently stale for anything moved mid-session.
  *Source:* `2026-09-16-scene-reasoning-describe-scene-design.md`
- ~~**Asset-usage queries**~~ — **SHIPPED**, `entities_referencing`, MCP
  tool 26. "What entities reference asset X", via a third `FieldVisitor`
  subclass (`AssetReferenceCollector`) alongside `AssetRefCollector`
  (`broken_references`) and `ReferencedAssetGatherer` (`orphaned_assets`),
  both in `app/mcp/mcp_tools.cpp`. Classified bounded, not architectural
  — no spec/plan doc, agreed in chat and implemented directly. *Source:*
  `2026-09-16-scene-reasoning-describe-scene-design.md`.
- **Generic criteria-based search (query DSL)** — "find entities where
  field Y matches Z". Most general/powerful, needs its own decomposition.
  *Source:* same
- ~~**Studio plugin (`.py`) authoring via MCP**~~ — **SHIPPED** 2026-09-20
  (MCP tools 37-39, PR #193). `plugin_script_write` validates via a
  throwaway `PluginHost` load (a scratch directory holding only the
  candidate file, bound to the real live World) before persisting to
  the project's real `plugins/`; `plugin_script_read`/`plugin_script_list`
  round out the loop. *Source:*
  `2026-09-16-code-generation-set-script-design.md`
- ~~**Undo support for `set_script`**~~ — **SHIPPED** 2026-09-20.
  `ScriptEditorModel::apply()` now applies via `SetComponentCommand`
  (the same mechanism `set_component` uses) instead of a raw field
  mutation, so undo restores the entity's entire prior
  `ScriptComponent` snapshot. *Source:* same
- **`ScriptEditorModel::debug()` exposure** — compile + run `on_update(0)`
  with a debugger attached, no write. *Source:* same
- **Procedural asset generation** — generate texture/mesh/audio content
  from scratch (not import). No existing machinery, needs its own
  decomposition. *Source:*
  `2026-09-18-asset-generation-import-asset-design.md`
- ~~**Confirm `AssetID` referencing via `set_component`**~~ — **RESOLVED**,
  no longer deferred. Exercised repeatedly and directly during the
  project-health arc's manual verification (`broken_references`,
  `orphaned_assets`, `bulk_set_field` all set `Material.texture` to a
  literal `AssetID` string via `set_component` against a real `mosaic mcp`
  session) — confirmed working, no gap remains.
- ~~**Hot-reload imported assets into the live `AssetRegistry` mid-session**~~
  — **SHIPPED**. `import_asset` now reruns `load_project_assets()` on
  success, threaded through a new `handle_tool_call` pointer parameter.
  `broken_references`/`orphaned_assets`/`entities_referencing` see a
  same-session import immediately, no restart. *Source:*
  `2026-09-18-automated-testing-tick-design.md`.
- ~~**"Tick until condition met" tool**~~ — **SHIPPED** as `tick_until`
  (MCP tool 27), reusing `entities_where`'s exact matching primitives.
  *Source:* same
- **Broader per-system profiling** — physics/animation/particles timing,
  not just scripts. *Source:*
  `2026-09-19-optimization-script-profile-design.md`
- ~~**Bulk refactoring / project health checks**~~ — **SHIPPED, all 4
  sub-projects complete** (`entities_where`, `broken_references`,
  `orphaned_assets`, `bulk_set_field` — MCP tools 22-25). No longer
  deferred; kept here only as the historical pointer. *Source:*
  `2026-09-19-project-maintenance-build-project-design.md`. Specs:
  `2026-09-19-project-health-{entities-where,broken-references,
  orphaned-assets,bulk-set-field}-design.md`.
- **Multi-scene project support in MCP**, **concurrent MCP session locking
  on `save_scene`** — both explicitly unaddressed. *Source:*
  `2026-09-16-agent-api-mcp-server-design.md`

**Refresh notes (2026-09-19, post-arc):**
- ~~The `FieldVisitor` extension~~ — **SHIPPED**, along with both of its
  consumer follow-ups except one. `field_entity`/`field_handle_asset`
  exist (`engine/reflection/field_visitor.hpp`), both read-only, both
  defaulting to the exact prior `field_readonly` output — zero behavior
  change for any existing visitor. `broken_references`/`orphaned_assets`/
  `entities_referencing` cover `Handle<R>` references, closing that
  blind spot. `broken_references` also now detects a non-nil `EntityID`
  field that fails `world.is_valid()` (verified end-to-end against a
  real destroyed entity via `Transform.parent`) — every `"broken"` entry
  now carries a `"kind"` discriminator (`"asset"`/`"entity"`).
  Precise field type labels in component reference docs also **SHIPPED**
  — with this, every follow-up the `FieldVisitor` extension enabled is
  now closed out.
- The `AssetRegistry`-staleness gap ("hot-reload imported assets... mid-
  session") was hit and explicitly worked around three separate times
  this arc (`broken_references`, `orphaned_assets`, and implicitly by
  every tool that reads `asset_registry`) — each time by documenting the
  limitation in the affected tool's own schema description rather than
  fixing it. Same underlying gap, now with three call sites' worth of
  concrete motivation and lived experience with its exact failure mode.

## Plugin ecosystem

- **Native `.so`/`.dll` dynamic-library plugins** — repeatedly deferred
  across all 3 Plugin SDK slices; ABI stability, cross-platform loader,
  sandboxing are the blockers.
- ~~**Per-plugin enable/disable + manifest/versioning**~~ — **shipped**
  2026-09-21 (PR #194). ~~**Live file-watching/hot-reload**~~ — **shipped**
  2026-09-21 (PR #195); Studio no longer needs a manual "Reload Plugins"
  click for edits made while the editor is open. **User-global
  (non-project) plugins** — still unbuilt.
- **Public marketplace** — hosting, moderation pipeline, static/dynamic
  analysis review layers. *Status:* design-only
  (`docs/design/marketplace.md`), explicitly "not yet scheduled."
- **Package manager: network fetch, version constraints/dependency
  resolution, remote registry, templates-as-packages, transitive deps** —
  all deferred, local-only today.

## Documentation tooling

- ~~CI/pre-commit wiring for `mosaic docs` + nav regen~~ — **SHIPPED** as
  a local pre-commit hook (`.githooks/pre-commit`, opt-in via
  `git config core.hooksPath .githooks`). Caught real staleness in
  `docs/index.html`'s nav on its first run. **Scope note:** this repo
  has no CI at all (no `.github/workflows/`), so only the pre-commit
  half was built — standing up actual CI is a separate, larger item,
  see below.
- **No CI exists in this repo at all** — discovered while shipping the
  pre-commit hook above. `CONTRIBUTING.md` claims the `Foundation →
  Engine → Editor → Studio` dependency rules are "enforced in CI," but
  there is no `.github/workflows/` (or any other CI config) anywhere —
  that enforcement doesn't actually run anywhere today. Standing up a
  first CI workflow (running the test suite, then the dependency-rule
  check, then possibly the same docs/nav staleness check the local
  hook covers) is real, undone work, not a stale doc claim to just
  delete — scope TBD (a GitHub Actions build of this project needs a
  full C++ toolchain + Vulkan/GLFW deps, non-trivial to set up).

## UI pillar (declared "COMPLETE" at U5b, but with real gaps)

- Hover enter/exit + visual feedback, keyboard/gamepad navigation, text
  input fields, pointer capture/drag, z-order-aware overlap resolution,
  editor-mode interaction provider.
- Multi-line/word-wrap text, kerning, non-ASCII/Unicode, SDF/multi-size
  atlases.
- Aspect-preserving image fit/fill, 9-slice scaling, texture atlas
  sub-rects.
- Inherited visibility/alpha, clipping/scroll regions, flow/grid/stack
  auto-layout.
- World-space interactive UI (non-billboard labels, occlusion testing,
  distance fade).

## Networking pillar (declared "COMPLETE" at N7, but with real gaps)

- NAT traversal, encryption, compression (ENet doesn't provide these
  either).
- Reconnect/backoff/session resumption on drop, protocol-version
  negotiation.
- Delta/dirty snapshots, unreliable snapshot channel,
  interpolation/prediction, relevancy/interest management, per-component
  replication opt-in.
- Authoritative/anti-cheat input validation, sequence numbers, rate
  limiting.
- Real internet-facing dedicated-server story (firewalling, matchmaking) —
  explicitly "out of scope for the foreseeable future."
- Testing beyond loopback (genuinely separate machines).

## Blender Live-Link

- Mesh/geometry sync (currently transform-only).
- Windows support (Winsock) — Linux-only, though the socket layer is
  isolated for this.
- `CommandStack`/undo integration for live-link edits.
- Per-entity change detection/send throttling, multiple simultaneous
  Blender connections.

## Rendering

- RenderGraph / multi-pass structure — deferred since the point-light
  slice, never picked up (two-pass submission chosen instead each time it
  came up). **Shadows (below, in scoping as of 2026-09-21) will bring the
  editor's viewport path to 3 direct-submission passes** (shadow depth →
  offscreen viewport → swapchain blit) — still not being used as the
  trigger to build the graph for real (confirmed with the user: extend
  direct submission again for shadows' v1), but this is now the closest
  the project has come to the "≥3-pass slice" bar named as the graph's
  actual trigger back at 37d/46. Worth re-raising with the user the next
  time a *dynamic* pass need shows up (conditional/reorderable passes,
  not just "one more fixed pass"), since that's the harder case direct
  submission genuinely doesn't scale to.
- ~~**Shadows — v1 (directional-only, hard-edged, fixed-size ortho
  frustum)**~~ — **SHIPPED 2026-09-21.** See `docs/project/next-steps.md`
  for the full shipment entry (a real editor-pass-ordering bug found by
  functional testing, not by review, and fixed with a ruling). The
  upgrades below remain real, explicitly-deferred follow-ups, each its
  own future slice, not silently folded in:
  - **PCF / soft shadow filtering** — v1 ships hard-edged (single depth
    sample) shadows deliberately, to prove the pipeline (light-space
    matrix, depth pass, sampling) before adding filtering. A small,
    separable follow-up (a few extra texture samples averaged in the
    shader) once v1 is solid.
  - **Point-light shadows (cubemap / 6-pass omnidirectional depth)** —
    v1 is directional-light-only. Point-light shadows are a genuinely
    different technique (cubemap rendering, not a single ortho
    frustum), roughly doubling the scope of v1 — its own future slice.
  - **Scene/camera-fitted (and eventually cascaded) shadow frustum** —
    v1 uses a fixed-size orthographic box centered on the origin,
    correct for the current demo/test scenes but not for scenes bigger
    than that box or not centered near the origin. Fitting the frustum
    to the live scene bounds (similar math to the existing
    `entities_near`/`scene_bounds` MCP tool) or the camera's view
    frustum, and eventually cascaded shadow maps for large open scenes,
    are named, deliberate follow-ups, not built in v1.
  - **Configurable shadow map resolution** — v1 fixes the shadow map's
    resolution as an implementation constant, not a user- or
    project-facing setting. Exposing it (likely alongside a future
    quality/performance settings surface, not on its own) is a named
    follow-up once there's a real place for such a setting to live.
- **"Specular/PBR, spot lights" decomposed 2026-09-21** (confirmed with
  the user before brainstorming any of it): the current lighting model
  is Lambert diffuse only (no specular term at all), single directional
  + single point light ("first found," never accumulated), no shadows
  for point lights. ~~**Spot lights**~~ — **SHIPPED 2026-09-21**, the
  first, smallest slice (reused `PointLight`'s `WorldTransform`-derived
  positional machinery, no material/BRDF changes, no multi-light
  question). See `docs/project/next-steps.md` for the full shipment
  entry. Deferred, each its own future slice, not silently folded into
  spot lights:
  - **Specular highlights (Blinn-Phong)** — a shininess/specular-color
    param added to `Material` and the shader's lighting math, without
    committing to a full PBR material model. Bigger than spot lights
    (touches the whole lighting shader), smaller than full PBR.
    **Unblocked 2026-09-27** — it was blocked on the shading-data UBO
    migration below, which has now shipped. The blocker was that
    Blinn-Phong needed 5 new floats against a `PushConstants` struct
    pinned at the 256-byte ceiling with 4 padding slots left. Post-
    migration that pressure is largely gone: there are 48 bytes of
    per-draw headroom, and because shading is now done in world space
    the camera position is *per-frame* data that belongs in the
    lighting UBO rather than the per-draw payload — so specular's only
    per-draw additions are shininess and strength (8 bytes). This is
    the natural next rendering slice.
  - ~~**Perceptual `0..1` gloss remap**~~ — **resolved 2026-10-04 by PBR
    slice 2.** `Material::shininess` was a raw Phong exponent and a poor
    authoring knob: perceptually non-linear, so 8 → 16 was a large visual
    change while 200 → 208 was invisible. It was deferred 2026-09-27
    because the remap decision belonged with PBR's roughness parameter and
    shipping one earlier would have left two competing gloss scales in the
    material system at once. `roughness` **is** that remap — the deferral
    was resolved by the parameter replacing the exponent outright rather
    than by a second scale being added beside it.
  - **Metallic/roughness texture maps** — per-texel `metallic` and
    `roughness` sampled from a map, rather than the two scalar `Material`
    params. Deferred 2026-09-27 during the Blinn-Phong slice's design,
    when it was framed as shininess and specular-strength maps; PBR
    slice 2 replaced those parameters, so this now reads against
    metallic/roughness and is PBR **slice 3**, decomposed but not yet
    specced. It needs a second texture binding and an asset-pipeline
    answer (how such a map is authored, imported, and referenced) that
    has nothing to do with the shading math itself. It is also the
    reason intermediate `metallic` values exist at all — they are for
    texture blending, not for authoring by hand.
  - ~~**Fresnel / energy conservation on the specular term**~~ —
    **resolved 2026-10-04 by PBR slice 2.** Blinn-Phong v1 was
    deliberately non-energy-conserving: `specular_strength` was an
    authored fudge factor, not a physically derived `F0`. Deferred
    2026-09-27 because a Schlick Fresnel term only makes sense once the
    BRDF around it is physically based; doing it inside Blinn-Phong would
    have been half a PBR model with none of its coherence. Slice 2 ships
    Schlick Fresnel with `F0 = mix(vec3(0.04), base_color, metallic)` and
    `kD = (1 - F)(1 - metallic)`, so diffuse and specular are now
    energy-conserving relative to each other; `specular_strength`
    dissolved rather than migrating. **Still open from this item:** the
    multi-scatter / energy-compensation term — single-scatter GGX loses
    energy at high roughness, and the Fdez-Agüera or LUT correction is a
    refinement on a working BRDF, listed separately below.
  - **The light components' default intensities still mean the pre-π
    thing** — `PointLight::intensity` and `SpotLight::intensity` default
    to `1.0f` and `DirectionalLight::color` to `{1,1,1}`, all authored
    against the non-energy-conserving diffuse term. Since PBR slice 2
    divides diffuse by π, a light a user adds with the defaults now
    renders ~3.14× dimmer than the identical light did before, and a
    "unit" light is no longer a unit of anything. Deferred 2026-10-04:
    the slice-2 spec considered moving the π into the component defaults
    and rejected it with an argued reason — the demo's explicitly-set
    values would still have needed re-authoring, so it would hide where
    the constant went without avoiding the work. Recorded because the
    consequence outlives the decision; the real fix is a defined unit for
    light intensity (lumens or candela), which is its own design
    question. Surfaced by the slice-2 branch review.
  - **`metallic` outside `0..1` gives a material its diffuse lobe back**
    — the engine deliberately does not clamp shading parameters, and
    tests now explicitly guarantee an out-of-range `metallic` reaches the
    GPU unchanged. At `metallic = 2`, `F0 = 2·albedo − 0.04` can exceed 1
    and `kD = (1 − F)(1 − metallic)` flips sign twice to become `F − 1`,
    so a "more than metal" surface is both over-energetic in specular and
    diffuse-lit again. Finite and tone-map-clamped, so it is a wrong
    image rather than a NaN. Deferred 2026-10-04: the spec defines
    `metallic` only on `0..1` and calls intermediate values physically
    meaningless, so there is no stated requirement to violate — but the
    shader already bounds `roughness` on both sides for the same reason,
    and the asymmetry is the kind that reads as an oversight later.
    Depends on nothing; a one-line `clamp` in `cube.frag`. Surfaced by
    the slice-2 branch review.
  - **Per-light specular color distinct from diffuse color** — each
    light currently carries one color used for both terms. Deferred
    2026-09-27: real renderers almost never separate these, and doing
    so would grow the lighting UBO for a knob nobody has asked for.
    Low priority; listed only so the omission is deliberate.
  - **Ambient/environment specular (IBL)** — the flat `0.1` ambient term
    contributes no highlight, and since PBR slice 2 made the ambient
    `kD`-weighted it contributes *nothing whatsoever* to a metal:
    `kD = (1 - F)(1 - metallic)` is exactly zero at `metallic == 1`. The
    demo's metal sphere therefore renders near-black with two small
    specular highlights, which was confirmed visually 2026-10-04 and
    accepted as correct — a metal with nothing to reflect has nothing to
    show. It is the most visible single argument for this item. Deferred 2026-09-27: image-based lighting
    needs cubemap capture, prefiltering, and a BRDF LUT — it is a pillar
    of the PBR work below, not an add-on to Blinn-Phong.
  - **Full PBR material model** — **DECOMPOSED 2026-10-04** into four
    slices; see `docs/superpowers/specs/2026-10-04-pbr-decomposition-and-tone-mapping-design.md`
    for the reasoning and the measured starting state. (1) **Tone
    mapping** — specced in that document and taken first, because an
    energy-conserving BRDF produces values above 1.0 routinely and the
    8-bit sRGB target clips them, so doing the BRDF first would make the
    renderer look *worse* until tone mapping landed. It also pays for
    itself now: specular already clips. (2) **Metallic/roughness plus a
    GGX Cook-Torrance BRDF** with Schlick Fresnel and energy
    conservation — **SHIPPED 2026-10-04.** It *absorbed* the "perceptual
    `0..1` gloss remap" item above (roughness **is** that parameter) and
    the "Fresnel / energy conservation" item. Migration settled as a
    **clean break**: remove `shininess`/`specular_strength` and update
    the committed scene and the test files referencing them, since the
    engine is pre-1.0 with no external consumers and carrying both
    parameter sets is the exact "two competing gloss scales" the remap
    was deferred to avoid. **Correction for slices 3 and 4:** this entry
    and both PBR specs named `scene.json` as the committed scene. It is
    not — `.gitignore` lists it; it is a local export. The committed
    scene is `templates/default/scenes/main.json`, and it holds two
    lights, not one.
    (3) **Texture maps** (metallic/roughness/normal) — needs further
    texture bindings plus an asset-pipeline answer. (4) **Image-based
    lighting** — the largest; the ambient term stays a flat `0.1` until
    it lands. Explicitly outside the decomposition: multi-light
    accumulation (orthogonal — a change to light gathering, not the
    BRDF) and per-light specular colour (already a deliberate omission).
    Each of 2–4 gets its own spec when its turn comes.
  - ~~**Shading-data UBO migration**~~ — **SHIPPED 2026-09-27.**
    Lighting moved into the codebase's first uniform buffer (descriptor
    set 2, one per frame-in-flight) and shading from object space to
    world space; per-draw push constants went 256 → 208 bytes. See
    `docs/project/next-steps.md` for the full shipment entry, including
    the sharpened boundary on the "identical output" claim (exact for
    scale 1; a uniform scale `s != 1` shifts point/spot attenuation
    brightness by `s`, since the old object-space distance input was
    `world_distance / s` — the new behavior is the correct one).
  - **A multi-scatter / energy-compensation term for the GGX specular.**
    Single-scatter GGX loses energy at high roughness — a rough dielectric
    comes out darker than it should, because light that would have
    bounced between microfacets is simply dropped. The correction is
    either the Fdez-Agüera analytic fit or a precomputed LUT. Deferred
    2026-10-04 during the metallic/roughness design: it is a refinement
    on top of a working BRDF, not part of standing one up, and the error
    it fixes is only obvious at roughness values the demo does not use.
  - **Anisotropic roughness.** One scalar `roughness` assumes isotropic
    microfacets, so brushed metal and hair cannot be represented.
    Deferred 2026-10-04: anisotropy needs a per-vertex tangent frame,
    which the vertex format does not carry — so this is a mesh-format
    change before it is a shading change.
  - **Material layers: clear-coat, sheen, transmission.** The
    metallic/roughness workflow is the base model; each layer is its own
    feature with its own parameters and its own BRDF lobe. Deferred
    2026-10-04 as out of scope for standing up the base.
  - **An offscreen HDR render target with a fullscreen resolve pass.**
    The correct-compositing form of tone mapping, and the prerequisite
    for bloom. Deferred 2026-10-04 during the tone-mapping design:
    nothing in the renderer blends additively today — every pipeline in
    `vulkan_render_device.cpp` is either `SRC_ALPHA` (eight of nine, with
    `VK_BLEND_FACTOR_ONE` only as `srcAlphaBlendFactor`) or, for the opaque
    mesh pipeline that actually renders `cube.frag`, `blendEnable = VK_FALSE` — so per-shader tone
    mapping lerps between mapped values, which is a defensible
    approximation rather than an error. The HDR target would cost a
    render target, a render pass, a pipeline, and a decision about where
    UI draws relative to the resolve, for little gain. **The trigger is
    specific, not vague: the moment any additive pipeline is added, the
    reasoning above is invalidated and this becomes necessary** —
    `tonemap(a) + tonemap(b) != tonemap(a + b)`. Related and deliberate:
    the four unlit scene shaders (`particle`, `ribbon`, `decal`,
    `world_text`) are *not* tone mapped, because they cannot exceed 1.0
    and every reasonable curve maps 1.0 to about 0.8 — mapping them
    would darken authored colour for nothing. A test pins that, so a
    later "consistency" edit has to argue with it.
  - **Bloom.** Depends on the HDR render target above; there is nowhere
    to read bright pixels back from until that exists. Deferred
    2026-10-04.
  - **Exposure control.** Tone mapping ships with a hard-coded curve.
    Deferred 2026-10-04 during its design: exposure is a camera concern
    rather than a material or lighting one, and adding the knob now would
    mean deciding where it lives (a `Camera` member? the lighting UBO? a
    push constant?) for no current caller.
  - **Move `view_proj` per-frame and drop `mvp` from push constants** —
    a further per-draw reduction available on top of the UBO migration
    (the vertex shader would compute `view_proj * model` instead of
    receiving a precomputed `mvp`), taking the per-draw payload from
    ~208 bytes to ~144. Deliberately not done in the migration slice:
    that slice's job is the *lighting* data, and the 48 bytes of
    headroom it restores already covers everything queued behind it.
    Worth revisiting only if the per-draw payload comes under pressure
    again.
  - **Real multi-light support** — multiple lights of each type,
    accumulated rather than "first found." Distinct from the UBO
    migration above (which is only about *where* the existing
    single-light-per-type data lives, not how many lights there are);
    the migration is a prerequisite that makes this tractable, but does
    not deliver it. Spot lights inherit the same one-light-per-type
    limitation directional/point already had.

## Project templates

- **3rd-person adventure template content** — needs a new `Health`
  component, character controller, basic combat/AI scripting. Real design
  work, explicitly tracked as deferred.
- Template parameterization (setup wizard, prompted values) beyond
  verbatim copy.
- ~~Remove now-unused `types` param from `create_project`~~ — **SHIPPED**
  2026-09-20, ~40 call sites updated.

## Editor / Studio

- ~~**In-editor "File → Reload"**~~ — **SHIPPED** 2026-09-20. Turned out
  to need two new primitives that didn't exist before: `World::clear()`
  (synchronous, no-system-stepping entity wipe -- `destroy_entity()`
  alone is deferred to the next `tick()`) and `CommandStack::clear()`.
  `reload_scene_from_disk()` (`app/run/scene_load.hpp`) validates via a
  throwaway scratch `World` before touching the live one. A real bug
  (confirmation popup's `OpenPopup`/`BeginPopupModal` in mismatched
  ImGui window contexts -- a well-documented pitfall) was caught by
  review and fixed.
- ~~**Studio has zero persisted preferences**~~ — window size/panel
  layout **SHIPPED** 2026-09-20 (`app/studio_prefs.hpp`,
  `~/.config/mosaic/studio.toml` + `studio_layout.ini`); **theme
  SHIPPED** 2026-09-21 (PR #196) -- a `UiTheme` enum on
  `RenderDevice` (Dark/Light/Classic, ImGui's own built-ins), a
  live-switching "Theme" menu, and persistence alongside window
  size; **recent-projects SHIPPED** 2026-09-21 (PR #197) -- a
  persisted, capped, deduped `recent_projects` list, a dismissible
  startup picker window, and a full-process relaunch (`readlink`
  + `execl`) to reopen a chosen project (no in-process
  project-switching mechanism exists, correctly left as its own
  future design rather than half-built here). **All three slices of
  this item are now shipped.**
- ~~**Plugin discovery doesn't follow the project dir**, only CWD~~ —
  **SHIPPED** 2026-09-20. `resolve_plugins_dir()`
  (`app/project/project_io.hpp`) derives `<project_dir>/plugins` from
  the CLI's positional argument; `StudioApp` uses it instead of a bare
  `"plugins"` literal. Verified via the real editor binary launched
  from an unrelated CWD. Scoped to Studio's `PluginHost` specifically
  -- `import_plugins` (used by `mosaic run`) already resolved
  correctly and needed no change.

## Scripting

- Same-frame writes / per-script inline override on threaded scripts
  (deliberately excluded — GIL/race reasons, likely permanent).
- Entity lifecycle (`create_entity`/`destroy_entity`) from a threaded
  script — unsupported, documented limitation.

## Physics pillar (gaps found 2026-09-29 while trying to demo specular)

Both were discovered while trying to get the demo sphere rolling so a
specular highlight could be seen sweeping across it. Neither is a bug in
anything shipped; they are capabilities that were never built.

- **No way to apply an impulse or set a velocity.** `PhysicsBackend`'s
  only write path into the simulation is `set_body_transform`, which
  teleports a body *and resets its velocity*, and `BodyDef` has no
  initial-velocity field. So a body can be positioned but never pushed —
  nothing in the engine can make an object move under its own momentum.
  This blocks click-to-nudge, any gameplay verb (throwing, knockback,
  jumping), and even a scripted "roll this ball" one-liner. The real
  design question inside it: an impulse applied **at a point** induces
  torque and makes a sphere *roll*, while a center-of-mass impulse makes
  it *slide* — the demo needs the former, so the API must take a world
  point, not just a direction. ~~**SHIPPED 2026-09-29**~~ --
  `PhysicsBackend::apply_impulse(BodyHandle, impulse, world_point)` plus an
  entity-level `PhysicsSystem::apply_impulse` and a `ClickNudgeSystem` that
  turns a click into an impulse along the camera ray. See
  `docs/project/next-steps.md` for the full entry, including the Critical
  dead-code bug it shipped past a fully green suite.
- **The Box3D backend's end-event shape-validity filter became load-bearing on
  2026-10-02 and nothing but a comment records it.** Found in review of the
  body-destruction slice. `b3DestroyBody` pushes a `b3ContactEndTouchEvent` for
  each of the destroyed body's shapes (box3d `src/contact.c`), and the event
  buffer is swapped at the end of `b3World_Step` -- so a body destroyed by the
  reap at step 0 produces an end event that `step()` reads after step 3 of the
  same tick. The `if (!b3Shape_IsValid(sa) || !b3Shape_IsValid(sb)) continue;`
  branch in `box3d_backend.cpp` drops it. Before this slice nothing could
  destroy a body, so that branch was unreachable and its comment speculative.
  It is now the first line of defence, and removing it makes the
  "emits no ContactEvent naming a reaped entity" test crash on
  `b3Shape_GetBody` over a freed shape. Deferred 2026-10-02: the comment now
  says all of this, which is enough to stop someone deleting it as dead code,
  but there is no test that fails if ONLY that filter is removed and the
  handle_to_entity_ erase is kept -- the crash masks the cleaner assertion. A
  fake backend that synthesises an end event for a just-reaped handle would
  separate the two guards properly; worth doing if either is ever changed.

- **Nothing enforces the GIL rule the scripting module now depends on.**
  Recorded 2026-10-03 with the GIL fix. The rule is "no thread owns the GIL
  implicitly; every entry into CPython acquires", and it is held together by
  guards a human has to remember to add: a new `ScriptEngine` entry point, a
  new `py::` member (which must also be emptied in the guarded destructor), a
  new static `py::object` (which must leak), or a new direct pybind11 caller
  outside the engines will all compile and then abort or crash at runtime.
  The two new cases in `script_gil_test.cpp` catch the worker-thread shape,
  but nothing catches a missed guard on a specific method. Deferred
  2026-10-03: the real answer is probably to stop exposing raw `py::` types
  across the module boundary at all, so there is only one place that can
  touch CPython — which is a bigger refactor than the bug warranted. Until
  then the destructor comment and this entry are the enforcement.

  Two specifics worth naming, both from the 2026-10-03 review. **`value_to_py`
  (`value_marshal.hpp`) is the single weakest point** in the otherwise
  self-enforcing argument: every other unguarded public function takes an existing
  `py::dict&`/`py::handle`, so a caller cannot reach it without already holding the
  GIL, but `value_to_py(const Value&) -> py::object` *mints* a Python object from
  non-Python input, so a future caller in `app/` or `studio/` could call it with no
  GIL and the returned object's destructor would decref unguarded too. And **the
  main thread's `tstate->gilstate_counter` immediately after init was not verified**
  from source (CPython's `pystate.c` is not vendored). If it is >=1, main-thread
  guards nest harmlessly, which is the intended and almost certainly actual
  behaviour. If it is 0, the first main-thread guard's release would tear down and
  recreate the main thread state on every call -- quiet per-call churn plus a lost
  main-thread `sys` thread dict, not a crash. The one-line check is to print
  `PyThreadState_Get()->gilstate_counter` right after `initialize_interpreter`
  returns; worth doing if unexplained main-thread Python state loss ever appears.
  **RESOLVED 2026-10-04** by the thread-dict probe described in the `PyThreadState`
  churn entry below: the main thread's thread dict survives across an
  acquire/release pair, which it could not if the counter were 0, so the quiet-churn
  branch does not occur.
- **The threaded script path creates and destroys a `PyThreadState` per engine call.**
  **Investigated 2026-10-04; the cheap fix was tried and PROVABLY DOES NOT WORK.** Read this
  before attempting it again.

  *Confirmed real, with a falsifiable test.* The observable is the thread **dict**, not the
  `PyThreadState` pointer: `PyThreadState_Clear` clears the dict, so a value written under one
  `gil_scoped_acquire` and read back under the next survives if and only if no teardown
  happened in between. On a worker thread it does **not** survive — the churn is real.
  (Pointer identity was tried first and is **unsound**: the freed state is re-allocated at the
  same address, so that version of the test passed on the churning build. Do not use it.)

  *`PYBIND11_SIMPLE_GIL_MANAGEMENT` does not fix it.* The idea was that
  `PyGILState_Ensure`/`Release` is cached per thread by CPython, so no interface change would
  be needed — one `target_compile_definitions(pybind11_headers INTERFACE ...)` line. Tried,
  with the macro verified present in the test translation unit by an `#error` probe: **the
  test stayed red.** The caching only applies to *nested* `Ensure` calls; the outermost
  `Release` on a thread still tears the state down, under both pybind11 implementations. The
  premise that this was a one-line fix was simply wrong. (Note for whoever retries: the real
  target is `pybind11_headers`; `pybind11::headers` is an ALIAS and
  `target_compile_definitions` rejects it.)

  *What does work, measured.* Nesting. Holding one outer `gil_scoped_acquire` for the duration
  and letting the inner acquires nest makes the probe survive (verified directly). So the
  remedy is the one the review originally proposed: a guard spanning `execute(job)`, which
  makes every inner acquire a counter bump. That needs a **neutral hook on `ScriptEngine`** —
  an opaque RAII batch token (`virtual std::unique_ptr<BatchScope> begin_batch()` defaulting to
  `nullptr`) — because `engine/script/script_worker.cpp` is deliberately pybind11-free. One
  wrinkle to design around: the GIL is global, not per-engine, so taking the token from one
  arbitrary engine to cover all of them reads oddly and wants a better home than
  `ScriptEngine` instances. An alternative worth weighing is a one-time "attach this thread to
  the script runtime" hook that establishes a persistent thread state at worker start (Ensure
  then `PyEval_SaveThread`, keeping the state alive while releasing the GIL), which fixes the
  churn without bracketing every batch.

  *Bonus result, already banked.* The same probe **resolves the `gilstate_counter` question**
  recorded above from #215's review: the main thread's dict survives across acquire/release,
  so its counter is >=1 at init and the quiet-churn branch the reviewer could not rule out
  does not occur. That uncertainty can be considered closed.

  Original entry follows.
  Found 2026-10-03 in review of the GIL fix. `ScriptWorker::execute` makes three
  independent guarded calls per scripted entity per job (`bind_context`,
  `set_global("dt")`, `run_string`), and on a worker thread each guard's release drops
  the counter to zero, so `PyThreadState_Clear` + `PyThreadState_DeleteCurrent` run and
  the next acquire calls `PyThreadState_New`. At 100 scripted entities and 60 Hz under
  `--script-thread` that is roughly 18,000 thread-state create/destroy pairs a second --
  precisely the case pybind11's own `gil.h` comment warns about. Correctness is
  unaffected and the main-thread path does not churn. Deferred 2026-10-03 because the
  cheap fix is unavailable where it belongs: nesting is free, so one guard spanning
  `execute(job)` would make every inner acquire a counter bump -- but
  `engine/script/script_worker.cpp` is deliberately pybind11-free, so it needs a neutral
  hook on `ScriptEngine` (an opaque scoped batch token), and adding an interface method
  wants its own design rather than riding along with a crash fix.
- **`app/run/editor.cpp`'s shutdown-safety comment is slightly stronger than the code
  supports.** Pre-existing, noted 2026-10-03 in review. It argues the engine destructors
  are safe because they "only drop their `py::dict`" and never dereference the bound
  `world`/`reflection` -- but dropping the dict can run `__del__` on script-defined
  objects, and those can call the installed `get_component`/`set_component`/host
  callbacks, which *do* dereference `world_`/`context_`/`host_`. Unchanged by the GIL
  work (the dict was always destroyed there, and now at least the GIL is correctly held
  while it happens). Deferred: the fix is either to clear the namespace before unbinding,
  or to say plainly in the comment that teardown order depends on no script holding a
  live object with a `__del__` that touches the world.
- **Handle reuse / a free list for the Box3D backend's `bodies_` vector.**
  Recorded 2026-10-02 as a non-goal of the body-destruction slice. That slice
  tombstones a destroyed slot with `b3_nullBodyId` rather than erasing it,
  because `handle == index + 1` and erasing would shift every later handle. So
  the vector only ever grows: a session that spawns and destroys many bodies
  accumulates dead slots even though the Box3D bodies themselves are freed.
  Strictly better than leaking the bodies, but a leak of a different size
  rather than its absence. Deferred 2026-10-02: a free list would reuse slots
  but reintroduces the ABA problem that the generational `EntityID` solves on
  the ECS side -- a recycled handle could be mistaken for the body that
  previously held it -- so it wants a generational `BodyHandle`, which is an
  interface change. No measured pressure yet.
- **Changing a physics body's shape or type in place.** Recorded 2026-10-02 as
  a non-goal of the body-destruction slice. `BodyDef` is consumed once at
  `create_body` and nothing can alter a live body's shape, collider extents, or
  static/dynamic type afterwards. The body-destruction slice makes
  destroy-and-recreate possible for the first time, but deliberately does not
  expose it. Deferred 2026-10-02: a scene editor changing a collider's extents,
  or gameplay toggling a body between static and dynamic, both want this, and
  both need a decision about whether velocity and accumulated state carry over
  across the recreate. Related: reaping a body when its *collider* component is
  removed while `RigidBody` stays -- the body currently persists with the shape
  it was created with, and arguably should be recreated instead, which makes it
  the same question rather than a separate one.
- ~~**Destroying an entity never destroys its physics body**~~ — **SHIPPED
  2026-10-02.** `PhysicsBackend` gained a pure-virtual `destroy_body`, and
  `PhysicsSystem::update()` gained a reap as step 0 inside the existing lock:
  any entry in `bodies_` whose `RigidBody` has vanished loses its body and its
  three map entries. The predicate is "`RigidBody` is gone" rather than "the
  entity is invalid", which covers `destroy_entity` committing, `World::clear()`
  (which bypasses the deferred queue and runs no systems), and removing
  `RigidBody` from a living entity -- all three with one check. The Box3D side
  was the bulk of it: `handle == index + 1` in an insert-only vector, so a
  destroyed slot is tombstoned with `b3_nullBodyId` and all four handle-taking
  methods now route through one `live_body()` guard, where three of them
  previously checked only the range. Evidence: with the backend
  call dropped but the maps still erased, the call-counting tests fail merely
  because the call is absent while the stacked-boxes test fails
  `1.99934 < 1.49934` -- the upper box still propped up by a ghost. That
  asymmetry is why a positional test was written instead of a call-counting one.
  Original entry follows.

- **Destroying an entity never destroys its physics body.** Found 2026-10-01
  while scoping the ContactEvent lock-ordering fix. Neither `PhysicsSystem` nor
  `PhysicsBackend` has any body-removal path: `create_body` has no counterpart,
  and `bodies_`, `handle_to_entity_` and `synced_pose_` are only ever inserted
  into. So destroying a simulated entity leaks its Box3D body, which keeps
  stepping and keeps generating contacts forever, while the three maps keep
  stale entries that grow without bound over a long session. `update()` happens
  to tolerate it -- step 2 and step 4 both `continue` when `get_component`
  returns null, and step 5 drops contacts whose handles are unmapped -- which is
  exactly why this has stayed invisible. Deferred 2026-10-01: the fix adds a
  pure virtual (`destroy_body`) to `PhysicsBackend`, which every backend must
  then implement, plus a decision about how `PhysicsSystem` learns an entity
  died (poll the ECS each tick for vanished bodies, or an explicit
  entity-destroyed event it subscribes to). That is an interface change and a
  design question, not a patch. Noted now because the ContactEvent fix lets a
  subscriber call back into `PhysicsSystem` for the first time, which makes
  "handler destroys a colliding entity" a reachable shape rather than a
  hypothetical one -- it is still safe today, but it is one step closer.
- **Angular impulse / torque API.** Box3D exposes
  `b3Body_ApplyAngularImpulse`, but the impulse slice deliberately ships
  only the linear-at-a-point call, which already induces spin via torque
  and is what the demo needs. Deferred 2026-09-29: a direct torque verb
  has no caller yet, and adding an unused second way to spin a body
  invites picking the wrong one.
- **Script (Python) bindings for impulses and raycasts.** Scripts can
  read and write Transforms but cannot push a body or query the physics
  world, so "roll this ball when the player presses X" is not yet
  expressible in a scene's own script. Deferred 2026-09-29: it needs its
  own binding-surface design (how a script names an entity, what a
  raycast result looks like in Python) and is a bigger question than the
  C++ primitive it would wrap. Depends on the impulse API existing first.
- ~~**The `[script]` SIGSEGV**~~ — **ROOT-CAUSED AND FIXED 2026-10-03. It was
  never a flake, and the cause was written in our own comment.**
  `ensure_interpreter` noted that after init "the calling (main) thread holds
  the GIL ... no explicit GIL handoff" — so whoever won the `std::call_once`
  race kept the GIL for the process. The threaded script test passed alone
  because the `ScriptWorker` thread won and held it; it died after any other
  engine had been created on the main thread, because the worker then called
  `PyDict_New()` with no GIL. `ctest` never saw it because
  `catch_discover_tests` gives every `TEST_CASE` its own process, so the worker
  was always first — **the gate was green and blind at the same time.**
  Fixed by making the invariant real: no thread owns the GIL implicitly, and
  every entry into CPython acquires. Four things that were not obvious going
  in, each of which cost a failing-gate iteration: `globals_` had to become a
  `std::optional<py::dict>` in both engines, because a `py::dict` *member* is
  built before the constructor body and destroyed after the destructor body,
  leaving no scope in which to guard `PyDict_New`; `restricted_builtins`'
  function-local static `ProxyClasses` had to start leaking, because
  `__cxa_atexit` destroys it with no GIL and `dec_ref` aborts -- and that abort is a
  *debug* symptom only (`PYBIND11_ASSERT_GIL_HELD_INCREF_DECREF` is defined when
  `NDEBUG` is unset), so in a Release build the same path is a silent unguarded
  `Py_XDECREF`: the leak is **more** necessary there, not less; `trace_fn_` had
  to be emptied inside the guarded destructor, since a `py::` member outlives
  the body; and every direct pybind11 user outside the engines — the docs
  generator and four test files that build their own `py::dict` — had to
  acquire too. Evidence: `[script]` in one process is 5/5 clean where it was
  RC=139 every time, the whole binary now runs in a single process (1081
  cases, 31577 assertions) which the crash made impossible, and removing the
  release makes the new tests **deadlock** rather than crash.
  The entry that called this "unexplained" follows for context.

- **The `[script]` SIGSEGV now has a deterministic, one-command reproduction,
  and `ctest` structurally cannot see it.** Found 2026-10-03 while guarding
  `raycast`'s sequence reads. #208's entry recorded this failure mode as
  unexplained and said it "has not reproduced since". It reproduces **3/3 on
  pristine `main`** with no local changes at all:

  ```
  ./build/debug/tests/mosaic_unit_tests "[script]"   # RC=139, every run
  ```

  The failing case is `script_system_test.cpp`'s "threaded ScriptSystem applies
  script writes one tick later", which **passes in isolation** -- so it is the
  in-process ordering of the `[script]` cases that kills it, almost certainly
  embedded-CPython interpreter init/teardown across threads, as #208 guessed.
  Verified it is not caused by anything in flight: reverting a local engine
  change and then the new tests too left it failing identically on untouched
  `main`.

  **Why CI has stayed green through all of this:**
  `catch_discover_tests(mosaic_unit_tests)` registers each `TEST_CASE` as its
  own ctest test, so every case runs in a fresh process and the ordering that
  triggers the crash never occurs. The gate reporting 1079/1079 is therefore
  true and also blind to this. Anyone running the tag directly -- which is the
  normal way to work on scripting -- hits it immediately.

  Deferred 2026-10-03 rather than fixed: the cause is interpreter lifetime
  across threads, which is the same machinery #208 spent nine days in, and it
  deserves its own slice rather than riding along with an unrelated binding
  fix. But it is no longer "cannot reproduce" -- the command above is the
  starting point, and `script_system_test.cpp:195` is the assertion that goes
  first.
- ~~**`raycast()`'s script binding indexes its sequence arguments without a
  length check**~~ — **SHIPPED 2026-10-03.** It now uses the same `to_vec3`
  helper `apply_impulse` has used since #207, so a short sequence or a
  non-numeric element yields the miss dict the call already returns when no
  host callback is set, rather than raising out of pybind11 and aborting the
  tick. The host is deliberately not called at all on malformed input --
  zero-filling the missing components would hand the simulation a ray the
  script never asked for -- which the tests pin with a call counter. On the
  behaviour-change worry recorded below: what goes away is an *undocumented*
  exception. `raycast`'s `py::doc` promised only "hit data as a dictionary" and
  never promised to raise; it now states the miss-on-malformed rule explicitly.
  Each of the three new sections asserts both the miss AND that the script
  keeps running, and all three fail under the mutation that restores unguarded
  indexing. Original entry follows.

- **`raycast()`'s script binding indexes its sequence arguments without a
  length check.** `python_script_engine.cpp`'s `raycast` reads
  `origin[0..2]` and `direction[0..2]` directly, so a script passing a
  two-element list raises out of pybind11 rather than returning a miss,
  killing the tick. Found 2026-09-29 while adding `apply_impulse` (which
  guards its own reads). Deferred: pre-existing, and tightening it would
  change behaviour a script might already depend on, so it wants its own
  change rather than riding along silently.
- ~~**Five small `ScriptWorker` tidy-ups found in review 2026-09-30**~~ —
  **ADDRESSED 2026-10-04, and two of the five were WRONG.** Checked each against
  the code rather than taking the entry at its word:

  **(a) "`host_` is now write-only" — FALSE.** It is read at
  `script_worker.cpp:19`, inside the recording `apply_impulse` lambda
  (`if (!host_.apply_impulse) { return false; }`). There is a live reader; the
  member is not dead. No change made.

  **(d) "`execute()` clears `pending_impulses_` after `std::move`-ing it, a
  no-op" — FALSE.** A moved-from `std::vector` is valid but *unspecified*, so the
  `clear()` guarantees empty instead of relying on an implementation detail. It is
  defensive and correct. No change made.

  **(b) "`has_pending()` has no callers" — true. Removed.** Grep found only its
  declaration and definition; public API with zero consumers and its correctness
  verified by nothing. The gate proved nothing used it.

  **(c) "`dispatch()` clears `result_ready_` unconditionally" — true, and now
  documented with two measurements rather than a vague comment.** The clear is
  deliberate, not vestigial: without it a `collect()` after two dispatches would
  return the STALE result while the newer job was still pending, which is worse
  than losing the older one. Two things were measured and are recorded in the
  header: the drop is reachable only by a race between `dispatch()` and the worker
  dequeuing, which came out **9 passes / 11 failures over 20 runs** — so a test of
  it would be flaky rather than load-bearing; and **removing the clear entirely
  leaves the whole suite green (1081/1081)**, so nothing pins it today. The
  precondition (collect before dispatch) is now stated as the contract.

  **(e)** stands as recorded: a script-spawned Python thread calling
  `apply_impulse` after `on_update` returns would race `execute()`'s tail, and
  `SnapshotContext` has the identical exposure, so it is not a new hazard class.

  Lesson worth keeping: this entry had been carried for five days with two
  incorrect claims in it. A recorded finding is a hypothesis until someone
  re-checks it against the code.
- **`apply_impulse`'s return value is optimistic for non-scripted targets.**
  **DOCUMENTED 2026-10-04** at the binding's `py::doc` rather than changed -- the
  behaviour below is still the right call, it was simply invisible to a script author
  reading the docstring. The binding now says that under `--script-thread` `True` means
  "accepted for replay on the main thread", not "a body was pushed", and that a push
  aimed at anything outside the job snapshot (including a non-existent id) is buffered
  and returns `True`. Original reasoning follows and still stands.
  The worker answers exactly for entities in the job snapshot, but an entity
  absent from it cannot be judged, so the push is buffered and `True` is
  returned; the main-thread replay re-checks `has_body` and drops it if there
  is none. Consequence: a script that latches on the return value when pushing
  some *other* object can latch on a push that is later dropped, and an
  invented entity id now returns `True` rather than `False`. Chosen 2026-09-30
  over the alternative, which was to refuse every unjudgeable push — that made
  a controller script pushing a crate silently do nothing under
  `--script-thread`, which is a far worse failure than an imprecise bool. The
  real fix is to snapshot the physics-relevant components of bodied
  non-scripted entities too, so the worker can answer exactly in every case.
- **Deferred host calls are not a general mechanism.** Only
  `apply_impulse` is buffered across the script-worker boundary; the
  collected result carries a writes channel and an impulses channel as two
  parallel vectors. Deferred 2026-09-30 during the buffering design: if a
  third deferrable verb appears, generalising the channel (a tagged
  deferred-call list) is the better change than adding a third parallel
  vector — but two is not yet a pattern, and generalising now would be
  speculative.
- **Snapshot-served ray geometry.** The physics backend mutex makes a
  worker raycast wait for the main thread's physics update. The only
  approach that removes the dependency entirely is answering rays against
  collider geometry captured in the job snapshot, with no shared state at
  all. Deferred 2026-09-30: it means reimplementing ray-vs-box/sphere/
  capsule outside Box3D and keeping two intersection implementations in
  agreement forever, whose failure mode is silent divergence from the real
  physics world. This is the right answer only if blocking ever shows up
  in a profile.
- ~~**`PhysicsSystem::update()` emits `ContactEvent` synchronously while holding
  `backend_mutex_`**~~ — **SHIPPED 2026-10-01.** `update()` now maps contacts
  into a local vector inside a braced block holding the lock, and emits after
  the block closes. The extent is delimited lexically on purpose: a `unique_lock`
  plus `unlock()` would have been a smaller diff but would leave the locked
  region ending at an unmarked statement, so a later edit adding a call-out above
  it would reintroduce exactly this bug. The pose write-back already preceded
  emission, so handler-visible ordering did not change -- the entry below, and the
  #209 summary it came from, were wrong to claim it would. Evidence: both new
  re-entrancy tests fail on a 3000 ms watchdog with the emit back under the lock
  and pass with it outside; the watchdog runs the tick on a detached thread so the
  failure is a named assertion in seconds rather than a 25-minute ctest stall.
  Original entry follows.

- **`PhysicsSystem::update()` emits `ContactEvent` synchronously while
  holding `backend_mutex_` — a lock-ordering constraint, not just a
  re-entrancy one.** Found 2026-09-30 in review of the mutex slice.
  `EventBus::emit` dispatches immediate subscribers on the same stack, so a
  subscriber runs inside the lock. The narrow risk is a subscriber that calls
  `raycast`/`has_body`/`apply_impulse` and hangs on a plain `std::mutex`;
  today's two subscribers were checked (one logs, one re-emits onto the
  *deferred* bus) and neither does. The wider and already-live consequence is
  that **every lock a synchronous subscriber takes is now ordered *under*
  `backend_mutex_`** — which today includes the global logger mutex, since the
  contact logger calls `log::info` once per contact inside the lock. Nothing
  inverts that ordering right now, but it is an unwritten invariant: a future
  log sink, or any sync subscriber holding a lock that another thread holds
  while waiting on `backend_mutex_`, deadlocks in a way that looks nothing
  like the re-entrancy case. There is a performance half to the same point: a
  worker `raycast()` can wait behind per-contact logging and the `Table`
  construction in the contact-to-`ScriptEvent` bridge — work with nothing to
  do with the backend. The spec accepts holding the lock across ECS
  traversal explicitly; it never considered holding it across subscriber
  dispatch. Deferred 2026-09-30: the fix is to collect contacts under the
  lock and emit them after releasing it, which changes when subscribers
  observe a contact relative to the pose write-back — a behaviour decision
  worth its own slice. A recursive mutex would hide the re-entrant hang
  without making the read correct, and would do nothing about the ordering.
- ~~**A spurious watchdog trip in the contact-reentry tests degrades into a
  use-after-free**~~ — **SHIPPED 2026-10-03.** The World and the state the
  handlers write are now heap-owned (`std::shared_ptr<World>` plus a `Probe`
  struct), and `tick_with_watchdog` captures both by value, so a thread that
  outlives the `TEST_CASE` keeps what it touches alive and they leak on a trip --
  self-healingly for a spurious slow-runner trip (the thread finishes late and
  drops its references), but for the rest of the test *process* on a genuine
  deadlock trip, since Catch2 keeps running later cases after a failed
  `REQUIRE`. Both are bounded by process exit, and `backend_mutex_` is
  per-instance, so a leaked parked thread cannot block the next case. **The entry below understated the hazard:** heap-allocating the World
  alone would not have fixed it, because the subscriber lambdas also captured
  `handler_calls`/`hit_the_box`/`pushed`/`box` by reference -- all `TEST_CASE`
  stack locals, reachable from inside the World's own EventBus. The ordering
  that makes the fix correct: `Probe` deliberately does NOT own the World, since
  the EventBus holds a lambda owning the Probe, and a Probe owning the World
  back would close a reference cycle and leak on the *passing* path too; the
  handlers hold World and PhysicsSystem as raw non-owning pointers instead.
  Measured both ways with a 50 ms watchdog against 4,000,000 ticks, so the
  thread is certainly still running when the case unwinds: before, RC=134
  (SIGABRT) and RC=139 (SIGSEGV), printing the assertion and *then* dying on a
  signal; after, **RC=1 five times out of five** with the clean named failure.
  Original entry follows.

- **A spurious watchdog trip in the contact-reentry tests degrades into a
  use-after-free instead of the named failure it is designed to produce.**
  Found 2026-10-01 in review of the contact-emit slice.
  `tests/unit/physics/physics_contact_reentry_test.cpp`'s `tick_with_watchdog`
  runs the tick on a detached thread capturing `&world`, so when the main
  thread times out and `REQUIRE` throws, `world` is destroyed at scope exit
  while that thread may still be using it. In the genuine-deadlock case the
  thread is parked on a mutex that is then destroyed -- UB, though observed to
  unwind cleanly and print the assertion. In the case the 3000 ms margin is
  actually guarding against (a loaded runner, no deadlock), the thread is
  *actively ticking* a destroyed `World`, so the run reports a segfault rather
  than "the tick did not return" -- defeating the whole reason the watchdog was
  chosen over a ctest `TIMEOUT`. Damage is bounded: `catch_discover_tests` gives
  each case its own process, so it cannot take other cases down. Deferred
  2026-10-01: the fix is to heap-allocate the `World` on the watchdog path and
  deliberately leak it when the flag never sets, which turns both failure modes
  into a clean named assertion. Not folded into that slice because it only
  affects a failure path that the four-orders-of-magnitude timing margin makes
  very unlikely, and leaking a `World` on purpose wants its own justification
  in the test's comments rather than arriving as a fix-round edit.
- **No ctest `TIMEOUT` property anywhere in the suite.** Recorded 2026-10-01 as
  a non-goal of the ContactEvent lock-ordering slice. `tests/CMakeLists.txt`
  calls `catch_discover_tests(mosaic_unit_tests)` with no timeout, so ctest's
  1500s default applies to every case in the suite: any test that hangs costs 25 minutes
  of CI before it fails, and says only "Timeout" when it does. Deferred
  2026-10-01 rather than folded into that slice, which solves its own hang with
  an in-test watchdog instead -- the watchdog gives a named failure in seconds
  and works when running the test binary directly, which a ctest property does
  not. A suite-wide timeout is still worth having as a backstop for the next
  hang nobody anticipated; it needs its own value chosen against the slowest
  legitimate test rather than a number picked to fit one case.
- **Nothing enforces that a system never calls user code while holding an
  internal lock.** Recorded 2026-10-01 as a non-goal of the ContactEvent slice,
  which fixes the single live instance. `PhysicsSystem` is the only system that
  holds a mutex today, so there is currently nothing else to audit -- but the
  rule it now follows ("collect under the lock, call out after releasing it")
  exists only as a comment and a test in one file. Deferred 2026-10-01: a real
  enforcement mechanism means either a lock-order-checking wrapper around the
  mutex or a convention the dependency linter could verify, and inventing either
  before a second system takes a lock would be speculative.
- **The two concurrent physics tests busy-spin their reader thread** with no
  `yield` or `sleep`. Found 2026-09-30 in review. On a 1-vCPU CI runner the
  reader monopolizes a core, so the interleaving profile differs sharply from
  a dev box and the overlap detector's sensitivity becomes
  machine-dependent. Not a correctness issue, and the detector is
  empirically sensitive where it has been measured (it fails 5/5 runs under
  the removed-lock mutation locally). Deferred 2026-09-30 deliberately
  rather than "fixed" by adding a yield: throttling the reader would
  *reduce* the overlap pressure the detector depends on, so the change needs
  a re-run of the mutation probe on CI hardware to show it still fails
  without the lock. Doing that blind would trade a measured detector for an
  unmeasured one.
- **Nothing enforces that the script worker is joined before
  `PhysicsSystem` is destroyed.** Found 2026-09-30 in review. A worker
  thread blocked on `backend_mutex_` while `PhysicsSystem` is being
  destroyed is undefined behaviour: the mutex and `backend_` die with the
  system. It is safe today only by registration order — `ScriptSystem` is
  added before `PhysicsSystem` in `setup_runtime`, and the system vector
  destroys front to back, so `ScriptWorker`'s destructor joins the thread
  first. Entirely pre-existing; the mutex neither introduces nor worsens it.
  Recorded because the new lock *reads* as though lifetime were handled,
  which makes the real invariant easier to break by reordering two
  `add_system` calls. Deferred 2026-09-30: the fix is an explicit
  shutdown ordering or a `PhysicsSystem` that outlives its readers by
  construction, which is a lifetime-design question rather than a patch.
- **Other engine systems are not thread-safe, and nothing enforces that
  future `ScriptHost` hooks are.** The physics boundary is closed because
  it is the one scripts reach. `set_playing` and `request_stop` were
  verified safe (they write an atomic), but that is a property of today's
  implementations rather than a rule anything checks. Deferred 2026-09-30:
  a general answer means deciding what thread-safety contract `ScriptHost`
  imposes on its implementors, which is a design question rather than a
  patch.
- **Reader-writer or lock-free refinement of the physics lock.** A plain
  mutex serializes a worker raycast against the whole physics update.
  Deferred 2026-09-30 as speculative: there is no measured contention, and
  a finer-grained lock is the classic way to turn a correct coarse lock
  into a subtly wrong fine one.
- ~~**`raycast()` still runs off the script-worker thread**~~ — **SHIPPED
  2026-09-30.** Closed with a `mutable std::mutex` on `PhysicsSystem`, taken by
  `update()`, `raycast()`, `apply_impulse()` **and** `has_body()`. The lock
  protects two things, not one: the seven `backend_` call sites, and the
  `bodies_`/`handle_to_entity_` maps that `update()` writes while the other
  three read them — so `has_body()` needs it despite never touching the
  backend, and `apply_impulse()` needs it despite being main-thread-only
  (it runs from `ScriptSystem::update_threaded`'s replay, *outside*
  `update()`). The entry below guessed this needed the same buffering/snapshot
  design as the impulse work; it did not — blocking a worker raycast for the
  duration of one physics update is bounded and was the user's call. Evidence:
  the new concurrent smoke test **segfaults 3/3 runs** without the lock, and a
  dedicated overlap-detecting fake backend **fails 5/5 runs** when the
  `lock_guard` is removed from `raycast()` alone. Original entry follows.

- **`raycast()` still runs off the script-worker thread.** The
  `--script-thread` path binds the same `ScriptHost` into worker-thread
  engines, and while `apply_impulse` is now deliberately left unwired there,
  `raycast` is not: it queries the live physics world from the worker thread
  while the main thread may be inside `PhysicsSystem`'s step. It is a *read*
  rather than a write and predates all of this, which is why it was not
  changed alongside the impulse guard — but it is the same boundary, and a
  read racing a step can still return torn or mid-integration results.
  Deferred 2026-09-30: fixing it properly means the same buffering/snapshot
  design decision the impulse entry above needs, and the two should be solved
  together rather than one at a time.
- **Two thin spots in the new physics-binding tests.** Found 2026-09-30 in
  review, both real but neither load-bearing today. (a) The demo ball-pusher
  test asserts only `fabs(delta) > 0.05`, which would not distinguish the
  intended origin-seeking oscillation from a wrong-sign runaway that rolls the
  sphere off the platform — bounding the sphere's `|x|` would pin the actual
  intent. (b) `make_physics_apply_impulse`'s `world.is_valid(e)` guard is
  unproven: removing it fails nothing, because the only invalid-id test uses
  an id that also fails `has_body`. A test that destroys a bodied entity and
  pushes it in the same tick would make it load-bearing.
- ~~**Script-thread impulses are disabled, not buffered**~~ — **SHIPPED
  2026-09-30.** Impulses are now recorded by `ScriptWorker` against the job
  snapshot and replayed on the main thread through the neutral host callback,
  so the same script works on both paths with the one-tick lag the threaded
  path already has for component writes. The `!script_thread` guard is gone.
  `raycast`'s off-thread read is unchanged and still tracked below. Original
  entry follows for context.

- **Script-thread impulses are disabled, not buffered.** On the
  `--script-thread` path, `on_update` runs on a worker thread while the main
  thread may be inside `PhysicsSystem`'s step, and `apply_impulse` writes
  straight into the live Box3D body -- an unsynchronized write during a step
  is undefined behaviour. Found 2026-09-30 in whole-branch review, once the
  default demo scene began calling `apply_impulse` and made the hazard
  reachable with no user code. Mitigated by NOT wiring the hook on that path,
  so it returns `False` there per `ScriptHost`'s unset-hook contract. That is
  a guard, not a fix: a script that works single-threaded silently does
  nothing under `--script-thread`. The real fix is to buffer impulses the way
  the threaded path already buffers component writes, and apply them on the
  main thread -- which needs a design decision about ordering against the
  physics step. Note `raycast` still runs off-thread on that path; it is a
  read and pre-existing, but it is the same boundary.
- ~~**A dict passed to `apply_impulse` raises instead of returning `False`**~~ —
  **SHIPPED 2026-10-04, for `raycast` too.** `to_vec3` now takes a `py::handle` and
  checks sequence-ness itself, and both bindings take `py::object` instead of
  `py::sequence`. The signature was the whole problem: with `py::sequence` there,
  pybind11's *dispatcher* rejected a non-sequence before the binding body ran, so the
  guard the spec relied on never got a chance. A dict, an int and `None` now all
  return `False`/a miss without aborting the tick.
  **Taken wider than recorded, deliberately:** the entry scoped this to
  `apply_impulse`, but #214 had just made `raycast` degrade for malformations *within*
  a sequence, so fixing only one would have left a dict returning `False` from one
  binding and raising from the other -- a new inconsistency rather than a closed one.
  Also documented at both surfaces, including the one imprecision that stays:
  `apply_impulse` under `--script-thread` returns `True` meaning "accepted for replay",
  not "a body was pushed", since the worker can only judge entities in its own job
  snapshot. That was a deliberate 2026-09-30 choice and remains so -- it was just
  undocumented at the binding. One behaviour is pinned as a two-layer guard rather than
  one: `str` passes `PySequence_Check`, so a string falls through to the element casts
  and fails there, which the tests now cover so the layers stay honest. Mutation:
  restoring `py::sequence` in either signature fails that binding's cases (3 and 4
  sections respectively). Original entry follows.

- **A dict passed to `apply_impulse` raises instead of returning `False`.**
  The spec says a malformed argument returns `False` rather than raising, but
  that only covers malformations *within* a sequence. A non-sequence -- and
  the single most likely wrong argument here is a **dict**, because this same
  scripting surface uses dicts for component values (`tr['position']`) -- is
  rejected by pybind11's dispatcher before the binding's own guard runs, so
  `apply_impulse(e, {'x':1,'y':0,'z':0}, ...)` raises and aborts that
  script's `on_update` for the tick. Found 2026-09-30 in review. Deferred:
  it mirrors `raycast`'s existing behaviour exactly, and tightening it means
  taking `py::object` and hand-rolling the sequence check for both bindings,
  which is its own change.
- **Other physics verbs from scripts** — setting a body's velocity
  directly, teleporting a body, and overlap/shape queries. Deferred
  2026-09-29 during the scripting-bindings design: impulse and raycast are
  the two with a caller today, and each of the others needs its own
  semantics decision (does a script-set velocity survive the next tick's
  Transform sync? does a teleport wake the body?) rather than being a
  matching pass-through.
- **Raycast layer masks and all-hits queries from scripts** — the backend
  exposes closest-hit only, so anything richer starts with a
  `PhysicsBackend` change rather than a binding. Deferred 2026-09-29.
- **A `Vec3` Python class instead of dicts** — nicer ergonomics than
  `{'x':…,'y':…,'z':…}` for hand-written literals, but it would introduce a
  second vector representation alongside the dicts every existing global
  already uses, and the two would have to coexist indefinitely. Deferred
  2026-09-29: worth doing only as a deliberate migration of the whole
  scripting surface, not per-binding.
- **Surfacing scripting-binding failures to the log** — every binding
  returns `False`/`None` on bad input rather than raising, which is right
  for a game loop (a raising binding kills the tick) but gives a script
  author with a typo'd key no feedback at all. Deferred 2026-09-29: the
  fix is a logging/diagnostics decision spanning every global, not just
  the physics ones.
- **Continuous forces, as distinct from instantaneous impulses.** An
  impulse is a one-shot momentum change; sustained thrust, wind, or
  magnetism needs a per-tick force accumulator. Deferred 2026-09-29: no
  caller today, and the two have genuinely different semantics, so
  bolting `apply_force` onto the impulse call would blur them.
- **Drag-to-throw.** Click-and-drag to aim, release to launch with a
  magnitude from the drag vector. Deferred 2026-09-29: it needs pointer
  press/drag/release state tracking that nothing else in the engine wants
  yet, whereas a click nudge needs only the press edge already available.
- **A physics body whose `Transform` is written every tick loses any impulse.**
  `PhysicsSystem::update` step 2 teleports a body whenever its `Transform`
  diverges from the pose the system last wrote, and `set_body_transform` zeroes
  both linear and angular velocity. So if a script, the animation system, or
  client-side replication drives a `RigidBody` entity's `Transform`, clicking
  that object does nothing visible -- the impulse is erased on the next tick.
  Found 2026-09-29 in review; latent, not live, since no demo body is in that
  category today. Deferred: the fix is a real design question (does an authored
  Transform win over the simulation, or does the body become kinematic?) rather
  than a patch.
- **UI click arbitration is `UiButton`-only.** `UiSystem::press_consumed()` is
  true only when a press lands on a `UiButton`, so a click on a decorative
  `UiImage` or `UiText` falls through and nudges whatever world object sits
  behind it. The demo's HUD bar is a button and is correctly claimed, so the
  spec's stated criterion is met. Deferred 2026-09-29 as a recorded limitation:
  generalizing it means deciding which UI element kinds are click-opaque, which
  is a UI-pillar decision rather than a physics one.
- **`ClickNudgeSystem` declares no `ComponentAccess`.** It reads `Camera` and
  `WorldTransform` and causes `Transform` writes through the physics backend,
  but declares nothing -- matching `UiSystem` and `AudioSystem`, and harmless
  today because nothing reads `access()`. Deferred 2026-09-29: when a job-system
  scheduler lands, an empty access set reads as "depends on nothing" and this
  system becomes a candidate to run concurrently with `PhysicsSystem`. The fix
  is two `reads<>()` lines, but it should land with whatever actually consumes
  the access sets, so the declaration and its enforcement are reviewed together.
- ~~**`app/main.cpp`'s system wiring is untestable**~~ — **SHIPPED 2026-09-29.**
  `app/run/windowed_systems.hpp`'s `register_windowed_systems()` now owns the
  conditional registration, and tests assert what is registered under which
  condition. Investigation narrowed the problem first: `setup_runtime` was
  already a testable seam with five test callers registering eleven systems,
  and the dead feature had simply been wired *outside* it. Only conditional
  registration carries this risk — an unconditional one cannot silently
  vanish — so `TransformSystem` stayed where it is. Known limit, stated in the
  header: this makes the registration decision testable but cannot verify the
  caller passes the right condition; `main()` therefore passes the parsed CLI
  flag straight through with no intermediate that can go stale. Original entry
  follows for context.

- **`app/main.cpp`'s system wiring is untestable, and it has already shipped a
  dead feature once.** Found 2026-09-29: `ClickNudgeSystem` was registered
  behind `if (glfw_window != nullptr)` placed ~60 lines *before* the window is
  created, so the guard was always false and clicking did nothing in the real
  app. Every automated check passed — the unit tests construct the system
  directly and bypass `main()` entirely, and the launch check only proves the
  process starts. The gap is structural: `main()` is one long function whose
  wiring decisions no test can observe. The fix is to extract the registration
  decisions into small functions that take their inputs explicitly (e.g.
  `register_windowed_systems(World&, bool windowed, providers...)`) so a test
  can assert *that* a system is registered under the right conditions.
  Deferred 2026-09-29 rather than done inside a fix round, because carving a
  testable seam out of `main()` is a design change that deserves its own
  review. Depends on nothing.
- **`ClickNudgeSystem::camera_position` duplicates `studio::pick_entity`'s
  camera selection.** Both independently implement "first entity with a
  `Camera` and a `WorldTransform`, take the world matrix translation". They
  agree today by construction, since both run in the same tick over the same
  components. Deferred 2026-09-29: the clean fix is for `RaycastHit` to carry
  the ray origin `pick_entity` already computed, which changes a studio API the
  editor's picker also consumes — a separate, reviewable change rather than a
  fix-round edit. If an explicit active-camera policy is ever added and only one
  site is updated, the nudge would push along a ray from the wrong eye while
  picking still used the right one.
- ~~**Static bodies cannot be created rotated**~~ — **SHIPPED 2026-09-29.**
  `BodyDef` gained a `rotation` field, passed through from the entity's
  `Transform` and into `b3BodyDef::rotation` (which already existed and was
  simply never wired). Investigation found this was **two** live bugs, not the
  one recorded: a rotated *static* body kept its Transform rotation while its
  collider stayed axis-aligned, and a rotated *dynamic* body had its authored
  rotation **overwritten with identity by the pose write-back on the first
  tick** — visibly flattening itself the instant physics started. Nothing
  pinned the old behavior (no test used a non-identity rotation on a physics
  body, and no `BodyDef{...}` aggregate-init sites existed), so the fix was a
  clean addition. Original entry follows for context.

- **Static bodies cannot be created rotated.** `BodyDef` carries
  `{type, position, shape, density}` — no rotation — so `create_body`
  always produces an axis-aligned body. A tilted ramp, a slanted wall, or
  any angled static geometry is therefore unrepresentable: the renderer
  draws the entity rotated while the collider stays axis-aligned, and the
  two silently disagree. The only workaround today is to mutate the
  entity's Transform *after* creation so the "external edit" path calls
  `set_body_transform`, which does accept a rotation — a hack that
  depends on a sync heuristic rather than on the API meaning what it
  says. Deferred 2026-09-29: the fix is to add `rotation` to `BodyDef`
  and pass it through `create_body`, which is small but touches the
  backend interface and wants its own tests. Not needed once impulses
  exist, which is why it is not being folded into that slice.

## Tech debt

- ~~**Issue #25 — Component TypeID densification**~~ — **SHIPPED**
  2026-09-20. `storages_` now indexed by a new, purely-internal dense
  slot (`detail::component_storage_slot<T>()`) instead of the shared
  `type_id<T>()` -- `ComponentTypeID` itself deliberately untouched,
  avoiding a reintroduction of the earlier "#9 identity drift" bug.
- ~~**`LiveLinkServer` tests hard-code listen ports, and one flaked on CI**~~ —
  **SHIPPED 2026-09-30.** All eight now call `start(0)` and ask the kernel for a
  free ephemeral port. `start(0)` was already possible; what was missing was any
  way to learn the kernel's choice, so `LiveLinkServer` gained
  `port()` (via `getsockname()`, reset to 0 by `stop()`). Production is
  unchanged: `studio_app` still calls `start(kDefaultLiveLinkPort)` and `port()`
  returns exactly that. The already-bound-port case got *stronger* rather than
  merely portable — it now binds a port it **knows** is taken
  (`server1.port()`) instead of hoping a hard-coded number is free.
  The mutation probe turned up a second defect worth recording: `TestClient`
  ignored `connect()`'s return value, and `send_text()` on a never-connected
  socket raises **SIGPIPE**, killing the whole test binary as a bare exit 141
  with no named assertion — which is exactly how the original port collision
  would have presented on CI. With `connected()` asserted, removing
  `getsockname()` now fails **7 named assertions across 7 cases**. Original
  entry follows.

- **`LiveLinkServer` tests hard-code listen ports, and one flaked on CI.**
  `tests/unit/studio/live_link_server_test.cpp` binds fixed ports (34906,
  34907, ...). On 2026-09-29 the case "LiveLinkServer start() fails on an
  already-bound port" failed a GitHub Actions run (1015/1016) and passed on
  a re-run of the same branch with no code change in that area, which is the
  signature of a lingering socket in `TIME_WAIT` or a port collision on a
  shared runner. Each Catch2 case runs as its own process, so the exposure is
  real. **Do not add a third `-E` exclusion for it** -- the fix is to bind
  port 0 and ask the OS which port it got, so the tests stop competing for
  fixed numbers. Deferred 2026-09-29: small and self-contained, but it is a
  test-infrastructure change unrelated to whatever slice is in flight.
- **`enet_transport_test.cpp` hard-codes six UDP ports too** (34001-34006).
  Found 2026-09-30 while scoping the `LiveLinkServer` ephemeral-port fix, which
  covers the TCP side only. Same failure signature is available to it: a fixed
  port colliding with anything else on a shared CI runner. Not folded into that
  slice because the fix is materially bigger — ENet's `enet_host_create` accepts
  port 0 and records the kernel's choice in `host->address.port`, but Mosaic's
  transport sits behind its own neutral interface, so learning the bound port
  means adding an accessor to that interface and threading it through the
  backend. Deferred 2026-09-30: a one-file test change on the TCP side versus an
  interface change on the UDP side is the difference between two slices, and the
  UDP tests have not actually flaked yet where the LiveLink one has. Depends on
  nothing; pick it up if a net test ever fails on CI.
- ~~**Embedded-Python worker-thread flakiness (ScriptSystem/ScriptWorker)**~~ —
  **ROOT-CAUSED AND FIXED 2026-09-30. It was never a flake.**
  `ScriptWorker::run()` cleared `pending_` when a job was dequeued but set
  `result_ready_` only after `execute()` returned — and `execute()` runs
  *unlocked*, for as long as starting Python and compiling scripts takes. In
  that window both flags were false, so `collect()`'s early-out
  (`if (!pending_ && !result_ready_) return {};`) could not distinguish "job
  executing right now" from "nothing was ever dispatched" and silently
  returned an empty result, dropping that tick's writes. Fixed by adding an
  `executing_` flag covering the window, so `collect()` waits for an in-flight
  job instead of skipping it. Measured: the headline test went from **2/12 to
  12/12**, the other from flaky to 12/12, and the full suite is green with **no
  `-E` exclusions**, which have accordingly been removed from the CI gate.
  **The old entry below understated it as "rare/near-impossible to reproduce
  locally"; it actually failed ~80% of the time locally.** That mischaracterisation
  is why a real race sat behind a CI exclusion for nine days — worth remembering
  the next time something is labelled a flake.
  **One caveat, stated rather than buried:** the original entry recorded TWO
  failure modes, and this fix explains only one. The SIGSEGV under full-suite
  ordering, attributed to embedded-Python interpreter init/teardown across
  threads, is untouched by an `executing_` flag and remains unexplained. It has
  not reproduced since, and is now gated by CI rather than excluded from it --
  but if a segfault returns, this is the thread to pull.
  Original entry follows.

- **Embedded-Python worker-thread flakiness (ScriptSystem/ScriptWorker)**
  — real, currently un-root-caused. Two tests --
  `tests/unit/script/script_system_test.cpp`'s "threaded ScriptSystem
  applies script writes one tick later" (a known SIGSEGV flake,
  previously observed under full-suite local ordering) and
  `tests/unit/script/script_worker_test.cpp`'s "ScriptWorker runs a
  job on its thread and returns the write batch" (a size-mismatch
  failure, not a crash) -- both touch the same embedded-Python +
  worker-thread machinery and both are rare/near-impossible to
  reproduce in local dev runs, but failed on 2 of the first 3 real
  GitHub Actions CI runs (`.github/workflows/ci.yml`, added 2026-09-21)
  -- a materially higher rate than anything seen in this project's
  local-dev history, almost certainly because CI's shared/constrained
  runner vCPUs expose thread-scheduling races local hardware doesn't.
  **Not fixed, only worked around for now:** both are excluded from
  the CI `ctest` gate via `-E` (still run by every local/full-suite
  invocation) so CI stays usably green; the actual race in
  `engine/script/script_worker.hpp`/`ScriptSystem`'s threaded path
  needs real investigation (likely embedded-Python interpreter
  init/teardown ordering across threads) as its own scoped piece of
  work, not something to guess at blind.

## Effort vs. impact

Cross-cutting prioritization pass, originally 2026-09-19, **re-derived
2026-09-19** post-arc: the shipped item is removed, resolved items are
removed, and the two refresh-note items (asset-usage queries' now-easier
build, the `FieldVisitor` extension's now-tripled motivation) are
re-rated. Re-derive again when the backlog above has moved significantly
rather than trusting the ratings below forever.

### 🟢 Quick wins (hours-scale, single focused change)

**Empty as of 2026-09-20, re-derived three times today** — the original
2026-09-19 survey's items shipped, then two follow-up re-derivations each
found exactly one more genuine item (both now shipped too), then a third
pass found nothing at all ("the well is dry" -- every remaining Non-goals/
deferred-work mention traces to already-shipped items, not-yet-applicable
conditions, or content already tracked in the Medium/Large tiers below).
Re-derive again once real new work has landed since the last pass — don't
immediately re-run a fourth time expecting different material from the
same unchanged source docs.

**Re-derived 2026-10-04** after eleven slices landed (#209–#219). Two
genuine quick wins now exist, both surfaced by those slices rather than by
re-reading the same source docs:

| Item | Why it is a quick win |
|---|---|
| **Exposure control** | One `float` through the existing lighting UBO (which already carries `camera_position`), one multiply before the tone map. The design question — where the knob lives — is the only real work. |
| **A suite-wide ctest `TIMEOUT`** | One `set_tests_properties` call. Recorded three times as a backstop; this session produced two hang-shaped bugs (the watchdog use-after-free and the GIL deadlock), so it is no longer hypothetical. The work is choosing the value against the slowest legitimate test, not writing the line. |

### 🟡 Medium (a real focused sub-project, bounded new machinery)

**Empty as of 2026-09-21.** All five original items shipped: Studio
plugin (`.py`) authoring via MCP (PR #193), per-plugin enable/disable
+ versioning (PR #194), plugin live file-watching/hot-reload
(PR #195), Studio theme system + persistence (PR #196), Studio
recent-projects list (PR #197).

**Re-derived 2026-10-04.** One genuine medium item now exists:

| Item | Shape |
|---|---|
| **An offscreen HDR render target plus a fullscreen resolve pass** | New render target, new pass, new pipeline, and a decision about where UI draws relative to the resolve. Bounded new machinery rather than a quick win, and it is the prerequisite for bloom. **Trigger, not preference:** the moment any additive-blending pipeline is added, in-shader tone mapping stops being defensible and this becomes necessary. |

Otherwise the next pick comes from the 🔴 Large
tier below — each of those items needs its own decomposition before
implementation starts.

### 🔴 Large (needs its own decomposition, multi-slice)

| Item | Impact |
|---|---|
| Blender mesh/geometry sync | Medium-High — the difference between a preview and a real DCC tool |
| Native `.so`/`.dll` plugins | High, but reopens native-code security surface |
| Rendering: full PBR material model — **decomposed 2026-10-04**, slices 1 (tone mapping) and 2 (metallic/roughness + GGX Cook-Torrance BRDF) shipped; slices 3 (texture maps) and 4 (IBL) remain, decomposed but unspecced | High |
| RenderGraph refactor | Medium (enabling, not user-visible) |
| UI pillar gaps | High if interactive-game UI is a near-term goal, else Low |
| Networking gaps | High if shipping real multiplayer soon, else Low |
| Package manager (network/deps/registry) | Medium |
| Public marketplace | Low today, High later — gated on an actual user base |
| 3rd-person adventure template | Medium |

**Stand up real CI (GitHub Actions) — SHIPPED 2026-09-21 (PR #198)**,
removed from the table above. `.github/workflows/ci.yml`: debug-preset
build, full `ctest`, headless `mosaic run --ticks N --json` smoke test,
on every push to `main` and every PR.

**Dependency-rule lint — SHIPPED 2026-09-21 (PR #199)**, the follow-up
named above. `tools/ci/check_dependency_rules.py` (+ its own unit
tests), wired into CI and `.githooks/pre-commit`. Surfaced and fixed a
real doc-vs-reality mismatch along the way: `ARCHITECTURE.md`/
`repo-layout.md` described a `Foundation → Engine → Editor → Studio`
layout with an `editor/` directory that was never built — rewritten to
describe the real tiers (`foundation/engine/studio/app` + `modules/`)
confirmed by grepping every `#include` direction in the tree. See the
`next-steps.md` entry for the full story, including the real
`app → studio` direction (opposite of what the old docs said) and the
CI-flake exclusion this PR's own run surfaced (tracked separately in
Tech debt above).

**Rendering: shadows — SHIPPED 2026-09-21 (PR #200)** and **spot lights
— SHIPPED 2026-09-21 (PR #201)**, both removed from the table above.
Directional-light-only, hard-edged, fixed-frustum shadow mapping, and a
third simultaneous light type reusing the point light's positional
machinery plus a `smoothstep` cone edge. Both extended direct
submission rather than building the RenderGraph, which stays deferred
(its own row above) until a genuinely *dynamic* pass need appears. The
per-slice deferrals each opened — PCF, point/spot-light shadows,
scene-fitted and cascaded frustums, configurable shadow resolution,
real multi-light support — are tracked in the Rendering section above,
not here.

**The old "Rendering: PBR/specular/spot lights" row is now only "full
PBR."** Decomposing it turned out to be the right call: spot lights
shipped as their own small slice, and the **shading-data UBO migration
— SHIPPED 2026-09-27 (PR #202)** (world-space shading, the codebase's
first uniform buffer, push constants 256 → 208 bytes) unblocked
**Blinn-Phong specular**, which is now a *small* slice rather than part
of a Large item — it needs 8 bytes of per-draw data against 48 bytes of
headroom, since world-space shading moved the camera position into the
per-frame UBO. Specular is tracked in the Rendering section above and
does **not** need its own decomposition pass. What genuinely remains
Large is the full metallic/roughness Cook-Torrance material model,
which still needs its own design decisions (scalar params vs. texture
maps for v1) and likely its own sub-decomposition.

**Picked as the largest effective win (2026-09-19, original pass):** bulk
refactoring / project health checks — **shipped**, all 4 sub-projects
complete (see specs above). Next pick TBD with the user.

**Scoping pass on "Stand up real CI" (2026-09-21, after the Medium tier
closed out)** — investigated the actual dependency chain rather than
guessing at a workflow file blind, since getting package names wrong
means a slow guess-push-fail loop with no one around to weigh
trade-offs. Findings from reading `CMakeLists.txt` and
`modules/render_vulkan/CMakeLists.txt` directly:
- Hard `REQUIRED` dependencies at configure time: `Threads`, system
  `Vulkan` (`find_package(Vulkan REQUIRED)` — needs the SDK/loader +
  headers installed, not just runtime-resolvable), `Python3`
  (Interpreter + Development, for the embedded pybind11 scripting),
  and a `glslc` binary on `PATH` (`find_program(GLSLC glslc
  REQUIRED)` — shaders are compiled to SPIR-V at *build* time, so a
  CI runner needs the compiler even though it never needs a real GPU
  to run it). GLFW is fetched and built from source via
  `FetchContent` — no system package needed for it.
- **The good news, confirmed by actually running the full suite
  locally and reading what it touches:** `ctest`/`mosaic_unit_tests`
  never creates a real `VkDevice`/window — it's headless logic
  (confirmed via this session's own repeated full-suite runs, all
  green, on a sandbox with no functioning GPU-backed Vulkan ICD).
  Only `mosaic run --window`/`mosaic editor` (never invoked by
  `ctest`) touch a real GPU. **This means CI likely does NOT need a
  working GPU or software rasterizer (lavapipe/llvmpipe) at all** —
  just the Vulkan *headers + loader* to satisfy `find_package`/link,
  and a real `glslc` binary to compile shaders at build time. This
  significantly de-risks the item versus the original "needs a full
  Vulkan toolchain in the runner" framing.
- **The actual open risk is `glslc` package availability on GitHub's
  `ubuntu-latest` runner image** — not confirmed by installing it
  anywhere, only reasoned about from memory (Ubuntu's own repos have
  historically been inconsistent about shipping a standalone `glslc`
  package versus only `glslang-tools`' `glslangValidator`, a
  different tool). The standard robust fix other Vulkan projects use
  is adding LunarG's official Vulkan SDK apt repository and
  installing the `vulkan-sdk` meta-package (guarantees loader,
  headers, validation layers, and `glslc` together) rather than
  hunting for the right combination of distro packages — but this
  needs an actual CI run to confirm, not more reading.
- **Not attempted tonight, deliberately:** writing and iterating on
  the actual `.github/workflows/*.yml` needs a real push-and-watch
  loop (`gh run watch`) to get the LunarG-repo-vs-distro-packages
  question and the exact CMake preset/build-type right, plus
  judgment calls this session's own practice would normally raise
  with the user first rather than decide alone at 1am: which preset
  (debug/release/both as a matrix?), whether to also gate on the
  dependency-rule check `CONTRIBUTING.md` already claims is
  "enforced" (it isn't, anywhere, today — a lint pass would need
  writing from scratch, not just wiring up a workflow), and whether
  a `ctest`-only pipeline is an acceptable v1 or whether launching
  `mosaic run --ticks N` headlessly (no `--window`) should also gate
  merges as a smoke test. **Next: brainstorm this properly with the
  user** (a real spec/plan cycle, not a chat-agreed bounded slice —
  this is the first item in the whole session that touches
  infrastructure outside the repo itself) before writing the
  workflow file.
