# Audio A3 — cooked audio-clip asset — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import a `.wav` as a project audio asset — parse + cook at import, register a decoded `AudioClip` resource at load — mirroring the T5 image pipeline.

**Architecture:** A neutral `engine/audio/audio_clip.hpp` resource; an `app/asset/audio_cook.hpp` (`read_wav`/`cook_audio`/`uncook_audio`) modeled on `image_cook.hpp`; and `.wav`/`"audio"` branches added to `import_asset`/`load_project_assets`. Decode-at-cook keeps runtime load dependency-free. Not wired to the backend/`setup_runtime` (that's A4).

**Tech Stack:** C++20, header-only, Catch2, CMake/CTest. No new dependencies (our own minimal WAV parser; no miniaudio in the importer).

**Spec:** [docs/superpowers/specs/2026-09-07-audio-a3-clip-asset-design.md](2026-09-07-audio-a3-clip-asset-design.md)

## Global Constraints

- C++20; header-only under `engine/audio/` and `app/asset/`.
- `read_wav` supports **only** uncompressed 16-bit integer PCM (`audioFormat 1`, `bits 16`) and IEEE 32-bit float (`audioFormat 3`, `bits 32`); `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`) resolves via the SubFormat GUID's tag. Anything else → `nullopt`.
- Cooked form: `"MOSAAUD1"` + `u32 channels` + `u32 sample_rate` + `u32 frames` (LE) + raw interleaved f32 sample bytes.
- `AudioClip` is a runtime resource (registered via `register_resource<AudioClip>`), **not** a reflected component.
- Same import safety as images: refuse to overwrite on a stem collision; roll back the blob if the metadata write fails.
- New tests tagged `[app]`, added to `tests/CMakeLists.txt`. `mosaic_audio_miniaudio` is already linked into `mosaic_unit_tests` (from A2). Build target `mosaic_unit_tests`; preset build dir `build/debug`.

---

### Task 1: `AudioClip` resource + WAV cooker

**Files:**
- Create: `engine/audio/audio_clip.hpp`
- Create: `app/asset/audio_cook.hpp`
- Create: `tests/unit/app/audio_cook_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::audio::AudioClip`; `mosaic::app::read_wav(const std::string&) -> std::optional<AudioClip>`, `cook_audio(const AudioClip&) -> std::string`, `uncook_audio(const std::string&) -> std::optional<AudioClip>`.

- [ ] **Step 1: Create `engine/audio/audio_clip.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace mosaic::engine::audio
{
// Decoded PCM audio: interleaved f32 (per frame, one sample per channel in order).
// A runtime resource (like render::Image) resolved by AssetID via the AssetRegistry;
// NOT a reflected component.
struct AudioClip
{
    std::vector<float> pcm;
    std::uint32_t      channels    = 0;
    std::uint32_t      sample_rate = 0;
    std::uint64_t      frames      = 0; // pcm.size() == frames * channels
};
} // namespace mosaic::engine::audio
```

