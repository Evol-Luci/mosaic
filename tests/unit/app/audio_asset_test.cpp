#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h> // ::getpid

#include <catch2/catch_test_macros.hpp>

#include "app/asset/asset_io.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/audio/audio_clip.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"

using mosaic::app::create_project;
using mosaic::app::import_asset;
using mosaic::app::load_project_assets;
using mosaic::app::save_scene_file;
using mosaic::audio::miniaudio::create_miniaudio_backend;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::audio::AudioClip;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

namespace fs = std::filesystem;

namespace
{
std::string tmp(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_a3_") + tag + std::to_string(::getpid()))).string();
}
void put_u16(std::string& b, std::uint16_t v) { b.push_back(char(v & 0xFF)); b.push_back(char((v >> 8) & 0xFF)); }
void put_u32(std::string& b, std::uint32_t v)
{
    b.push_back(char(v & 0xFF)); b.push_back(char((v >> 8) & 0xFF));
    b.push_back(char((v >> 16) & 0xFF)); b.push_back(char((v >> 24) & 0xFF));
}
// A 2-channel 16-bit PCM WAV, `frames` frames of mid-level tone.
std::string make_wav16(std::uint32_t rate, std::uint32_t frames)
{
    const std::uint16_t ch = 2, bits = 16, block = 4;
    const std::uint32_t dataLen = frames * block;
    std::string b;
    b += "RIFF"; put_u32(b, 36 + dataLen); b += "WAVE";
    b += "fmt "; put_u32(b, 16); put_u16(b, 1); put_u16(b, ch);
    put_u32(b, rate); put_u32(b, rate * block); put_u16(b, block); put_u16(b, bits);
    b += "data"; put_u32(b, dataLen);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        put_u16(b, static_cast<std::uint16_t>(static_cast<std::int16_t>(8000)));  // L
        put_u16(b, static_cast<std::uint16_t>(static_cast<std::int16_t>(-8000))); // R
    }
    return b;
}
ComponentReflection make_types(TypeRegistry& r) { ComponentReflection t; register_builtin_components(r, t); return t; }
} // namespace

TEST_CASE("import_asset cooks a .wav into an audio asset, and load registers an AudioClip", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("src") + ".wav";
    REQUIRE(save_scene_file(src, make_wav16(48000, 1000)));

    const auto id = import_asset(proj, src);
    REQUIRE(id.has_value());
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".audioclip")));
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".toml")));

    AssetRegistry registry;
    const int     n = load_project_assets(registry, (fs::path(proj) / "assets").string());
    REQUIRE(n == 1);
    REQUIRE(registry.contains(*id));
    auto clip = registry.load<AudioClip>(*id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->channels == 2);
    REQUIRE(clip->sample_rate == 48000);
    REQUIRE(clip->frames == 1000);
    REQUIRE(clip->pcm.size() == 2000);

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("a loaded AudioClip plays through the miniaudio null backend", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj2");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("src2") + ".wav";
    REQUIRE(save_scene_file(src, make_wav16(48000, 4000)));
    const auto id = import_asset(proj, src);
    REQUIRE(id.has_value());

    AssetRegistry registry;
    REQUIRE(load_project_assets(registry, (fs::path(proj) / "assets").string()) == 1);
    auto clip = registry.load<AudioClip>(*id);
    REQUIRE(clip != nullptr);

    auto backend = create_miniaudio_backend(/*null_device=*/true);
    REQUIRE(backend != nullptr);
    REQUIRE(backend->register_clip(*id, clip->pcm.data(), clip->frames, clip->channels,
                                   clip->sample_rate));
    const auto voice = backend->play(*id, 1.0f, 1.0f, false, true);
    REQUIRE(voice != 0);
    REQUIRE(backend->is_playing(voice));

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("import_asset rejects a non-WAV .wav", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj3");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("src3") + ".wav";
    REQUIRE(save_scene_file(src, "this is not a wav file"));
    REQUIRE_FALSE(import_asset(proj, src).has_value());

    fs::remove_all(proj); fs::remove(src);
}
