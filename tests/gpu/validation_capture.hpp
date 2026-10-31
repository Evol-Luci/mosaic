#pragma once

// One capture sink and one assertion for every GPU test that claims "no validation
// messages".
//
// WHY THIS IS NOT JUST DE-DUPLICATION. Seven of these tests used to end in a bare
// `REQUIRE(validation_messages.empty())`, and that assertion is only as strong as the
// thing that produces the messages. The layer is enabled in
// `vulkan_render_device.cpp` under `#ifndef NDEBUG` *and* only when the loader
// enumerates it, so in a release build -- or on a machine without the validation
// layer package, or under `VK_LOADER_LAYERS_DISABLE` -- the count is zero because
// nothing ever looked, not because the frame was legal.
//
// Measured on 2026-10-29 against unmodified main: with `VK_LOADER_LAYERS_DISABLE='*'`
// those seven DID fail, but only by accident -- the backend's own "validation layer
// not available; continuing without it" WARN is itself a `vulkan`-category warning,
// so the sink captured it and `empty()` was false. The diagnosis a reader got was
// `REQUIRE( validation_messages.empty() )` with expansion `false`, which reads as a
// Vulkan validation error in the renderer. Nothing pinned that crutch: re-word that
// log line, change its category or level, or install the sink after device creation,
// and all seven go quietly vacuous with no test edit. A release build was vacuous
// already.
//
// So the order below matters and is the point: assert that validation was OBSERVED,
// then assert it was SILENT.

#include "foundation/logging/log.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace mosaic::gpu_test
{

// Collects every `vulkan`-category Warn/Error record while still echoing to the
// console, so a failing run shows the messages as well as counting them.
class ValidationCaptureSink final : public mosaic::foundation::log::Sink
{
public:
    void write(const mosaic::foundation::log::Record& record) noexcept override
    {
        console_.write(record);
        if (record.category == "vulkan" &&
            (record.level == mosaic::foundation::log::Level::Warn ||
             record.level == mosaic::foundation::log::Level::Error))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            messages_.emplace_back(record.message);
        }
    }

    std::vector<std::string> messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

private:
    mosaic::foundation::log::ConsoleSink console_;
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
};

// Install BEFORE creating the device: the backend logs about validation during
// instance creation, and a sink added afterwards misses it.
inline std::shared_ptr<ValidationCaptureSink> install_validation_capture()
{
    auto sink = std::make_shared<ValidationCaptureSink>();
    mosaic::foundation::log::clear_sinks();
    mosaic::foundation::log::add_sink(sink);
    return sink;
}

// Call after the device has been destroyed, with the messages taken from the sink.
// Asserts, IN THIS ORDER, that validation actually ran and that it said nothing.
inline void require_validation_observed_and_silent(
    const std::vector<std::string>& validation_messages)
{
    INFO("validation messages: " << validation_messages.size());
    for (const std::string& message : validation_messages)
    {
        INFO("  " << message);
    }

    {
        INFO("VK_LAYER_KHRONOS_validation was NOT enabled for this device, so the "
             "message count below is zero whatever the renderer did. Causes, in order "
             "of likelihood: the run is under VK_LOADER_LAYERS_DISABLE or "
             "VK_INSTANCE_LAYERS; the validation layer package is not installed (CI "
             "installs vulkan-validationlayers for exactly this reason); or this is an "
             "NDEBUG build, where vulkan_render_device.cpp elides the layer entirely. "
             "This is asserted rather than skipped to match "
             "tests/gpu/headless_device_test.cpp, which has required "
             "VulkanHeadlessDevice::validation_enabled() since 2026-10-24.");
        REQUIRE(mosaic::render::vk::validation_messages_are_observed());
    }

    REQUIRE(validation_messages.empty());
}

} // namespace mosaic::gpu_test