- [ ] **Step 2: Create `app/asset/audio_cook.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

#include "engine/audio/audio_clip.hpp"

namespace mosaic::app
{
namespace detail_wav
{
inline std::uint16_t rd_u16(const std::string& b, std::size_t o)
{
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[o]) |
                                      (static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[o + 1])) << 8));
}
inline std::uint32_t rd_u32(const std::string& b, std::size_t o)
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[o])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[o + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[o + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[o + 3])) << 24);
}
inline float rd_f32(const std::string& b, std::size_t o)
{
    const std::uint32_t u = rd_u32(b, o);
    float               f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}
} // namespace detail_wav

// Parse an uncompressed WAV (16-bit PCM or 32-bit float) into an AudioClip (interleaved
// f32). nullopt on a non-WAV, unsupported format/bits, or a malformed/truncated file.
inline std::optional<mosaic::engine::audio::AudioClip> read_wav(const std::string& bytes)
{
    using namespace detail_wav;
    if (bytes.size() < 12) { return std::nullopt; }
    if (bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) { return std::nullopt; }

    std::size_t   pos       = 12;
    bool          have_fmt  = false;
    bool          have_data = false;
    std::uint16_t fmt = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    std::size_t   data_off = 0, data_len = 0;

    while (pos + 8 <= bytes.size())
    {
        const std::string   id  = bytes.substr(pos, 4);
        const std::uint32_t sz  = rd_u32(bytes, pos + 4);
        const std::size_t   pay = pos + 8;
        if (pay + sz > bytes.size()) { return std::nullopt; } // truncated chunk
        if (id == "fmt " && sz >= 16)
        {
            fmt      = rd_u16(bytes, pay + 0);
            channels = rd_u16(bytes, pay + 2);
            rate     = rd_u32(bytes, pay + 4);
            bits     = rd_u16(bytes, pay + 14);
            if (fmt == 0xFFFE && sz >= 40) { fmt = rd_u16(bytes, pay + 24); } // EXTENSIBLE subformat
            have_fmt = true;
        }
        else if (id == "data")
        {
            data_off  = pay;
            data_len  = sz;
            have_data = true;
        }
        pos = pay + sz + (sz & 1u); // chunks padded to even length
    }

    if (!have_fmt || !have_data || channels == 0 || rate == 0) { return std::nullopt; }

    const bool is_pcm16 = (fmt == 1 && bits == 16);
    const bool is_f32   = (fmt == 3 && bits == 32);
    if (!is_pcm16 && !is_f32) { return std::nullopt; }

    const std::uint32_t bytes_per = is_pcm16 ? 2u : 4u;
    const std::uint32_t block     = static_cast<std::uint32_t>(channels) * bytes_per;
    if (block == 0 || data_len % block != 0) { return std::nullopt; }
    const std::uint64_t frames = data_len / block;
    if (frames > std::numeric_limits<std::uint32_t>::max()) { return std::nullopt; } // cook stores u32

    mosaic::engine::audio::AudioClip clip;
    clip.channels    = channels;
    clip.sample_rate = rate;
    clip.frames      = frames;
    clip.pcm.resize(static_cast<std::size_t>(frames) * channels);
    for (std::size_t i = 0; i < clip.pcm.size(); ++i)
    {
        const std::size_t o = data_off + i * bytes_per;
        if (is_pcm16)
        {
            const std::int16_t s = static_cast<std::int16_t>(rd_u16(bytes, o));
            clip.pcm[i]          = static_cast<float>(s) / 32768.0f;
        }
        else
        {
            clip.pcm[i] = rd_f32(bytes, o);
        }
    }
    return clip;
}

// Cooked on-disk form: "MOSAAUD1" + u32 channels + u32 sample_rate + u32 frames (LE)
// + raw interleaved f32 sample bytes.
inline std::string cook_audio(const mosaic::engine::audio::AudioClip& clip)
{
    std::string out;
    auto        put_u32 = [&](std::uint32_t v)
    {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    out.reserve(20 + clip.pcm.size() * sizeof(float));
    out += "MOSAAUD1";
    put_u32(clip.channels);
    put_u32(clip.sample_rate);
    put_u32(static_cast<std::uint32_t>(clip.frames));
    out.append(reinterpret_cast<const char*>(clip.pcm.data()), clip.pcm.size() * sizeof(float));
    return out;
}

// Inverse of cook_audio. nullopt on bad magic, truncation, or a size mismatch.
inline std::optional<mosaic::engine::audio::AudioClip> uncook_audio(const std::string& bytes)
{
    using namespace detail_wav;
    if (bytes.size() < 20 || bytes.compare(0, 8, "MOSAAUD1") != 0) { return std::nullopt; }
    const std::uint32_t channels = rd_u32(bytes, 8);
    const std::uint32_t rate     = rd_u32(bytes, 12);
    const std::uint32_t frames   = rd_u32(bytes, 16);
    if (channels == 0 || rate == 0) { return std::nullopt; }
    const std::uint64_t samples = static_cast<std::uint64_t>(frames) * channels;
    if (samples > std::numeric_limits<std::size_t>::max() / sizeof(float)) { return std::nullopt; }
    const std::size_t need = static_cast<std::size_t>(samples) * sizeof(float);
    if (bytes.size() - 20 != need) { return std::nullopt; }

    mosaic::engine::audio::AudioClip clip;
    clip.channels    = channels;
    clip.sample_rate = rate;
    clip.frames      = frames;
    clip.pcm.resize(static_cast<std::size_t>(samples));
    std::memcpy(clip.pcm.data(), bytes.data() + 20, need);
    return clip;
}
} // namespace mosaic::app
```

