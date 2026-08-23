#pragma once

#include "studio/studio_layout.hpp"

namespace mosaic::studio
{

// Draws the editor shell each frame from a StudioLayout: a full-window dockspace, a
// menu bar (File > Exit), and an empty window per open panel. ImGui glue only; the
// caller wraps draw_ui() between the device's begin_ui_frame()/end_ui_frame().
class StudioApp
{
public:
    void draw_ui();

    StudioLayout&       layout() { return layout_; }
    bool                exit_requested() const { return layout_.exit_requested(); }

private:
    StudioLayout layout_;
};

} // namespace mosaic::studio
