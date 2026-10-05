# Rendering — Metallic/Roughness Texture Maps and Real Image Import (design)

**Date:** 2026-10-04
**Status:** approved (design)
**Branch:** `render-pbr-texture-maps`, cut from `main` at `4187ccc`

Slice 3 of four. The decomposition lives in
`docs/superpowers/specs/2026-10-04-pbr-decomposition-and-tone-mapping-design.md`;
slice 1 (tone mapping) shipped in #219 and slice 2 (metallic/roughness
plus a GGX Cook-Torrance BRDF) in #220. This document specifies
per-texel `metallic` and `roughness`, and the image import they need to
be authorable at all.

## Intent

A material's `metallic` and `roughness` become **per-texel**, sampled
from an artist-authored map, rather than two scalars applied uniformly
across a whole surface. Uniform scalars are why every object in the demo
reads as one substance; a map is what makes a surface look like rusted
metal rather than like a metal-coloured ball.

**Success criteria**

- The demo contains one object whose roughness visibly varies across its
  surface, authored as an **image file** rather than as a C++ pixel loop.
- An untextured material behaves exactly as it does today — the scalars
  keep their current meaning, and no existing scene changes appearance.
- A metallic/roughness map's values arrive at the shader **undistorted**
  (see "The colour-space trap", which is the one way this slice fails
  silently).
- A PNG exported from an ordinary authoring tool imports without a
  bespoke conversion step.

## What was measured while scoping

Four things, each of which changed the design. They are recorded because
three of them contradicted a reasonable prior assumption.

- **Image import already exists end to end**, and an earlier claim in
  this project's own planning that "the engine has no image file loading"
  was **wrong**. `app/asset/asset_io.hpp` dispatches by file extension,
  `app/asset/image_cook.hpp::read_ppm` decodes binary PPM (P6),
  `cook_image`/`uncook_image` implement a cooked on-disk form
  (`"MOSAIMG1"` + `u32` width + `u32` height + RGBA bytes), and
  `AssetRegistry` resolves an `AssetID` to an `engine::render::Image`.
  The gap is **not** loading. The gap is that the only source format is
  PPM, which no authoring tool exports.
- **`third_party/stb/stb_truetype.h` is already vendored as a plain
  header**, not through `FetchContent`, and is used from
  `app/asset/font_cook.cpp` with a `#define ..._IMPLEMENTATION`.
  `stb_image.h` therefore drops in beside it with no new dependency
  mechanism and no CMake change beyond the source list.
- **`create_texture` hardcodes `VK_FORMAT_R8G8B8A8_SRGB`** for every
  texture it uploads (`vulkan_render_device.cpp`, the image and its
  view). That is correct for base colour and wrong for a data map.
- **A descriptor set belongs to a *texture*, not a material.** Set 0 is a
  single `COMBINED_IMAGE_SAMPLER` at binding 0; each registered texture
  gets its own set, and the lit draw path binds
  `textures_[handle - 1].set`. `kMaxTextures = 64` sizes both the pool
  and `maxSets`. Two maps per material cannot be expressed in that
  arrangement without changing who owns a set.

## Image import

Vendor `stb_image.h` into `third_party/stb/`, beside `stb_truetype.h`.
Add to `app/asset/image_cook.hpp`:

```cpp
// Decodes PNG/JPEG/BMP/TGA via stb_image, falling back to read_ppm.
std::optional<mosaic::engine::render::Image> read_image(const std::string& bytes);
```

`asset_io.hpp`'s import dispatch calls `read_image` where it currently
calls `read_ppm`. Everything downstream is unchanged: the cooked blob is
still `MOSAIMG1`, the asset type is still `"texture"`, and the upload
path is untouched — `MOSAIMG1` is already RGBA8, which is exactly what
stb_image produces with `desired_channels = 4`.

**`read_ppm` stays, and keeps its tests.** It is the only decoder with no
third-party code in the path, which makes it the honest fixture for
testing everything downstream of decode: a test that uses it is testing
our cook/uncook/upload chain, not stb_image's. `read_image` tries
stb_image first and falls back to it, so a PPM still imports.

