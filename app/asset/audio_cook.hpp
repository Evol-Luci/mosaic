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
