# Navigation (grid A* + NavAgent) — Design

## Goal (Phase 4, subsystem 3)

Grid pathfinding so entities can route to goals around obstacles.
Maintainer-approved: a **NavAgent + NavigationSystem** ("engine subsystem" like
physics), grid **injected** into the system; the A* grid core is neutral and
exhaustively headless-tested. Script `find_path()` is a follow-up.

## Design

### `engine/navigation/nav_grid.hpp`

- `GridCell { int x, y; }` (+ `==`/`!=`).
- `NavGrid(width, height)` — a 2D grid; `walkable(x,y)` = in-bounds && not blocked;
  `block(x,y)` / `clear(x,y)`.
- `find_path(grid, start, goal) -> vector<GridCell>` — **A\***, 8-connected,
  octile heuristic, diagonal cost √2, **no corner-cutting** (a diagonal step needs
  both adjacent orthogonal cells walkable). Empty if either end is unwalkable or
  no path; `{start}` when start == goal.

### `engine/navigation/nav_agent.hpp` — component

`NavAgent { int goal_x, goal_y; float speed; bool active; }` (reflected;
registered as `"NavAgent"`).

### `engine/navigation/navigation_system.hpp`

`NavigationSystem(const NavGrid& grid)`. Maps a Transform's `(x, z)` to a cell
(1 cell = 1 unit, origin 0). Each tick, per active agent: steer the Transform
toward the next path cell by `speed * dt`, snapping on arrival.

**Key correctness point:** recomputing A* from `lround(position)` *every* tick can
round onto a **blocked** cell while the agent is mid-move between two walkable
cells → A* falsely reports "no path" and the agent freezes. Fix: cache a per-agent
**step target** (`unordered_map<entity, GridCell>`); recompute A* **only when the
agent is snapped to a cell center** (no cached target), where `lround` is exact and
walkable. Prune caches for departed agents.

Not wired into `setup_runtime` (navigation needs a grid — a per-game concern);
provided + unit-tested standalone.

## Testing (headless)

1. **`find_path`** — straight line on open ground; routes around a wall (never
   steps on a blocked cell); empty when the goal is boxed in; degenerate/OOB
   (start==goal → size 1; OOB start/goal → empty; blocked goal → empty).
2. **`NavigationSystem`** — an agent reaches its goal on open ground; **routes
   around an obstacle** to the goal (exercises the cached-target fix); an inactive
   agent doesn't move.

## Out of scope / follow-ups

- Script `find_path()` seam (mirror raycast); grid baked from scene colliders;
  navmesh / 3D; dynamic obstacles; path smoothing; multi-step path caching;
  `setup_runtime` wiring.
