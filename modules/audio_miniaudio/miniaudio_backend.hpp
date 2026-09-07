#pragma once

#include <memory>

#include "engine/audio/audio_backend.hpp"

namespace mosaic::audio::miniaudio
{
// Constructs a miniaudio-backed AudioBackend, or nullptr if the audio engine could
// not initialize. ma_* stays inside the .cpp. null_device = true forces miniaudio's
// null backend (no hardware) for headless tests; false auto-selects the OS device.
std::unique_ptr<mosaic::engine::audio::AudioBackend> create_miniaudio_backend(
    bool null_device = false);
} // namespace mosaic::audio::miniaudio