**The implementation macro goes in exactly one translation unit.**
`stb_truetype`'s already lives in `font_cook.cpp`; `stb_image`'s gets a
new `app/asset/image_cook.cpp`, which means `image_cook.hpp` stops being
header-only. That is a deliberate, visible cost of this slice: today it
is `inline` functions in a header, and several tests include it.

## The colour-space trap

**This is the one way this slice produces a wrong image with a green
suite, so it gets its own section.**

`create_texture` uploads every texture as `VK_FORMAT_R8G8B8A8_SRGB`. For
base colour that is right and deliberate — the hardware applies the sRGB
EOTF on sample, so the shader receives linear values, which is what the
BRDF needs.

A metallic/roughness map is **data, not colour**. Its channels are not a
picture; they are numbers that happen to be stored in an image. Uploaded
as sRGB, an authored roughness of `0.5` (byte 128) arrives at the shader
as roughly `0.21`. Nothing crashes, no validation layer complains, and
the result looks like a plausible material — just the wrong one. Every
test in this repository would stay green.

So `RenderDevice::create_texture` gains a colour-space argument:

```cpp
enum class TextureColorSpace { Srgb, Linear };
virtual TextureHandle create_texture(const Image& image,
                                     TextureColorSpace space = TextureColorSpace::Srgb) = 0;
```

`Srgb` keeps today's `R8G8B8A8_SRGB`; `Linear` selects
`R8G8B8A8_UNORM`. The default preserves every existing call site's
behaviour, including the font atlas and the default white. Base colour
maps pass `Srgb`; metallic/roughness maps pass `Linear`.

The defaulted argument is a deliberate trade: it keeps this slice's diff
small and every existing caller correct, at the cost that a *future* data
map added by someone who does not read this section gets sRGB silently.
A contract test pins the two formats so that the decision is at least
visible when it is changed.

## Material, packing, and sampling

`Material` gains one field:

```cpp
AssetID metallic_roughness_texture{}; // nil = untextured; the scalars apply alone
```

**Channel packing follows the glTF 2.0 convention: G = roughness,
B = metallic.** This is a convention rather than a derivation — there is
no reason intrinsic to the data that roughness is green — but it is what
every authoring tool exports and every other renderer expects, and
choosing differently would mean every imported asset is wrong in a way
that looks like a material choice.

**Sampling follows glTF too: the scalar is a *factor*, multiplied by the
texel.**

```glsl
float metallic  = pc.metallic  * mr.b;
float roughness = pc.roughness * mr.g;
```

This is what makes the slice backward-compatible. An untextured material
samples a 1×1 white default map, so both texels are `1.0` and the scalars
pass through unchanged — every existing scene renders identically. It
also gives an artist the usual workflow: author the map once, then tune
the whole surface with the scalar.

The red sphere added in slice 2 keeps `metallic = 1.0`, so its map's blue
channel scales a metal rather than creating one.

## Descriptors: the set moves from the texture to the material

Set 0 gains binding 1 (a second `COMBINED_IMAGE_SAMPLER`, fragment
stage), and a descriptor set becomes owned by a **material-texture
combination** — the `(base_color_texture, metallic_roughness_texture)`
pair — instead of by a single texture. The lit draw path still binds
exactly one set, so the per-draw cost does not change.

The texture table keeps owning images, views, and samplers; only set
ownership moves. Sets are allocated lazily per distinct pair and cached,
because the number of *pairs* in use is what the pool must now be sized
for.

**`kMaxTextures = 64` sizes `maxSets` today and must be re-derived.**
Sets are no longer one-per-texture, and the pool must hold one per
distinct pair plus two descriptors each. The spec does not fix the new
number; the plan measures the demo's actual pair count and sets a limit
with headroom, and the device fails loudly rather than silently reusing a
set when the limit is reached.

Rejected alternatives, both recorded in the backlog:

- **Bind a second descriptor set (set 3) per draw.** Smallest possible
  diff — the texture table and pooling are untouched — but it spends a
  set slot per map type, so normal maps become set 4 and occlusion set 5,
  and it adds a bind to every lit draw.
