#pragma once

namespace mosaic::engine::ui
{
// Per-tick pointer state in normalized screen space (top-left origin, matching UI
// rects). down = primary button pressed. Supplied by an injected provider; the engine
// never reads a window/mouse directly.
struct PointerState
{
    float x = 0.0f, y = 0.0f;
    bool  down = false;
    // Whether a pointer is actually PRESENT over the surface this state describes.
    //
    // Without this, "there is no pointer" had to be spelled as a zeroed state -- and
    // {0, 0} is a real position inside any button anchored TopLeft at {0,0}, which is
    // precisely what a default-constructed UiButton is. So every provider's "no pointer"
    // answer hovered a default-positioned button: headless `mosaic run` forever (the GLFW
    // window is never created), and in the editor whenever the cursor left the viewport
    // panel, where the position clamps to the panel edge instead.
    //
    // Defaults to TRUE so that existing `{x, y, down}` aggregate initialisation keeps
    // meaning "a pointer is here", which is what every such call site intends. A provider
    // that means "no pointer" must say so explicitly -- and each one that does is pinned by
    // a test, because a default cannot express both intents safely.
    bool  inside = true;
};
} // namespace mosaic::engine::ui
