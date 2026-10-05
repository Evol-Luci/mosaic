# Rendering — Cubemaps, an HDR Image Path, and a Skybox (design)

**Date:** 2026-10-05
**Status:** approved (design), pending implementation plan
**Branch:** to be cut from `main` at `87ff619`

IBL sub-slice **4b**. The IBL decomposition lives in
`docs/project/deferred-work-backlog.md` under "Ambient/environment
specular (IBL)"; sub-slice 4a (mipmaps) shipped in #222. This document
specifies the environment data path and the skybox that makes it
visible. The IBL math itself — irradiance convolution, prefiltered
specular, the BRDF LUT — is 4c and is out of scope here.

## Intent

Load a real `.hdr` environment, convert it to a cubemap, and draw it as
a skybox. The scene gains an environment; 4c gains something to
convolve.

**Success criteria**

- `mosaic run --window` shows the demo sitting inside a visible HDR sky,
  with faces correctly oriented — no rotation, no mirroring, no visible
  seams.
- An environment whose bright regions exceed 1.0 rolls off rather than
  clipping flat.
- The cubemap is available to the renderer as an ordinary sampled
  resource, so 4c consumes it without further plumbing.
- Every existing LDR texture and every already-cooked asset keeps
  working, unchanged.

## What scoping measured

Five things, each of which shaped a decision below.

- **`Image` is RGBA8 by construction** — `std::vector<std::uint8_t>`,
  hardcoded four bytes per pixel, with the comment stating so. HDR is
  float by definition; that is the entire point of `.hdr`, values above
  1.0 for the sun that an LDR sky clips.
- **The cooked format is versioned by its magic** — `"MOSAIMG1"`, and
  `uncook_image` already rejects an unknown one. A `MOSAIMG2` is
  therefore a clean extension rather than a break.
- **Nothing draws a background.** The scene is a flat clear colour
  (`0.10, 0.10, 0.12`). There is no skybox and no environment rendering
  of any kind, so a cubemap alone would produce *no visible output*
  until 4c.
- **`stbi_loadf_from_memory` is available** once `STBI_NO_HDR` is lifted
  — a define added during PBR slice 3, when float had no route into
  `Image`. It now has one.
- **The lit pipeline uses `depthTestEnable = TRUE`,
  `depthWriteEnable = TRUE`, `depthCompareOp = LESS`.** A skybox must
  differ on two of the three.

## The HDR path

`Image` gains a format tag, keeping its byte storage:

```cpp
enum class ImageFormat { Rgba8, Rgba16F };

struct Image
{
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    ImageFormat               format = ImageFormat::Rgba8;
    std::vector<std::uint8_t> pixels; // width*height*(4 or 8)
};
```

Half-floats are bytes, so the storage does not change shape and every
existing caller keeps working — `Rgba8` is the default, and the size
rule simply becomes `w*h*4` or `w*h*8`.

The rejected alternatives were a separate `ImageHdr` struct with a
`std::vector<float>` (cleanest separation, but the registry, the texture
cache, the cooked format and the device each grow a second branch), and
packing to `RGB9E5` (`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32`, which leaves
`Image` untouched at four bytes per texel but is lossy, awkward to
assert on in a test, and still has to get its format to the device
somehow).

`STBI_NO_HDR` is removed from `image_cook.cpp`. `read_image` branches on
`stbi_is_hdr_from_memory`: HDR sources load through
`stbi_loadf_from_memory` with four desired channels and convert
float → half into the byte vector; everything else goes through the
existing path, untouched.

**Half-float, not full float.** `RGBA16F` is the standard runtime format
for an environment: half the memory of `RGBA32F`, natively filterable,
and its ~11-bit mantissa is ample for a sky. The precision loss on very
bright values is real and is accepted.

### The cooked format

`"MOSAIMG2"` — magic, `u32` width, `u32` height, `u32` format tag, then
pixels. `uncook_image` keeps reading `MOSAIMG1` as `Rgba8`, so every
asset already cooked on disk still loads. `cook_image` writes `MOSAIMG2`
from here on.

## Equirect → cube, converted at import

An `.hdr` is an equirectangular rectangle; a cubemap is six square
faces. The conversion happens **at import, on the CPU**, in a new
`app/asset/cubemap_cook.{hpp,cpp}` following the `image_cook`
precedent.

For each face and each texel: build the direction vector that texel
represents, convert to spherical coordinates, and sample the equirect
bilinearly.

```
u = atan2(d.z, d.x) / (2*PI) + 0.5
v = acos(clamp(d.y, -1, 1)) / PI
```

Output is a cooked blob, **`"MOSAICUBE1"`**: magic, `u32` face size,
`u32` format tag, then six faces in Vulkan's layer order — **+X, −X, +Y,
−Y, +Z, −Z**. Asset type `"cubemap"`, dispatched in `asset_io.hpp` on
the `.hdr` extension.

**The reason for choosing import-time CPU conversion is testability.**
The direction math is ordinary C++, so it gets real unit tests — and
face orientation is the classic bug in this area, one that otherwise
surfaces only as a visibly wrong sky on a machine with a GPU. CI has no
GPU; this keeps the most error-prone part of the slice inside what CI
can actually check.

The rejected alternatives were converting at load on the GPU (no new
asset format and a runtime-selectable face size, but it is new
render-to-texture machinery and none of the math would be covered by
any test), and requiring six pre-split face images (no conversion code
at all, but essentially every HDR environment available is
equirectangular, so the conversion would just move outside the engine
and be done by hand forever).

