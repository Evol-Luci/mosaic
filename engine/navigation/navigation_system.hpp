#pragma once

#include <cmath>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/navigation/nav_agent.hpp"
#include "engine/navigation/nav_grid.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::navigation
{
// Moves NavAgents toward their goal cell along an A* path on the injected grid. Maps the Transform's
// (x, z) to a grid cell (1 cell == 1 world unit, origin at 0).
//
// The agent caches a per-tick STEP target (the next cell on its path) and only recomputes A* once it
// is snapped to a cell center. This is important: recomputing from lround(position) mid-move can
// round onto a blocked cell (between two walkable cells) and wrongly report "no path". Recomputing
// only at cell centers keeps the start cell walkable. (Path caching beyond one step is a follow-up.)
class NavigationSystem : public ecs::System
{
public:
    explicit NavigationSystem(const NavGrid& grid) : grid_(grid) {}

    void update(World& world, float dt) override
    {
        std::unordered_set<std::uint64_t> seen;
        world.for_each<NavAgent>(
            [&](mosaic::foundation::EntityID e, NavAgent& agent)
            {
                seen.insert(e.value());
                if (!agent.active) { return; }
                auto* tf = world.get_component<mosaic::engine::transform::Transform>(e);
                if (tf == nullptr) { return; }

                const std::uint64_t id = e.value();
                const GridCell      goal{agent.goal_x, agent.goal_y};

                // Ensure a step target. We only reach this recompute when the agent is at a cell
                // center (no cached target), so lround() lands on a walkable cell.
                auto it = targets_.find(id);
                if (it == targets_.end())
                {
                    const GridCell current{static_cast<int>(std::lround(tf->position.x)),
                                           static_cast<int>(std::lround(tf->position.z))};
                    if (current == goal) { return; } // arrived
                    const std::vector<GridCell> path = find_path(grid_, current, goal);
                    if (path.size() < 2) { return; } // no route
                    it = targets_.emplace(id, path[1]).first;
                }

                const GridCell target = it->second;
                const float    tx   = static_cast<float>(target.x);
                const float    tz   = static_cast<float>(target.y);
                const float    ddx  = tx - tf->position.x;
                const float    ddz  = tz - tf->position.z;
                const float    dist = std::sqrt(ddx * ddx + ddz * ddz);
                const float    step = agent.speed * dt;
                if (dist <= step || dist == 0.0f)
                {
                    tf->position.x = tx; // snap to the cell center; recompute next tick
                    tf->position.z = tz;
                    targets_.erase(id);
                }
                else
                {
                    tf->position.x += ddx / dist * step;
                    tf->position.z += ddz / dist * step;
                }
            });

        // Drop cached targets for agents that no longer exist.
        for (auto it = targets_.begin(); it != targets_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : targets_.erase(it);
        }
    }

private:
    const NavGrid&                                  grid_;
    std::unordered_map<std::uint64_t, GridCell>     targets_; // current step cell per agent
};
} // namespace mosaic::engine::navigation
