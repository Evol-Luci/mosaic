#pragma once

#include <cstdint>
#include <functional>

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::script
{
// Neutral closest-hit ray result handed to scripts (no physics type crosses the
// script boundary — the runtime maps PhysicsSystem::RaycastHit onto this).
struct ScriptRayHit
{
    bool                           hit = false;
    std::uint64_t                  entity = 0; // valid only when hit
    mosaic::foundation::math::Vec3 point{};
    mosaic::foundation::math::Vec3 normal{};
    float                          distance = 0.0f;
};

// Host hooks a script may call to drive the run-state / query the world. Callbacks
// are optional: an unset (empty) function is a silent no-op (or an always-miss for
// raycast). Supplied by the host (runtime/editor); ScriptSystem binds them into
// every engine.
struct ScriptHost
{
    std::function<void(bool)> set_playing;  // play(true) / pause(false)
    std::function<void()>     request_stop; // end the run

    // Closest-hit ray query; unset => always-miss. Kept neutral (Vec3 in / ScriptRayHit
    // out) so the script backend never depends on the physics system.
    std::function<ScriptRayHit(mosaic::foundation::math::Vec3 origin,
                               mosaic::foundation::math::Vec3 direction,
                               float max_distance)>
        raycast;
};
} // namespace mosaic::engine::script
