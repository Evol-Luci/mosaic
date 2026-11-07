// The guard on `validation_messages_are_observed()`'s PUBLISHED VALUE.
//
// Ten GPU tests reach `tests/gpu/validation_capture.hpp`, which asserts this flag is
// true before asserting the validation message list is empty. That ordering is what
// stops "no validation messages" from being a claim that is true for free. But the flag
// itself was published by one line in vulkan_render_device.cpp and checked by nothing:
//
//   g_validation_observed.store(validation_layer_enabled_, std::memory_order_relaxed);
//
// Measured 2026-10-30 on 401d027: replacing that with `store(true, ...)` -- claiming
// observation on messenger creation alone, which happens either way -- left 12/12 GPU
// tests PASSING with the layer present. Add the removal of the accidental "layer not
// available" WARN crutch and layer-off left ten of them green having observed nothing.
// Every test that depended on the flag was satisfied by a flag that had stopped meaning
// anything, because they all asked it the same question in the same environment.
//
// WHY THIS IS NOT THE PER-DEVICE ACCESSOR THE HEADER PREDICTED
// -------------------------------------------------------------
// vulkan_render_device.hpp said the fix was "the per-device accessor shape that
// VulkanHeadlessDevice::validation_enabled() already has -- the only check that caught
// either fault". That is the right observation with the wrong cause attached, and acting
// on it would have produced a test that still cannot fail.
//
// What saved the two VulkanHeadlessDevice cases was not per-device-ness. It was that
// they read a DIFFERENT VARIABLE, set by a SECOND copy of the layer-enablement logic
// that a single-line edit in the other file does not touch. That is redundancy, and
// redundancy catches a fault only for as long as nobody edits both copies. Moving this
// flag onto the device would hand it the same shape and the same non-guarantee: a
// per-device accessor publishing a wrong value is exactly as unguarded as a global one.
//
// A value is guarded when a test compares it against a source that does not come from
// the code under test. There are two such sources here, and this file uses both.
//
// THE TWO CELLS
// -------------
//   1. `vkEnumerateInstanceLayerProperties` -- the loader's own answer to "is
//      VK_LAYER_KHRONOS_validation installed", asked without going through any Mosaic
//      code at all. With the layer installed the flag MUST be true, which is what makes
//      `store(false, ...)` or a dropped store detectable.
//
//   2. `VK_LOADER_LAYERS_DISABLE` -- the loader reads this at vkCreateInstance time, from
//      the process environment, so setting it between two device creations changes the
//      answer to "was the layer enabled" WITHOUT changing a line of Mosaic code and
//      without uninstalling anything. With layers disabled the flag MUST be false, which
//      is what makes `store(true, ...)` detectable WITH THE LAYER STILL PRESENT. That was
//      the requirement the backlog set for this item, and cell 1 alone cannot meet it:
//      when the layer is enabled `validation_layer_enabled_` is already true, so
//      `store(true, ...)` publishes the correct value by accident and no assertion that
//      only ever observes the enabled case can tell the two lines apart.
//
// Both cells live in ONE case on purpose. Cell 1 is the control for cell 2: it proves the
// device reports true in this environment moments earlier, so a false in cell 2 is
// attributable to the environment variable and not to a machine that never had the layer.
// Split across two cases, each half would pass in an environment where the flag was
// stuck at the value that half expects.
//
// This is also the first test in the suite to assert the LAYER-OFF path rather than
// infer it. Under a run that is already `VK_LOADER_LAYERS_DISABLE='*'`, cell 1's
// precondition fails and this case goes red with the rest -- asserted, not skipped, to
// match validation_capture.hpp. See docs/project/landmines.md for the expected split.

#include "modules/render_vulkan/vulkan_render_device.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>

namespace
{

constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 64;

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr const char* kDisableVar = "VK_LOADER_LAYERS_DISABLE";

// Does the loader itself list the validation layer? Asked through the Vulkan API rather
// than through anything in modules/render_vulkan/, so the answer is independent of the
// value under test. Note the order this is called in matters: the loader filters its own
// enumeration by VK_LOADER_LAYERS_DISABLE, so this must run BEFORE the variable is set
// or it would agree with the device for the wrong reason.
bool validation_layer_is_installed()
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) { return false; }
    std::vector<VkLayerProperties> layers(count);
    if (count != 0 && vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
    {
        return false;
    }
    for (const VkLayerProperties& layer : layers)
    {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0) { return true; }
    }
    return false;
}

