#pragma once

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "studio/selection.hpp"
#include "studio/studio_layout.hpp"

namespace mosaic::studio
{

// Draws the editor shell each frame: a full-window dockspace, a menu bar (File > Exit),
// the scene Hierarchy (entities from the World, click to select), the Inspector (shows
// the current selection), and empty placeholder panels. ImGui glue only; the caller
// wraps draw_ui() between the device's begin_ui_frame()/end_ui_frame().
class StudioApp
{
public:
    void draw_ui(mosaic::engine::World& world,
                 const mosaic::engine::reflection::ComponentReflection& reflection);

    StudioLayout&   layout() { return layout_; }
    SelectionModel& selection() { return selection_; }
    bool            exit_requested() const { return layout_.exit_requested(); }

private:
    void draw_hierarchy(const mosaic::engine::World& world);
    void draw_inspector(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection);

    StudioLayout   layout_;
    SelectionModel selection_;
};

} // namespace mosaic::studio