- **Bindless: one descriptor array bound once per frame, indices in the
  push block.** The right end state — 40 of 256 push-constant bytes are
  free, and it would make every future map type cost nothing — but it
  needs `VK_EXT_descriptor_indexing` with a capability check and a
  fallback, and reworks registration, pooling, and the draw path at once.
  A pillar, not a slice. The chosen design does not block it and makes it
  smaller later, since set ownership will already have moved.

## The demo

One object gets a real authored map, to satisfy the success criterion
that something visibly varies. The map is committed as a **PNG** —
importing a real PNG is half of what this slice delivers, so the demo
must exercise that path rather than a procedural shortcut.

The red metal sphere is the natural candidate: it is already the
showcase, already `metallic = 1`, and a roughness gradient across it is
the clearest possible demonstration, since a metal's appearance is
entirely specular and roughness is what shapes the lobe.

## Testing

- **Decode:** `read_image` on a real PNG fixture returns the expected
  dimensions and known pixel values; on a PPM it still works (the
  fallback); on malformed bytes it returns `nullopt` rather than
  throwing or reading out of bounds.
- **The import chain:** a PNG through `read_image` → `cook_image` →
  `uncook_image` returns pixel-identical data. `read_ppm`'s existing
  tests stay.
- **Colour space:** a contract test pinning that `Srgb` maps to
  `R8G8B8A8_SRGB` and `Linear` to `R8G8B8A8_UNORM`, and that the default
  argument is `Srgb`. This is the assertion that protects the trap above.
- **Reflection round-trip** for `metallic_roughness_texture`, matching
  how `texture` is already covered.
- **Shader contract:** the glTF channel assignment (`.g` → roughness,
  `.b` → metallic) and the factor-times-texel form. Reading G and B
  backwards swaps roughness and metallic, which produces a plausible
  material rather than an obvious defect — exactly the class of error a
  textual contract exists to catch, since there is no GPU in CI.
- **Backward compatibility:** a material with a nil map produces the same
  `DrawParams` as today, and the default map is white.

**The honest limit.** As in slice 2, no test here can show the result
*looks* right — only that it is wired, decoded, packed, and uploaded in
the right colour space. Whether a roughness gradient reads as a gradient
is a human check against `mosaic run --window`.

## Risks

**The packing convention is invisible when wrong.** Swap G and B and the
sphere becomes a rough dielectric with metallic variation instead of a
metal with roughness variation. Both are materials; neither looks like a
bug. The shader contract test is the only thing standing between that
and a silent regression.

**stb_image is the first untrusted-input parser in the asset path.** A
malformed image file is now reachable from a project directory. stb_image
is widely deployed and has had its share of CVEs; the mitigation here is
that `read_image` returns `nullopt` on failure rather than propagating,
and that import is an explicit developer action rather than something
triggered by running a game. Recorded rather than solved.

**Pool sizing changes shape.** "Sets ≈ textures" becoming "sets ≈
material combinations" is a different growth curve, and a scene with many
distinct pairs can exhaust a pool that a scene with many textures would
not.

**This slice makes mipmaps visibly wanted.** A varying roughness map
aliases badly at distance without them — shimmering specular is the
classic symptom. The engine generates no mipmaps today, so the demo will
show it. That is a true consequence of shipping this, not a defect in it.

## Non-goals

All recorded in `docs/project/deferred-work-backlog.md`:

- **Normal mapping, and the tangent-frame question it forces** —
  `Vertex` is `{position, normal, uv}` with no tangent, so it needs
  either per-fragment derivative tangents or a vertex-format change.
- **Occlusion and emissive maps** — the rest of the glTF standard set.
  An AO map is meaningless until there is an ambient term worth
  occluding (IBL, slice 4).
- **Bindless textures** — the end state this is heading toward; see the
  rejected alternatives above for the trigger.
- **Mipmaps and anisotropic filtering** — the aliasing this slice
  surfaces is real, but mip generation, sampler LOD state, and the
  upload path's extra transitions are their own slice.
- **A texture-compression or GPU-format pipeline** (BCn/ASTC) — the
  cooked form stays uncompressed RGBA8.
- **Per-material UV transforms or a second UV set** — one UV channel,
  used by both maps.
