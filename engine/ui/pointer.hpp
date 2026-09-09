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
};
} // namespace mosaic::engine::ui
