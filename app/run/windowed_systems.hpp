#pragma once

#include <functional>
#include <utility>

#include "app/run/click_nudge.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"

namespace mosaic::app
{

// What the windowed-only systems need from the host. Supplied as providers rather
// than as a window handle, so this header stays free of GLFW and the whole seam is
// testable with stubs.
struct WindowedSystemDeps
{
    std::function<mosaic::engine::ui::PointerState()> pointer;
    std::function<float()>                            aspect;
    std::function<bool()>                             ui_claimed;
    float nudge_strength = kDefaultNudgeStrength;
};

// Registers the systems that exist only when the run has a window. A no-op when
// `windowed` is false.
//
// This function exists to make a registration DECISION testable. Click-to-nudge once
// shipped dead: its registration sat behind a guard reading a variable that was still
// null at that point in main(), so the guard never fired, and the entire test suite
// stayed green because every test built the system directly and nothing exercised the
// wiring. Conditional registration is the risky class -- an unconditional one cannot
// silently vanish -- so conditional wiring belongs here, where a test can assert what
// was registered, rather than inline in main().
//
// Note the limit: this makes "what is registered under which condition" testable. It
// cannot check that the CALLER passes the right condition. That is why main() passes
// the parsed CLI flag straight through with no intermediate variable to go stale --
// a stale intermediate is exactly what caused the original bug.
//
// Ordering: call this AFTER setup_runtime. ClickNudgeSystem must tick after UiSystem
// so it sees the UI's verdict on the same press, and World ticks systems in
// registration order.
inline void register_windowed_systems(mosaic::engine::World& world, bool windowed,
                                      WindowedSystemDeps deps)
{
    if (!windowed)
    {
        return;
    }
    world.add_system<ClickNudgeSystem>(std::move(deps.pointer), std::move(deps.aspect),
                                       deps.nudge_strength, std::move(deps.ui_claimed));
}

} // namespace mosaic::app
