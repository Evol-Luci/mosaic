# Rendering 36d-2 — Texture — Design

**Issue:** [#45 — 36d: Material + texture + directional light](https://github.com/Evol-Luci/mosaic/issues/45) (**second of two sub-slices**; 36d-1 delivered lighting)
**Tracking:** [#36 Rendering](https://github.com/Evol-Luci/mosaic/issues/36) · **Builds on:** [36d-1 lighting](2026-08-18-render-36d1-lighting-design.md)
**Status:** Accepted (author-approved; user delegated approval)
**Date:** 2026-08-22

## Purpose

Texture the lit cube: sample a texture per fragment, resolved from the `Material`'s
`AssetID` through the asset registry (#20). This introduces the first real asset
resource type (a CPU `Image`), Vulkan texture upload + a sampler + **descriptor sets**,
and UVs. Completes #45.

## Scope

- `foundation/math::Vec2`; `Vertex` gains a `uv`; the cube generator emits per-face UVs.
- `engine/render/image.hpp` — `Image` (RGBA8 CPU pixels), the asset resource for textures.
- `RenderDevice::create_texture(Image) -> TextureHandle`; `draw` grows a `TextureHandle`.
- `Material` gains an `AssetID texture` (nil = untextured); a `TextureCache` resolves
  `AssetID → TextureHandle` via the registry, cached.
- Vulkan: sampled image + view + sampler, a descriptor-set layout/pool/set per texture,
  bound per draw; the fragment shader samples the texture.
- Demo: a procedural checkerboard `Image` registered in the asset registry; the cube's
  `Material.texture` points at it.

## Out of scope (later)

- Real image-file decoding (PNG/JPG via stb_image) + importers/cookers — a Tools follow-up.
- Mipmaps, anisotropy, multiple textures per material (normal/roughness maps) — later.
- Point light + render graph — 36e (#46).

## Architectural spine (inherited)

Backend-neutral: `Image`/`TextureHandle`/`Material`/`TextureCache`/`RenderSystem` live in
`engine/render` (Vulkan-free); `Vec2` in `foundation/math`. The Vulkan backend adds the
texture/sampler/descriptor-set machinery.

## Components & interfaces

### `foundation/math/vec2.hpp`

```cpp
namespace mosaic::foundation::math
{
struct Vec2 { float x = 0.0f; float y = 0.0f; };
inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
template <class V> void reflect(V& v, Vec2& o) { v.member("x", o.x); v.member("y", o.y); }
}
```

### `Vertex` gains a uv (`engine/render/render_device.hpp`)

```cpp
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;
    mosaic::foundation::math::Vec2 uv;
};
```

The cube generator emits per-face UVs (each face's four corners map to (0,0),(1,0),(1,1),(0,1)).

### `engine/render/image.hpp`

```cpp
namespace mosaic::engine::render
{
// A CPU-side RGBA8 image (row-major, 4 bytes/pixel). The asset resource a texture
// AssetID resolves to; the backend uploads it via create_texture.
struct Image
{
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    std::vector<std::uint8_t> pixels; // size == width*height*4
};
}
```

### `RenderDevice` growth

```cpp
using TextureHandle = std::uint32_t; // opaque; 0 == invalid / default

virtual TextureHandle create_texture(const Image& image) = 0;
virtual void draw(MeshHandle mesh, TextureHandle texture, const DrawParams& params) = 0;
```

`draw` binds `texture`'s descriptor set. `create_texture` with an empty/zero-size image
returns `0`.

### `Material` gains a texture

```cpp
struct Material
{
    mosaic::foundation::math::Vec3 base_color{0.8f, 0.8f, 0.8f};
    mosaic::foundation::serialization::AssetID texture{}; // nil = untextured
};
template <class V> void reflect(V& v, Material& m)
{
    v.member("base_color", m.base_color);
    v.member("texture", m.texture);
}
```

### `engine/render/texture_cache.hpp`

```cpp
class TextureCache
{
public:
    // Resolve an AssetID to a device TextureHandle, caching the result. Nil id or a
    // missing/failed asset returns 0. First hit runs registry.load<Image>(id) +
    // device.create_texture; later hits return the cached handle.
    TextureHandle get(AssetID id, AssetRegistry& registry, RenderDevice& device);
private:
    std::unordered_map<AssetID, TextureHandle> cache_; // uses std::hash<AssetID>
};
```

### `RenderSystem` change

`render` grows an `AssetRegistry&`: `render(World&, RenderDevice&, AssetRegistry&)`. Per
mesh with a `Material`:
- `TextureHandle tex = mat.texture is nil ? default_white(device) : cache_.get(mat.texture, registry, device)`.
- If `tex == 0` (untextured or unresolved) → use a **1×1 white default texture** (created
  once, lazily) so the shader always samples (`white × base_color × light` == the 36d-1
  lit look).
- `device.draw(mesh, tex, DrawParams{...})` (DrawParams unchanged from 36d-1).

The `TextureCache` is a `RenderSystem` member; the default-white handle is a lazily
created `mutable` member.

## Vulkan backend (descriptor sets)

- **`create_texture(Image)`**: staging buffer (HOST_VISIBLE) → `memcpy` pixels; device-local
  `VkImage` (`R8G8B8A8_SRGB`, `TRANSFER_DST | SAMPLED`); layout transitions
  `UNDEFINED → TRANSFER_DST_OPTIMAL` (copy) `→ SHADER_READ_ONLY_OPTIMAL` (one-time-submit
  barriers + `vkCmdCopyBufferToImage`); an image view; a `VkSampler` (linear filter, repeat
  address). Allocate a descriptor set (from a pool) for binding 0 (combined image sampler)
  and update it to this view+sampler. Store `{image, memory, view, sampler, set}` in
  `textures_`; handle = index+1.
- **Descriptor set layout** (binding 0: `COMBINED_IMAGE_SAMPLER`, `FRAGMENT`) created at
  init; the **pipeline layout** gains it (set 0) alongside the existing push-constant range.
  A descriptor pool sized for a modest number of textures (e.g. 64), created at init.
- **`draw(mesh, texture, params)`**: `vkCmdBindDescriptorSets(set 0 = textures_[texture-1].set)`,
  push constants (unchanged), `vkCmdDrawIndexed`. Guard `texture == 0`/out-of-range by
  binding the device's own default white texture (create_texture on a 1×1 white image at
  init) so a set is always bound.
- **Vertex input** gains the uv attribute (`loc2`, `R32G32_SFLOAT`, `offsetof(Vertex, uv)`).
- **Teardown**: destroy each texture (sampler, view, image, memory), the descriptor pool,
  and the set layout before the device.

## Shaders

`cube.vert` passes `uv` through (add `layout(location=2) in vec2 inUV; layout(location=1)
out vec2 fragUV;`). `cube.frag`:

```glsl
layout(set = 0, binding = 0) uniform sampler2D tex;
// ... existing push block + fragNormal ...
layout(location = 1) in vec2 fragUV;
void main() {
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l));
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;
    outColor = vec4(albedo * (0.1 + lambert * pc.light_color.xyz), 1.0);
}
```

## Data flow

```
Material.texture (AssetID) --RenderSystem--> TextureCache.get(id, registry, device)
   miss: registry.load<Image>(id) -> device.create_texture(img) -> TextureHandle (cached)
device.draw(mesh, tex, DrawParams) --backend--> bind textures_[tex-1].descriptor_set
   frag: texture(tex, uv).rgb * base_color * (ambient + lambert*light_color)
```

## Error handling / edge cases

- Nil `Material.texture` or missing asset → `TextureCache` returns `0` → RenderSystem uses
  the 1×1 white default → mesh renders as if untextured (lit base color).
- `create_texture` with an empty image → `0`.
- `draw` with texture `0`/out-of-range → binds the default white set (never an unbound set).
- Registry `load<Image>` returning null → `0` (guarded).

## Testing

Headless (fake `RenderDevice` + a real `AssetRegistry`) + smoke.

- **`Vec2` (`[math]`)** — construction, equality.
- **`TextureCache` (`[render]`)** — a fake device counts `create_texture` calls and returns
  incrementing handles; register an `Image` in a real `AssetRegistry`: first `get` uploads
  (count 1) + returns a non-zero handle; a second `get` of the same id returns the same
  handle without another upload (count still 1); a nil `AssetID` → 0; an unregistered id → 0.
- **`RenderSystem` texture resolution (`[render]`)** — a mesh with a `Material.texture`
  pointing at a registered `Image` records a draw whose `TextureHandle` matches the cache's;
  an untextured material records the default-white handle (non-zero, stable across draws).
- **`Material` reflection round-trip (`[reflection]`)** — now includes the `texture`
  AssetID; round-trips preserved (base_color + texture).
- **Cube UVs (`[render]`)** — 24 vertices, each `uv` in [0,1]; each face's corners cover
  (0,0)/(1,0)/(1,1)/(0,1).
- **GPU** — build + `mosaic run --window`: the cube shows a **checkerboard texture**,
  lit by the directional light, spinning; resizing keeps rendering; closing exits 0.

## Demo (windowed mode)

Generate a procedural checkerboard `Image` (e.g. 8×8 or 64×64, two-color), register it in a
new `AssetRegistry` owned by `main` (`register_resource<Image>(metadata{uuid, type:"texture"},
shared_ptr<Image>)`), and set the cube's `Material.texture` to that `AssetID`. Pass the
registry to `render_system.render(world, device, registry)`.

## Acceptance criteria (completes #45)

- [ ] A textured mesh renders lit by a directional light; the checkerboard is visible and shaded.
- [ ] The texture resolves through the asset registry (#20) via an `AssetID`, cached (uploaded once).
- [ ] `Material` (incl. its texture `AssetID`) reflects + round-trips through serialization.
- [ ] `TextureCache` resolution/caching + cube UVs are unit-tested headlessly; the windowed
      smoke shows the textured cube.

## Tier / dependency contract

- `Image`/`TextureHandle`/`Material`/`TextureCache`/`RenderSystem` stay in `engine/render`
  (Vulkan/GLFW-free); `Vec2` in `foundation/math`. The Vulkan backend owns all
  texture/sampler/descriptor-set code. Depends on 36d-1, the asset registry (#20), and
  reflection (#18). No new third-party dependency (the texture is procedural).
