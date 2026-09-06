#pragma once

namespace mosaic::engine::navigation
{
// Steers an entity's Transform toward a goal grid cell along an A* path. NavigationSystem reads the
// entity's Transform (its current cell) and moves it toward the goal. Reflected, so it serializes
// with the scene and shows in the Inspector.
struct NavAgent
{
    int   goal_x = 0;
    int   goal_y = 0;
    float speed  = 1.0f;  // world units (cells) per second
    bool  active = true;
};

template <class V>
void reflect(V& visitor, NavAgent& a)
{
    visitor.member("goal_x", a.goal_x);
    visitor.member("goal_y", a.goal_y);
    visitor.member("speed", a.speed);
    visitor.member("active", a.active);
}
} // namespace mosaic::engine::navigation
