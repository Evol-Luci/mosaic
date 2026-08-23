#pragma once

#include <cstdint>

namespace mosaic::studio
{

struct ViewportSize
{
    std::uint32_t width;
    std::uint32_t height;
};

// Tracks the Viewport panel's measured content size vs. the size last applied to the
// device's offscreen target, so the editor can decide when to resize. One-frame
// latency: the size measured during frame N feeds the offscreen pass of frame N+1.
class ViewportPanel
{
public:
    // Record ImGui's content-region avail for this frame. NaN / < 1 / negative inputs
    // floor to 1 so the offscreen target is never degenerate; fractional sizes truncate.
    void set_measured_size(float avail_w, float avail_h)
    {
        measured_.width  = to_extent(avail_w);
        measured_.height = to_extent(avail_h);
    }

    ViewportSize size() const { return measured_; }

    bool needs_resize() const
    {
        return measured_.width != applied_.width || measured_.height != applied_.height;
    }

    void mark_applied() { applied_ = measured_; }

    float aspect() const
    {
        return static_cast<float>(measured_.width) / static_cast<float>(measured_.height);
    }

private:
    static std::uint32_t to_extent(float v)
    {
        if (!(v >= 1.0f)) // catches NaN, negatives, and < 1
        {
            return 1u;
        }
        return static_cast<std::uint32_t>(v);
    }

    ViewportSize measured_{1, 1};
    ViewportSize applied_{0, 0}; // 0,0 == nothing applied yet -> first frame needs a resize
};

} // namespace mosaic::studio