- [ ] **Step 3: Create `tests/unit/app/audio_cook_test.cpp`**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "app/asset/audio_cook.hpp"

using Catch::Approx;
using mosaic::app::cook_audio;
using mosaic::app::read_wav;
using mosaic::app::uncook_audio;
using mosaic::engine::audio::AudioClip;

namespace
{
void put_u16(std::string& b, std::uint16_t v)
{
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put_u32(std::string& b, std::uint32_t v)
{
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
    b.push_back(static_cast<char>((v >> 16) & 0xFF));
    b.push_back(static_cast<char>((v >> 24) & 0xFF));
}
// A canonical uncompressed WAV from interleaved f32 samples: bits=16 -> PCM, bits=32 -> float.
std::string make_wav(std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                     const std::vector<float>& s)
{
    const std::uint16_t block   = static_cast<std::uint16_t>(channels * (bits / 8));
    const std::uint32_t dataLen = static_cast<std::uint32_t>(s.size() * (bits / 8));
    std::string         b;
    b += "RIFF";
    put_u32(b, 36 + dataLen);
    b += "WAVE";
    b += "fmt ";
    put_u32(b, 16);
    put_u16(b, bits == 16 ? 1 : 3);
    put_u16(b, channels);
    put_u32(b, rate);
    put_u32(b, rate * block);
    put_u16(b, block);
    put_u16(b, bits);
    b += "data";
    put_u32(b, dataLen);
    for (float x : s)
    {
        if (bits == 16)
        {
            long v = std::lround(x * 32767.0f);
            if (v > 32767) { v = 32767; }
            if (v < -32768) { v = -32768; }
            put_u16(b, static_cast<std::uint16_t>(static_cast<std::int16_t>(v)));
        }
        else
        {
            std::uint32_t u;
            std::memcpy(&u, &x, sizeof(float));
            put_u32(b, u);
        }
    }
    return b;
}
} // namespace

TEST_CASE("read_wav parses 16-bit PCM", "[app]")
{
    const std::string wav = make_wav(2, 44100, 16, {0.5f, -0.5f, 1.0f, -1.0f});
    const auto        clip = read_wav(wav);
    REQUIRE(clip.has_value());
    REQUIRE(clip->channels == 2);
    REQUIRE(clip->sample_rate == 44100);
    REQUIRE(clip->frames == 2);
    REQUIRE(clip->pcm.size() == 4);
    REQUIRE(clip->pcm[0] == Approx(0.5f).margin(0.001f));
    REQUIRE(clip->pcm[1] == Approx(-0.5f).margin(0.001f));
    REQUIRE(clip->pcm[2] == Approx(1.0f).margin(0.001f));
}

TEST_CASE("read_wav parses 32-bit float", "[app]")
{
    const std::string wav = make_wav(1, 48000, 32, {0.1f, 0.2f, 0.3f});
    const auto        clip = read_wav(wav);
    REQUIRE(clip.has_value());
    REQUIRE(clip->channels == 1);
    REQUIRE(clip->sample_rate == 48000);
    REQUIRE(clip->frames == 3);
    REQUIRE(clip->pcm.size() == 3);
    REQUIRE(clip->pcm[0] == 0.1f); // float path is bit-exact
    REQUIRE(clip->pcm[2] == 0.3f);
}

TEST_CASE("read_wav rejects malformed or unsupported input", "[app]")
{
    REQUIRE_FALSE(read_wav("NOTRIFFxxxxx").has_value());          // bad magic
    const std::string good = make_wav(1, 8000, 16, {0.0f, 0.1f, 0.2f});
    REQUIRE_FALSE(read_wav(good.substr(0, good.size() - 3)).has_value()); // truncated data chunk
    std::string fmt_only = make_wav(1, 8000, 16, {0.0f});
    fmt_only = fmt_only.substr(0, 36); // RIFF+WAVE+fmt, no data chunk
    REQUIRE_FALSE(read_wav(fmt_only).has_value());
    std::string wav8 = make_wav(1, 8000, 16, {0.0f});
    wav8[34] = 8; // bitsPerSample -> 8 (unsupported)
    REQUIRE_FALSE(read_wav(wav8).has_value());
}

TEST_CASE("cook_audio / uncook_audio round-trip", "[app]")
{
    AudioClip clip;
    clip.channels    = 2;
    clip.sample_rate = 32000;
    clip.frames      = 3;
    clip.pcm         = {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f};

    const std::string blob = cook_audio(clip);
    const auto        back = uncook_audio(blob);
    REQUIRE(back.has_value());
    REQUIRE(back->channels == 2);
    REQUIRE(back->sample_rate == 32000);
    REQUIRE(back->frames == 3);
    REQUIRE(back->pcm == clip.pcm);

    REQUIRE_FALSE(uncook_audio("BADMAGIC............").has_value());
    REQUIRE_FALSE(uncook_audio(blob.substr(0, blob.size() - 4)).has_value()); // size mismatch
}
```

- [ ] **Step 4: Add the test file to `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, after the line `  unit/audio/miniaudio_backend_test.cpp`, add:

```cmake
  unit/app/audio_cook_test.cpp
```

- [ ] **Step 5: Build and run the cooker tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "read_wav|cook_audio" --output-on-failure`
Expected: PASS — the four cooker cases.

- [ ] **Step 6: Commit**

```bash
git add engine/audio/audio_clip.hpp app/asset/audio_cook.hpp tests/unit/app/audio_cook_test.cpp tests/CMakeLists.txt
git commit -m "feat(audio): AudioClip resource + WAV cooker (16-bit PCM / float32)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Import + load dispatch, end-to-end test

**Files:**
- Modify: `app/asset/asset_io.hpp` (`.wav` import branch + `"audio"` load branch)
- Create: `tests/unit/app/audio_asset_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `read_wav`/`cook_audio`/`uncook_audio`, `AudioClip` (Task 1); `create_miniaudio_backend` (A2); `create_project`, `import_asset`, `load_project_assets`, `AssetRegistry`.

- [ ] **Step 1: Add the audio include to `asset_io.hpp`**

In `app/asset/asset_io.hpp`, after `#include "app/asset/image_cook.hpp"`, add:

```cpp
#include "app/asset/audio_cook.hpp"
```

- [ ] **Step 2: Add the `.wav` import branch**

In `import_asset`, change the `if (ext == ".py") { ... } else { PPM }` chain to insert a `.wav` branch. Replace:

```cpp
    if (ext == ".py")
    {
        blob_bytes = *src;       // script blob is the source text verbatim
        blob_ext   = ".script";
        asset_type = "script";
    }
    else
    {
```

with:

```cpp
    if (ext == ".py")
    {
        blob_bytes = *src;       // script blob is the source text verbatim
        blob_ext   = ".script";
        asset_type = "script";
    }
    else if (ext == ".wav")
    {
        const std::optional<mosaic::engine::audio::AudioClip> clip = read_wav(*src);
        if (!clip)
        {
            mosaic::foundation::log::error("import", "not a supported WAV (16-bit PCM or float32): {}", source_file);
            return std::nullopt;
        }
        blob_bytes = cook_audio(*clip);
        blob_ext   = ".audioclip";
        asset_type = "audio";
    }
    else
    {
```

- [ ] **Step 3: Add the `"audio"` load branch**

In `load_project_assets`, immediately after the `if (meta.type == "script") { ... continue; }` block (before the image blob read), add:

```cpp
        if (meta.type == "audio")
        {
            const fs::path clip_path =
                entry.path().parent_path() / (entry.path().stem().string() + ".audioclip");
            const std::optional<std::string> blob = load_scene_file(clip_path.string());
            if (!blob) { mosaic::foundation::log::warn("import", "missing cooked audio {}", clip_path.string()); continue; }
            const std::optional<mosaic::engine::audio::AudioClip> clip = uncook_audio(*blob);
            if (!clip) { mosaic::foundation::log::warn("import", "corrupt cooked audio {}", clip_path.string()); continue; }
            registry.register_resource<mosaic::engine::audio::AudioClip>(
                meta, std::make_shared<mosaic::engine::audio::AudioClip>(*clip));
            ++count;
            continue;
        }
```

- [ ] **Step 4: Create `tests/unit/app/audio_asset_test.cpp`**

```cpp
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
```

- [ ] **Step 5: Add the test file to `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, after the line `  unit/app/audio_cook_test.cpp`, add:

```cmake
  unit/app/audio_asset_test.cpp
```

- [ ] **Step 6: Build and run the audio asset tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "audio asset|AudioClip plays|import_asset cooks a .wav|rejects a non-WAV" --output-on-failure`
Expected: PASS — import→load round-trip, null-backend playback bridge, and the non-WAV rejection.

- [ ] **Step 7: Run the full suite**

Run: `ctest --test-dir build/debug`
Expected: PASS. (If the threaded `ScriptWorker` case flakes, re-run: `ctest --test-dir build/debug --rerun-failed --output-on-failure`.)

- [ ] **Step 8: Commit**

```bash
git add app/asset/asset_io.hpp tests/unit/app/audio_asset_test.cpp tests/CMakeLists.txt
git commit -m "feat(audio): import .wav as an audio asset; load registers an AudioClip

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Handoff doc update

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update the handoff doc**

In `docs/project/next-steps.md`: add an **Audio — A3 done** entry — a neutral `AudioClip` resource + `app/asset/audio_cook.hpp` (`read_wav` for 16-bit PCM / float32, `cook_audio`/`uncook_audio`), and `.wav`/`"audio"` branches in `import_asset`/`load_project_assets`, so `mosaic import <proj> foo.wav` cooks a `.audioclip` asset that loads as an `AudioClip` resource; verified end-to-end incl. playing a loaded clip through the null miniaudio backend. Note the remaining **A4**: at startup feed registered `AudioClip`s to `backend->register_clip`, inject the real device backend, wire `AudioSystem` into `setup_runtime`, add a demo `AudioSource`/`AudioListener` + bundled clip. Update the top "Last updated" summary.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(audio): next-steps — audio A3 (clip asset) done

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `AudioClip` neutral resource → Task 1 Step 1. ✓
- `read_wav` (16-bit PCM + float32 + EXTENSIBLE), rejection cases → Task 1 Step 2; tests Step 3. ✓
- `cook_audio`/`uncook_audio` format + inverse → Task 1 Step 2; round-trip test Step 3. ✓
- `.wav` import branch (overwrite-safe/rollback inherited from the shared tail) → Task 2 Step 2. ✓
- `"audio"` load branch registering `AudioClip` → Task 2 Step 3. ✓
- import→load round-trip + null-backend playback bridge + non-WAV rejection → Task 2 Step 4. ✓
- Not wired to backend/`setup_runtime` (A4) → no `setup_runtime`/`demo_scene` edits. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `read_wav/uncook_audio -> std::optional<AudioClip>`, `cook_audio -> std::string`; `AudioClip{pcm(vector<float>), channels(u32), sample_rate(u32), frames(u64)}` identical across cooker, tests, and the loader; `register_resource<AudioClip>` / `load<AudioClip>` match; `backend->register_clip(AssetID, const float*, uint64, uint32, uint32)` and `play(..., bool spatial)` match the A2 interface; `create_project`/`import_asset`/`load_project_assets`/`save_scene_file` signatures match `asset_import_test.cpp`. ✓
