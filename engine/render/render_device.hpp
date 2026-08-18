#pragma once

namespace mosaic::engine::render
{

// A color in linear-ish 0..1 components; the backend maps it to the swapchain.
struct Color
{
    float r, g, b, a;
};

// Backend-neutral rendering device. A backend (Vulkan today) implements it;
// engine and app code never see the backend's types. 36a clears the screen;
// 36b will add draw() between clear() and end_frame().
class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    virtual void begin_frame() = 0;        // acquire the next swapchain image
    virtual void clear(const Color&) = 0;  // clear the acquired image to a color
    virtual void end_frame() = 0;          // submit + present
};

} // namespace mosaic::engine::render