**Face size is fixed at 512.** It is a quality-versus-memory knob with
no second opinion to balance it yet; when something needs it to vary it
belongs in `AssetMetadata::import_settings`, which already exists for
exactly this. Changing it requires a re-import, which is the stated cost
of cooking at import time.

## Cubemaps in the render device

```cpp
virtual TextureHandle create_cubemap(const Image faces[6]) = 0;
```

One image with `arrayLayers = 6` and
`VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, a `VK_IMAGE_VIEW_TYPE_CUBE` view,
and `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` on all three axes. Cube
sampling is seamless across face edges in Vulkan by default, so no
device feature is required for that.

It shares the existing texture table and handle space, so a cubemap is
an ordinary `TextureHandle` to everything above the device.

**One mip level.** 4c's prefiltered specular is a *separate* cubemap
whose levels are written by convolution at increasing roughness — it is
not a blit-generated chain of this one. So 4b does not need sub-slice
4a's mip ladder extended to array layers, and deliberately does not do
it. A skybox barely minifies, so the aliasing argument that motivated
4a does not apply here.

## The skybox, and a consequence for tone mapping

A new pipeline and shader pair. The cube is drawn after opaque geometry
with `depthWriteEnable = FALSE` and `depthCompareOp = LEQUAL`, and the
vertex shader forces `gl_Position.z = gl_Position.w` so the fragment
lands exactly on the far plane and fills only pixels nothing else wrote.
The view matrix has its translation removed, so the sky does not move
with the camera.

**The skybox must tone map, and that invalidates a piece of slice 1's
reasoning.** The tone-mapping spec justified applying `tonemap_aces` in
`cube.frag` *only*, on the explicit grounds that every other
colour-producing fragment shader is unlit, cannot exceed 1.0, and would
merely be darkened by a curve that maps 1.0 to about 0.8. A skybox
sampling HDR radiance **can** exceed 1.0. It is the first shader for
which that argument does not hold, and the success criterion that bright
regions roll off rather than clip depends on it.

So `tonemap_aces` is duplicated into the skybox fragment shader, and a
contract test pins the two copies as textually identical. The
alternative — a shared GLSL include — needs an include mechanism the
shader build does not have, and introducing one is a build-system change
rather than a rendering one. The duplication is the smaller cost, and
the test is what keeps it honest; this paragraph exists so the next
person understands it was chosen rather than overlooked.

## Testing

- **Face direction math**, the highest-value tests in the slice: each
  face's centre texel maps to that face's expected axis; the four
  corners fall within tolerance of the expected diagonals; a
  direction → equirect UV → direction round trip is stable. These are
  what stand between a correct sky and a rotated or mirrored one.
- **Equirect sampling**: a known synthetic equirect (distinct solid
  colour per region) converts to faces whose centres carry the expected
  colours — which catches a U/V swap or a flipped V.
- **Cooked formats**: `MOSAIMG2` and `MOSAICUBE1` round-trip; a
  `MOSAIMG1` blob still loads as `Rgba8`; truncated and bad-magic blobs
  return `nullopt`.
- **Float → half** conversion on known values, including above 1.0,
  which is the case the whole HDR path exists for.
- **Source contracts** for `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`,
  `VK_IMAGE_VIEW_TYPE_CUBE`, `arrayLayers = 6`, the skybox's
  `depthWriteEnable = FALSE` / `LEQUAL` pair, the `z = w` trick, and the
  two `tonemap_aces` copies being identical. CI has no GPU, so these
  textual contracts are the only mechanism available — the same idiom
  the shader and colour-space contracts already use.
- **Vulkan validation** on both `--window` entry points, which is what
  catches an image-create-flag or view-type mismatch.

**The honest limit.** No test here can show the sky *looks* right. Face
orientation is verified by direction vectors rather than by pixels, and
a human looking at the window is still the final check — stated here
rather than letting a green suite imply more.

## Risks

**Face orientation is the likely bug and does not look like one.** A
wrong axis or a flipped V produces a rotated or mirrored sky, which
reads as a questionable asset rather than a defect. The direction unit
tests exist specifically because this failure mode is invisible to every
other kind of check.

**The two `tonemap_aces` copies can drift.** Only the contract test
holds them together, and a test is a weaker guarantee than a single
definition.

**Memory.** A 512² `RGBA16F` cubemap is about 6 MB, which is new and
permanent VRAM pressure, and 4c will add two more cubemaps on top.

**Half-float precision** on very bright values is lossy. Accepted for a
sky; it would not be for an HDR render target.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **Mip chains on the environment cubemap** — not needed by the skybox,
  and 4c's prefiltered cube is built by convolution rather than by
  blitting this one down.
- **Cubemap compression** — the cooked form stays uncompressed, the same
  decision the existing texture pipeline made.
- **Runtime environment switching, or more than one environment** — one
  cubemap, loaded once.
- **`.exr` and other HDR container formats** — `.hdr` (Radiance RGBE) is
  what stb_image reads without another dependency.
- **Dynamic or captured reflection probes** — a captured cubemap needs
  render-to-cubemap machinery, which is the alternative this design
  explicitly rejected for the import path.
- **A GLSL include mechanism** — the reason `tonemap_aces` is
  duplicated; introducing one is a build-system change.
- **Runtime-selectable face size** — fixed at 512 until
  `import_settings` has a reason to carry it.
