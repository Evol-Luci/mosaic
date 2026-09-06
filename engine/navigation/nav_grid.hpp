#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace mosaic::engine::navigation
{
struct GridCell
{
    int x = 0;
    int y = 0;
};

inline bool operator==(GridCell a, GridCell b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(GridCell a, GridCell b) { return !(a == b); }

// A uniform 2D grid of walkable / blocked cells for pathfinding.
class NavGrid
{
public:
    NavGrid(int width, int height)
        : width_(width), height_(height),
          blocked_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0)
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    bool walkable(int x, int y) const
    {
        return in_bounds(x, y) && blocked_[index(x, y)] == 0;
    }
    void block(int x, int y)
    {
        if (in_bounds(x, y)) { blocked_[index(x, y)] = 1; }
    }
    void clear(int x, int y)
    {
        if (in_bounds(x, y)) { blocked_[index(x, y)] = 0; }
    }

private:
    std::size_t index(int x, int y) const
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(x);
    }

    int                       width_;
    int                       height_;
    std::vector<std::uint8_t> blocked_;
};

// A* on an 8-connected grid (octile heuristic, diagonal cost sqrt(2), no corner-cutting).
// Returns the path from `start` to `goal` inclusive, or empty if either end is unwalkable or no
// path exists. A start == goal returns {start}.
inline std::vector<GridCell> find_path(const NavGrid& grid, GridCell start, GridCell goal)
{
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) { return {}; }
    if (start == goal) { return {start}; }

    const int         w = grid.width();
    const int         h = grid.height();
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const auto        idx = [w](int x, int y) { return static_cast<std::size_t>(y) * w + x; };

    std::vector<float>        g(n, std::numeric_limits<float>::max());
    std::vector<int>          came(n, -1);
    std::vector<std::uint8_t> closed(n, 0);

    const auto heur = [&](int x, int y)
    {
        const int dx = std::abs(x - goal.x);
        const int dy = std::abs(y - goal.y);
        const int mn = std::min(dx, dy);
        const int mx = std::max(dx, dy);
        return static_cast<float>(mx - mn) + 1.41421356f * static_cast<float>(mn);
    };

    using Item = std::pair<float, std::size_t>; // (f-score, cell index)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[idx(start.x, start.y)] = 0.0f;
    open.push({heur(start.x, start.y), idx(start.x, start.y)});

    static const int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!open.empty())
    {
        const std::size_t ci = open.top().second;
        open.pop();
        if (closed[ci] != 0) { continue; }
        closed[ci] = 1;
        const int cx = static_cast<int>(ci % w);
        const int cy = static_cast<int>(ci / w);
        if (cx == goal.x && cy == goal.y) { break; }

        for (int k = 0; k < 8; ++k)
        {
            const int nx = cx + dx8[k];
            const int ny = cy + dy8[k];
            if (!grid.walkable(nx, ny)) { continue; }
            const bool diag = dx8[k] != 0 && dy8[k] != 0;
            if (diag && (!grid.walkable(cx + dx8[k], cy) || !grid.walkable(cx, cy + dy8[k])))
            {
                continue; // no corner-cutting
            }
            const float      cost = diag ? 1.41421356f : 1.0f;
            const std::size_t ni  = idx(nx, ny);
            const float      ng   = g[ci] + cost;
            if (ng < g[ni])
            {
                g[ni]    = ng;
                came[ni] = static_cast<int>(ci);
                open.push({ng + heur(nx, ny), ni});
            }
        }
    }

    const std::size_t gi = idx(goal.x, goal.y);
    if (came[gi] == -1) { return {}; } // goal never reached

    std::vector<GridCell> path;
    for (int cur = static_cast<int>(gi); cur != -1; cur = came[static_cast<std::size_t>(cur)])
    {
        path.push_back({static_cast<int>(cur % w), static_cast<int>(cur / w)});
    }
    std::reverse(path.begin(), path.end());
    return path;
}
} // namespace mosaic::engine::navigation
