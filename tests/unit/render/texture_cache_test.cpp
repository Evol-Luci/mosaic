#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/texture_cache.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureCache;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::TextureColorSpace;
using mosaic::foundation::serialization::AssetID;

namespace
{
class CountingDevice : public RenderDevice
{
public:
    int texture_uploads = 0;
    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override { return 1; }
    TextureHandle create_texture(const Image& img, TextureColorSpace) override
    {
        if (img.pixels.empty()) return 0;
        return static_cast<TextureHandle>(++texture_uploads);
    }
    void draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};

AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

// A registered Image of the given pixel size. The dimensions must be unequal in the
// cases that use it: equal width and height pass with the two transposed.
AssetID register_image(AssetRegistry& registry, std::uint8_t tag, std::uint32_t w,
                       std::uint32_t h)
{
    auto img = std::make_shared<Image>();
    img->width = w;
    img->height = h;
    img->pixels.assign(static_cast<std::size_t>(w) * h * 4u, 255);
    const AssetID id = make_id(tag);
    registry.register_resource<Image>(AssetMetadata{id, "texture"}, img);
    return id;
}

std::shared_ptr<Image> checker()
{
    auto img = std::make_shared<Image>();
    img->width = 1;
    img->height = 1;
    img->pixels = {255, 255, 255, 255};
    return img;
}
} // namespace

TEST_CASE("TextureCache uploads once and caches by AssetID", "[render]")
{
    AssetRegistry registry;
    const AssetID id = make_id(1);
    registry.register_resource<Image>(AssetMetadata{id, "texture"}, checker());

    CountingDevice device;
    TextureCache cache;

    const TextureHandle h1 = cache.get(id, registry, device);
    const TextureHandle h2 = cache.get(id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h2 == h1);
    REQUIRE(device.texture_uploads == 1); // uploaded once, then cached
}

TEST_CASE("TextureCache returns 0 for a nil or unregistered id", "[render]")
{
    AssetRegistry registry;
    CountingDevice device;
    TextureCache cache;
    REQUIRE(cache.get(AssetID{}, registry, device) == 0);        // nil
    REQUIRE(cache.get(make_id(9), registry, device) == 0);       // unregistered
    REQUIRE(device.texture_uploads == 0);
}

TEST_CASE("TextureCache reports the pixel dimensions alongside the handle", "[render]")
{
    // The dimensions are already in hand when the texture is created -- get() loaded the
    // Image to call create_texture and then threw them away. Aspect modes need them at
    // draw time, and loading the Image a second time per frame to recover a number the
    // cache already saw would be the wrong trade.
    //
    // Non-square and unequal on purpose: equal dimensions pass with width and height
    // transposed.
    CountingDevice device;
    AssetRegistry registry;
    const AssetID id = register_image(registry, 1, /*w=*/64, /*h=*/32);

    TextureCache cache;
    const auto info = cache.get_info(id, registry, device);
    REQUIRE(info.handle != 0);
    REQUIRE(info.width == 64u);
    REQUIRE(info.height == 32u);
}

TEST_CASE("a non-zero handle always comes with non-zero dimensions", "[render]")
{
    // The invariant RenderSystem relies on, so it needs no "dimensions unknown" branch
    // and no frame can show the whole atlas while waiting for a load. RenderSystem
    // already skips unresolved textures with `if (tex == 0) return`, so this pairing is
    // what makes that skip sufficient.
    CountingDevice device;
    AssetRegistry registry;
    TextureCache cache;

    const auto missing = cache.get_info(AssetID{}, registry, device);
    REQUIRE(missing.handle == 0);
    // An unregistered id is the other way to miss, and it must not record dimensions
    // either -- otherwise a later successful load for the same id is shadowed.
    const auto unregistered = cache.get_info(make_id(9), registry, device);
    REQUIRE(unregistered.handle == 0);
    REQUIRE(unregistered.width == 0u);
    REQUIRE(unregistered.height == 0u);

    const AssetID id = register_image(registry, 2, 8, 4);
    const auto ok = cache.get_info(id, registry, device);
    REQUIRE(ok.handle != 0);
    REQUIRE(ok.width > 0u);
    REQUIRE(ok.height > 0u);
}

TEST_CASE("a FAILED upload records no dimensions even though the Image loaded", "[render]")
{
    // Added because a mutation probe found this uncovered: moving the `info.handle != 0`
    // guard off the width/height assignment left the whole suite GREEN.
    //
    // The two ways to miss are not the same. An unregistered id never loads an Image, so
    // the recording code never runs and any guard there is untested. The case that
    // actually exercises the guard is an Image that loads and then FAILS to upload --
    // here, empty pixels, which CountingDevice reports as handle 0, matching a real
    // backend that declines a malformed image.
    //
    // Without the guard this records 64x32 beside handle 0, and the invariant
    // RenderSystem leans on ("a non-zero handle implies non-zero dimensions") inverts into
    // something worse: a cached entry that claims dimensions for a texture that does not
    // exist, pinned for the process's lifetime because the miss is cached too.
    CountingDevice device;
    AssetRegistry registry;
    auto img = std::make_shared<Image>();
    img->width = 64;
    img->height = 32;
    // pixels deliberately left empty -> CountingDevice::create_texture returns 0
    const AssetID id = make_id(5);
    registry.register_resource<Image>(AssetMetadata{id, "texture"}, img);

    TextureCache cache;
    const auto info = cache.get_info(id, registry, device);
    REQUIRE(info.handle == 0);
    REQUIRE(info.width == 0u);
    REQUIRE(info.height == 0u);
}

TEST_CASE("get_info uploads at most once per id and space", "[render]")
{
    // The caching contract get() already has, now on the path RenderSystem will call
    // every frame for every image. A regression here is a per-frame GPU upload, which
    // shows up as a frame-time cliff rather than as a wrong image.
    CountingDevice device;
    AssetRegistry registry;
    const AssetID id = register_image(registry, 3, 16, 16);

    TextureCache cache;
    const auto a = cache.get_info(id, registry, device);
    const auto b = cache.get_info(id, registry, device);
    REQUIRE(a.handle == b.handle);
    REQUIRE(a.width == b.width);
    REQUIRE(device.texture_uploads == 1);
}

TEST_CASE("get returns the same handle get_info does", "[render]")
{
    // get() keeps its signature because three of its four callers do not want
    // dimensions. This pins that it stays a thin view of the same cache rather than a
    // second code path that could drift.
    CountingDevice device;
    AssetRegistry registry;
    const AssetID id = register_image(registry, 4, 16, 8);

    TextureCache cache;
    REQUIRE(cache.get(id, registry, device) == cache.get_info(id, registry, device).handle);
    REQUIRE(device.texture_uploads == 1); // and still cached across both accessors
}