// Sets VK_LOADER_LAYERS_DISABLE for its lifetime and puts the environment back exactly
// as it found it -- including the case where the variable was already set to something
// else, which must be restored rather than unset. RAII because the assertions in between
// can throw: a leaked VK_LOADER_LAYERS_DISABLE would silently disarm every later test in
// this process, and they would report success having validated nothing, which is the
// identical failure this file exists to prevent.
class ScopedLayersDisabled
{
public:
    ScopedLayersDisabled()
    {
        if (const char* previous = std::getenv(kDisableVar))
        {
            had_previous_ = true;
            previous_ = previous;
        }
        setenv(kDisableVar, "*", 1);
    }

    ~ScopedLayersDisabled()
    {
        if (had_previous_) { setenv(kDisableVar, previous_.c_str(), 1); }
        else { unsetenv(kDisableVar); }
    }

    ScopedLayersDisabled(const ScopedLayersDisabled&) = delete;
    ScopedLayersDisabled& operator=(const ScopedLayersDisabled&) = delete;

private:
    bool had_previous_ = false;
    std::string previous_;
};

} // namespace

TEST_CASE("gpu the validation-observed flag follows the LAYER and not the messenger",
          "[gpu][render][validation]")
{
    // Cell 1's precondition, and the reason this case can fail at all. Asserted rather
    // than skipped: a skip here would be indistinguishable in CI from a pass, and the
    // whole point of this file is that a test which cannot fail is worse than no test.
    {
        INFO("The loader does not list " << kValidationLayer << ". Every assertion below "
             "would then hold for free, so this case reports the missing layer instead of "
             "passing vacuously. Causes in order of likelihood: this run is already under "
             "VK_LOADER_LAYERS_DISABLE or VK_INSTANCE_LAYERS; the vulkan-validationlayers "
             "package is absent (CI installs it for exactly this reason); or the loader "
             "cannot read its manifest directory.");
        REQUIRE(validation_layer_is_installed());
    }

    // --- Cell 1: layer installed and enabled, so the flag must be TRUE ----------------
    // Catches a dropped store and `store(false, ...)`, and establishes that this process
    // and this device DO observe validation -- without which cell 2 proves nothing.
    {
        const mosaic::render::vk::OffscreenDevice device =
            mosaic::render::vk::create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(device.device != nullptr);

        INFO("The loader lists " << kValidationLayer << " but the device published "
             "validation_messages_are_observed() == false. The ten tests that reach "
             "validation_capture.hpp assert the opposite, so this is either a dropped "
             "store in setup_debug_messenger or a build in which the #ifndef NDEBUG layer "
             "block was elided while the loader still offers the layer.");
        REQUIRE(mosaic::render::vk::validation_messages_are_observed());
    }

    // --- Cell 2: layer still installed, loader told to disable it, flag must be FALSE --
    // The decisive cell, and the only one that can fail on `store(true, ...)` while the
    // layer is present. Nothing about the code changes between the two cells; the single
    // difference is one environment variable the Vulkan loader consults when the instance
    // below is created.
    {
        const ScopedLayersDisabled layers_off;

        const mosaic::render::vk::OffscreenDevice device =
            mosaic::render::vk::create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(device.device != nullptr);

        INFO("With " << kDisableVar << "='*' the loader enabled no layers for this "
             "instance, so a validation message could not reach the engine log and "
             "validation_messages_are_observed() must be false. It read true. Either the "
             "published value is not derived from validation_layer_enabled_ -- which is "
             "the fault this case exists for, and `store(true, ...)` is how it looks -- or "
             "this loader ignores " << kDisableVar << ", in which case the mechanism of "
             "this test is broken and cell 1 above would also have been meaningless.");
        REQUIRE_FALSE(mosaic::render::vk::validation_messages_are_observed());
    }
}
