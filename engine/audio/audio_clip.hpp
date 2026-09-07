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
