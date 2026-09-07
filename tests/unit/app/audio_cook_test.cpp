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
