#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "engine/animation/animation_player.hpp"
#include "engine/ecs/system.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::animation
{
// Split `s` on `delim` into non-empty-preserving segments (e.g. "a.b.c" -> {a,b,c}).
inline std::vector<std::string> split_path(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

// Set the scalar leaf at a dotted `path` within a component Value (a Table). Returns false if any
// intermediate segment is missing or not a table, or the leaf key doesn't already exist.
inline bool set_scalar_at(mosaic::foundation::serialization::Value& root,
                          const std::vector<std::string>& path, double v)
{
    using mosaic::foundation::serialization::Table;
    using mosaic::foundation::serialization::Value;
    if (path.empty()) { return false; }

    Value* cur = &root;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (!cur->is<Table>()) { return false; }
        Table& table = cur->as<Table>();
        if (table.find(path[i]) == nullptr) { return false; }
        cur = &table[path[i]];
    }
    if (!cur->is<Table>()) { return false; }
    Table& leaf = cur->as<Table>();
    if (leaf.find(path.back()) == nullptr) { return false; }
    leaf[path.back()] = Value(v);
    return true;
}

// Sample every track of `clip` at `time` and write the result into entity `e`'s components via
// reflection (any scalar reachable by a property path). Shared by AnimationSystem and the sequencer.
inline void apply_clip_at(const reflection::ComponentReflection& types, World& world,
                          mosaic::foundation::EntityID e, const AnimationClip& clip, float time)
{
    for (const Track& track : clip.tracks)
    {
        const std::string::size_type dot = track.target.find('.');
        if (dot == std::string::npos) { continue; } // needs Component.<path>
        const std::string component = track.target.substr(0, dot);
        const std::vector<std::string> path = split_path(track.target.substr(dot + 1), '.');

        auto value = types.component_value(world, e, component);
        if (!value) { continue; }
        if (set_scalar_at(*value, path, sample(track, time)))
        {
            types.set_value(world, e, component, *value);
        }
    }
}

// Advances AnimationPlayers and writes sampled track values into their target components via
// reflection. Generic: any reflected scalar reachable by a property path can be animated.
class AnimationSystem : public ecs::System
{
public:
    explicit AnimationSystem(const reflection::ComponentReflection& types) : types_(types) {}

    void update(World& world, float dt) override
    {
        world.for_each<AnimationPlayer>(
            [&](mosaic::foundation::EntityID e, AnimationPlayer& player)
            {
                if (!player.playing) { return; }
                player.time += dt * player.speed;
                if (player.loop && player.clip.duration > 0.0f)
                {
                    player.time = std::fmod(player.time, player.clip.duration);
                    if (player.time < 0.0f) { player.time += player.clip.duration; }
                }
                apply_clip_at(types_, world, e, player.clip, player.time);
            });
    }

private:
    const reflection::ComponentReflection& types_;
};
} // namespace mosaic::engine::animation
