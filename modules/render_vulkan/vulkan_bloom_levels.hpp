#pragma once

#include <cstdint>

namespace mosaic::render::vk
{

// Six levels, and the number is a budget rather than a limit of the math. At 1920x1080
// level 6 is already 30x16; each further step costs a render pass and contributes almost
// nothing to a result that is then multiplied by an intensity below 1.
constexpr std::uint32_t kBloomMaxLevels = 6;

// Stop before a mip's smaller dimension falls under this. A 4x4 mip blurred by a 4-tap
// kernel is a single colour -- a pass spent computing the average of the image.
constexpr std::uint32_t kBloomMinExtent = 8;

// How many mips a bloom chain gets for a swapchain of (width, height). Mip 0 is HALF of
// them, so the arguments are the swapchain's size and not the chain's.
//
// Returns 0 only for a zero extent (a minimised window), where the backend skips the
// chain entirely -- the same thing create_hdr_target does. Every drawable size returns at
// least 1, so the caller's N-1 loop counters are never asked to go negative while
// unsigned.
//
// Lives here rather than inside vulkan_render_device.cpp for one reason: nothing in the
// test suite creates a VkDevice, so sizing logic buried in that file is unreachable from
// any test. vulkan_swapchain_support.hpp is the precedent.
std::uint32_t bloom_level_count(std::uint32_t width, std::uint32_t height);

} // namespace mosaic::render::vk
