#include "app/mcp/mcp_tools.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <unistd.h> // ::getpid (unique scratch dir for plugin_script_write's validation copy)

#include "app/asset/asset_io.hpp"
#include "app/plugin/git.hpp"
#include "app/plugin/package_install.hpp"
#include "app/plugin/package_source.hpp"
#include "app/plugin/package_sources_registry.hpp"
#include "app/project/bundle.hpp"
#include "app/run/scene_io.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/commands/set_component_command.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/name.hpp"
#include "engine/scene/scene.hpp"
#include "engine/script/script_system.hpp"
#include "engine/animation/animation_system.hpp"
#include "foundation/commands/composite_command.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "studio/plugin_host.hpp"
#include "studio/script_editor.hpp"

namespace mosaic::app::mcp
{
namespace
{
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::EntityID;
using mosaic::foundation::commands::Command;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::commands::CompositeCommand;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::SerializationError;
using mosaic::foundation::serialization::Value;

nlohmann::json value_to_json(const Value& v)
{
    return nlohmann::json::parse(mosaic::foundation::serialization::to_json(v));
}

Value json_to_value(const nlohmann::json& j)
{
    return mosaic::foundation::serialization::from_json(j.dump());
}

std::uint64_t require_uint64(const nlohmann::json& args, const char* key)
{
    // nlohmann parses an ordinary non-negative JSON integer (e.g. from real wire input)
    // as number_integer (int64), NOT number_unsigned -- that type is only produced when
    // the C++ side constructs a json from an actual unsigned value, or the parsed value
    // overflows int64. So both number kinds must be accepted; number_integer additionally
    // needs an explicit non-negative check before the narrowing get<uint64_t>() below,
    // which would otherwise silently wrap a negative value into a huge one.
    if (!args.contains(key) || args.at(key).is_number_float() || !args.at(key).is_number())
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a non-negative integer)");
    }
    if (args.at(key).is_number_integer() && args.at(key).get<std::int64_t>() < 0)
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a non-negative integer)");
    }
    return args.at(key).get<std::uint64_t>();
}

std::string require_string(const nlohmann::json& args, const char* key)
{
    if (!args.contains(key) || !args.at(key).is_string())
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a string)");
    }
    return args.at(key).get<std::string>();
}

std::uint64_t optional_uint64(const nlohmann::json& args, const char* key, std::uint64_t default_value)
{
    if (!args.contains(key)) { return default_value; }
    return require_uint64(args, key);
}

double optional_double(const nlohmann::json& args, const char* key, double default_value)
{
    if (!args.contains(key)) { return default_value; }
    if (!args.at(key).is_number())
    {
        throw McpToolError(std::string("invalid '") + key + "' (expected a number)");
    }
    return args.at(key).get<double>();
}

double require_double(const nlohmann::json& args, const char* key)
{
    if (!args.contains(key) || !args.at(key).is_number())
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a number)");
    }
    return args.at(key).get<double>();
}

std::string optional_string(const nlohmann::json& args, const char* key, const std::string& default_value)
{
    if (!args.contains(key)) { return default_value; }
    return require_string(args, key);
}

using ToolHandler = nlohmann::json (*)(World&, const ComponentReflection&, CommandStack&,
                                       const std::string&, const nlohmann::json&);

nlohmann::json tool_create_entity(World& world, const ComponentReflection&, CommandStack&,
                                  const std::string&, const nlohmann::json&)
{
    return {{"id", world.create_entity().value()}};
}

nlohmann::json tool_destroy_entity(World& world, const ComponentReflection&, CommandStack&,
                                   const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    if (!world.is_valid(e)) { return {{"ok", false}}; }
    world.destroy_entity(e);
    return {{"ok", true}};
}

nlohmann::json tool_get_component(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    const auto v = types.component_value(world, e, name);
    return {{"value", v ? value_to_json(*v) : nlohmann::json(nullptr)}};
}

nlohmann::json tool_set_component(World& world, const ComponentReflection& types, CommandStack& stack,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    if (!args.contains("value"))
    {
        throw McpToolError("missing 'value'");
    }
    const Value next = json_to_value(args.at("value"));
    auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(world, types, e, name, next);
    cmd->execute();
    if (!cmd->valid()) { return {{"ok", false}}; }
    stack.push_executed(std::move(cmd));
    return {{"ok", true}};
}

nlohmann::json tool_add_component(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    return {{"ok", types.add_default(world, e, name)}};
}

nlohmann::json tool_remove_component(World& world, const ComponentReflection& types, CommandStack&,
                                     const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    return {{"ok", types.remove_value(world, e, name)}};
}

nlohmann::json tool_has_component(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    for (const std::string& n : types.components_on(world, e))
    {
        if (n == name) { return {{"ok", true}}; }
    }
    return {{"ok", false}};
}

nlohmann::json tool_component_names(World& world, const ComponentReflection& types, CommandStack&,
                                    const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    return {{"names", types.components_on(world, e)}};
}

nlohmann::json tool_entities(World& world, const ComponentReflection&, CommandStack&,
                             const std::string&, const nlohmann::json&)
{
    nlohmann::json ids = nlohmann::json::array();
    world.for_each_entity([&](EntityID e) { ids.push_back(e.value()); });
    return {{"ids", ids}};
}

nlohmann::json tool_entities_with(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const std::string name = require_string(args, "component");
    nlohmann::json ids = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& n : types.components_on(world, e))
            {
                if (n == name) { ids.push_back(e.value()); break; }
            }
        });
    return {{"ids", ids}};
}

std::optional<nlohmann::json> json_at_path(const nlohmann::json& root,
                                           const std::vector<std::string>& path)
{
    const nlohmann::json* cur = &root;
    for (const std::string& seg : path)
    {
        if (!cur->is_object() || !cur->contains(seg)) { return std::nullopt; }
        cur = &cur->at(seg);
    }
    return *cur;
}

bool json_set_at(nlohmann::json& root, const std::vector<std::string>& path, const nlohmann::json& value)
{
    nlohmann::json* cur = &root;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (!cur->is_object() || !cur->contains(path[i])) { return false; }
        cur = &cur->at(path[i]);
    }
    if (path.empty() || !cur->is_object() || !cur->contains(path.back())) { return false; }
    (*cur)[path.back()] = value;
    return true;
}

// Shared by every tool that accepts a where/field-comparison op (entities_where, bulk_set_field).
const std::unordered_set<std::string>& valid_compare_ops()
{
    static const std::unordered_set<std::string> ops = {"eq", "ne", "gt", "gte", "lt", "lte"};
    return ops;
}

bool compare(const nlohmann::json& lhs, const std::string& op, const nlohmann::json& rhs)
{
    if (op == "eq") { return lhs == rhs; }
    if (op == "ne") { return lhs != rhs; }
    if (!lhs.is_number() || !rhs.is_number()) { return false; }
    const double l = lhs.get<double>();
    const double r = rhs.get<double>();
    if (op == "gt") { return l > r; }
    if (op == "gte") { return l >= r; }
    if (op == "lt") { return l < r; }
    return l <= r; // "lte" -- op is already validated by the caller before compare() is called
}

nlohmann::json tool_entities_where(World& world, const ComponentReflection& types, CommandStack&,
                                   const std::string&, const nlohmann::json& args)
{
    const std::string component = require_string(args, "component");
    const std::string field = require_string(args, "field");
    const std::string op = require_string(args, "op");
    if (!args.contains("value"))
    {
        throw McpToolError("missing 'value'");
    }
    if (valid_compare_ops().find(op) == valid_compare_ops().end())
    {
        throw McpToolError("unknown op: " + op + " (expected one of eq, ne, gt, gte, lt, lte)");
    }
    const nlohmann::json target = args.at("value");
    const std::vector<std::string> path = mosaic::engine::animation::split_path(field, '.');

    nlohmann::json matches = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            const auto v = types.component_value(world, e, component);
            if (!v) { return; }
            const nlohmann::json component_json = value_to_json(*v);
            const auto field_value = json_at_path(component_json, path);
            if (!field_value) { return; }
            if (compare(*field_value, op, target))
            {
                matches.push_back({{"entity", e.value()}, {"value", *field_value}});
            }
        });
    return {{"matches", matches}};
}

nlohmann::json tool_entity_count(World& world, const ComponentReflection&, CommandStack&,
                                 const std::string&, const nlohmann::json&)
{
    return {{"count", static_cast<std::uint64_t>(world.entity_count())}};
}

nlohmann::json tool_undo(World&, const ComponentReflection&, CommandStack& stack,
                         const std::string&, const nlohmann::json&)
{
    return {{"ok", stack.undo()}};
}

nlohmann::json tool_redo(World&, const ComponentReflection&, CommandStack& stack,
                         const std::string&, const nlohmann::json&)
{
    return {{"ok", stack.redo()}};
}

nlohmann::json tool_undo_depth(World&, const ComponentReflection&, CommandStack& stack,
                               const std::string&, const nlohmann::json&)
{
    return {{"depth", static_cast<std::uint64_t>(stack.undo_depth())}};
}

nlohmann::json tool_save_scene(World& world, const ComponentReflection& types, CommandStack&,
                               const std::string& scene_path, const nlohmann::json&)
{
    if (scene_path.empty())
    {
        throw McpToolError("no scene path configured for this session");
    }
    const std::string json_text = mosaic::engine::scene::save_to_json(world, types);
    if (!mosaic::app::save_scene_file(scene_path, json_text))
    {
        throw McpToolError("failed to write scene file: " + scene_path);
    }
    return {{"path", scene_path}};
}

nlohmann::json tool_describe_scene(World& world, const ComponentReflection& types, CommandStack&,
                                   const std::string&, const nlohmann::json&)
{
    // component_counts: one pass, tallying components_on() per entity -- avoids
    // O(registered_names x entities) that a per-name re-scan would cost.
    std::unordered_map<std::string, std::uint64_t> counts;
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e)) { ++counts[name]; }
        });
    nlohmann::json component_counts = nlohmann::json::object();
    for (const auto& [name, count] : counts) { component_counts[name] = count; }

    // hierarchy: one pass collecting a parent -> children map and the root set, then a
    // depth-first walk from roots only. A cycle among non-root entities is never
    // reached from any root, so it's simply absent here -- no depth guard needed for
    // this walk (unlike TransformSystem::compute_world's own kMaxDepth, which guards a
    // different code path: walking UP the parent chain to compute a world matrix).
    std::unordered_map<std::uint64_t, std::vector<EntityID>> children_by_parent;
    std::vector<EntityID> roots;
    world.for_each_entity(
        [&](EntityID e)
        {
            const auto* t = world.get_component<mosaic::engine::transform::Transform>(e);
            if (t == nullptr) { return; }
            if (t->parent) { children_by_parent[t->parent.value()].push_back(e); }
            else { roots.push_back(e); }
        });

    std::function<nlohmann::json(EntityID)> build_node = [&](EntityID e) -> nlohmann::json
    {
        nlohmann::json node;
        node["entity"] = e.value();
        const auto* name = world.get_component<mosaic::engine::Name>(e);
        node["name"] = name != nullptr ? nlohmann::json(name->value) : nlohmann::json(nullptr);
        nlohmann::json children = nlohmann::json::array();
        const auto it = children_by_parent.find(e.value());
        if (it != children_by_parent.end())
        {
            for (EntityID child : it->second) { children.push_back(build_node(child)); }
        }
        node["children"] = children;
        return node;
    };

    nlohmann::json hierarchy = nlohmann::json::array();
    for (EntityID root : roots) { hierarchy.push_back(build_node(root)); }

    return {{"entity_count", static_cast<std::uint64_t>(world.entity_count())},
           {"component_counts", component_counts},
           {"hierarchy", hierarchy}};
}

// Shared by entities_near/scene_bounds: every entity with a Transform, positioned by
// its live world matrix -- computed on demand via TransformSystem::compute_world (the
// exact same parent-chain composer the renderer uses), NOT read from a cached
// WorldTransform component. WorldTransform is only ever written by TransformSystem's
// own update(), which is deliberately not part of World's regular system list (see
// app/run/editor.cpp) -- a headless `mosaic mcp` session never calls it again after
// startup, so a cached WorldTransform silently goes stale after any edit. Computing
// fresh here means correct results regardless of hierarchy or recent edits, at the
// cost of recomputing every queried entity's chain on each call -- a non-issue at
// interactive agent-tool-call frequency.
struct PositionedEntity
{
    EntityID entity;
    mosaic::foundation::math::Vec3 position;
};

std::vector<PositionedEntity> positioned_entities(World& world)
{
    std::vector<PositionedEntity> out;
    world.for_each<mosaic::engine::transform::Transform>(
        [&](EntityID e, mosaic::engine::transform::Transform&)
        {
            const auto m = mosaic::engine::transform::TransformSystem::compute_world(world, e, 0);
            out.push_back({e, {m.m[12], m.m[13], m.m[14]}});
        });
    return out;
}

nlohmann::json tool_entities_near(World& world, const ComponentReflection&, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const double x = require_double(args, "x");
    const double y = require_double(args, "y");
    const double z = require_double(args, "z");
    const double radius = require_double(args, "radius");
    if (radius < 0.0)
    {
        throw McpToolError("'radius' must be non-negative");
    }

    struct Match { EntityID entity; mosaic::foundation::math::Vec3 position; double distance; };
    std::vector<Match> matches;
    for (const PositionedEntity& pe : positioned_entities(world))
    {
        const double dx = pe.position.x - x;
        const double dy = pe.position.y - y;
        const double dz = pe.position.z - z;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance <= radius) { matches.push_back({pe.entity, pe.position, distance}); }
    }
    std::sort(matches.begin(), matches.end(),
             [](const Match& a, const Match& b) { return a.distance < b.distance; });

    nlohmann::json out = nlohmann::json::array();
    for (const Match& m : matches)
    {
        out.push_back({{"entity", m.entity.value()},
                       {"position", {{"x", m.position.x}, {"y", m.position.y}, {"z", m.position.z}}},
                       {"distance", m.distance}});
    }
    return {{"matches", out}};
}

nlohmann::json tool_scene_bounds(World& world, const ComponentReflection&, CommandStack&,
                                 const std::string&, const nlohmann::json&)
{
    const std::vector<PositionedEntity> entities = positioned_entities(world);
    if (entities.empty())
    {
        return {{"min", nullptr}, {"max", nullptr}, {"entity_count", 0}};
    }
    mosaic::foundation::math::Vec3 lo = entities.front().position;
    mosaic::foundation::math::Vec3 hi = entities.front().position;
    for (const PositionedEntity& pe : entities)
    {
        lo.x = std::min(lo.x, pe.position.x); hi.x = std::max(hi.x, pe.position.x);
        lo.y = std::min(lo.y, pe.position.y); hi.y = std::max(hi.y, pe.position.y);
        lo.z = std::min(lo.z, pe.position.z); hi.z = std::max(hi.z, pe.position.z);
    }
    return {{"min", {{"x", lo.x}, {"y", lo.y}, {"z", lo.z}}},
           {"max", {{"x", hi.x}, {"y", hi.y}, {"z", hi.z}}},
           {"entity_count", entities.size()}};
}

nlohmann::json tool_set_script(World& world, const ComponentReflection& types, CommandStack& stack,
                               const nlohmann::json& args, const ScriptEngineFactory& script_factory)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string source = require_string(args, "source");
    mosaic::studio::ScriptEditorModel model(script_factory);
    const mosaic::studio::ScriptApplyResult result = model.apply(source, world, types, e, stack);
    return {{"ok", result.kind == mosaic::studio::ScriptApplyResult::Kind::Ok}, {"message", result.message}};
}


constexpr std::uint64_t kMaxTicksPerCall = 10000;

nlohmann::json tool_tick(World& world, const ComponentReflection&, CommandStack&,
                         const std::string&, const nlohmann::json& args)
{
    const std::uint64_t count = optional_uint64(args, "count", 1);
    const double dt = optional_double(args, "dt", 1.0 / 60.0);
    if (count > kMaxTicksPerCall)
    {
        throw McpToolError("count exceeds the per-call limit of " + std::to_string(kMaxTicksPerCall));
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        world.tick(static_cast<float>(dt));
    }
    return {{"ticks", count}};
}

nlohmann::json tool_tick_until(World& world, const ComponentReflection& types, CommandStack&,
                               const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string component = require_string(args, "component");
    const std::string field = require_string(args, "field");
    const std::string op = require_string(args, "op");
    if (!args.contains("value"))
    {
        throw McpToolError("missing 'value'");
    }
    if (valid_compare_ops().find(op) == valid_compare_ops().end())
    {
        throw McpToolError("unknown op: " + op + " (expected one of eq, ne, gt, gte, lt, lte)");
    }
    const nlohmann::json target = args.at("value");
    const std::vector<std::string> path = mosaic::engine::animation::split_path(field, '.');
    const std::uint64_t max_ticks = optional_uint64(args, "max_ticks", kMaxTicksPerCall);
    const double dt = optional_double(args, "dt", 1.0 / 60.0);
    if (max_ticks > kMaxTicksPerCall)
    {
        throw McpToolError("max_ticks exceeds the per-call limit of " + std::to_string(kMaxTicksPerCall));
    }

    auto observe = [&]() -> std::optional<nlohmann::json>
    {
        const auto v = types.component_value(world, e, component);
        if (!v) { return std::nullopt; }
        return json_at_path(value_to_json(*v), path);
    };
    auto is_met = [&](const std::optional<nlohmann::json>& value)
    { return value.has_value() && compare(*value, op, target); };

    std::optional<nlohmann::json> value = observe();
    std::uint64_t ticks = 0;
    while (!is_met(value) && ticks < max_ticks)
    {
        world.tick(static_cast<float>(dt));
        ++ticks;
        value = observe();
    }
    return {{"ticks", ticks}, {"met", is_met(value)}, {"value", value ? *value : nlohmann::json(nullptr)}};
}

nlohmann::json tool_script_profile(World& world, const ComponentReflection&, CommandStack&,
                                   const std::string&, const nlohmann::json&)
{
    auto* sys = world.get_system<mosaic::engine::script::ScriptSystem>();
    if (sys == nullptr) { return {{"entries", nlohmann::json::array()}}; }
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& e : sys->profile())
    {
        entries.push_back({{"entity", e.entity}, {"calls", e.calls},
                           {"total_ms", e.total_ms}, {"max_ms", e.max_ms}});
    }
    return {{"entries", entries}};
}

// Shared by every tool taking an optional `source` argument (plugin_list,
// plugin_install): a registered named source resolves to its path, anything
// else is a literal path, and omitting it entirely falls back to the
// compile-time bundled packages/ directory -- mirrors the CLI's own
// resolve_package_source() (Options-based, not reusable here as-is).
std::string resolve_plugin_source(const nlohmann::json& args)
{
    const std::string requested = optional_string(args, "source", "");
    return requested.empty() ? mosaic::app::default_package_source() : mosaic::app::resolve_source(requested);
}

nlohmann::json tool_plugin_list(World&, const ComponentReflection&, CommandStack&,
                                const std::string&, const nlohmann::json& args)
{
    const std::string source = resolve_plugin_source(args);
    nlohmann::json packages = nlohmann::json::array();
    for (const mosaic::app::PackageInfo& p : mosaic::app::list_packages(source))
    {
        packages.push_back({{"name", p.name},
                            {"version", p.version},
                            {"description", p.description},
                            {"dir", p.dir.string()}});
    }
    return {{"packages", packages}};
}

nlohmann::json tool_build_project(const ComponentReflection& types, const std::string& project_dir,
                                  const nlohmann::json& args)
{
    const std::string out_dir = optional_string(args, "out_dir",
                                                (std::filesystem::path(project_dir) / "dist").string());
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) { return {{"ok", false}}; }
    return {{"ok", mosaic::app::build_bundle(project_dir, out_dir, exe.string(), types)}};
}

nlohmann::json tool_plugin_install(const std::string& project_dir, const nlohmann::json& args)
{
    const std::string name = require_string(args, "name");
    const std::string source = resolve_plugin_source(args);
    const auto pkg = mosaic::app::find_package(source, name);
    if (!pkg) { return {{"ok", false}}; }
    return {{"ok", mosaic::app::install_package(project_dir, *pkg)}};
}

nlohmann::json tool_plugin_remove(const std::string& project_dir, const nlohmann::json& args)
{
    const std::string name = require_string(args, "name");
    return {{"ok", mosaic::app::remove_package(project_dir, name)}};
}

// List every *.py stem in `project_dir`/plugins, sorted for a deterministic result --
// mirrors PluginHost::load's own non-recursive *.py scan, but names only (no execution).
// A missing or non-directory plugins/ (no plugins authored yet, the common case for a
// fresh project) returns an empty list, matching every other plugin_* tool's convention
// of "not found" being a normal result, never an error.
nlohmann::json tool_plugin_script_list(const std::string& project_dir, const nlohmann::json&)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path plugins_dir = fs::path(project_dir) / "plugins";
    std::vector<std::string> names;
    if (fs::is_directory(plugins_dir, ec))
    {
        std::error_code it_ec;
        for (auto it = fs::directory_iterator(plugins_dir, ec); it != fs::directory_iterator();
            it.increment(it_ec))
        {
            if (it_ec) { break; }
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || file_ec || it->path().extension() != ".py") { continue; }
            names.push_back(it->path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return {{"scripts", names}};
}

// Reads back a project-local plugin's source, the read half of write/read authoring --
// so an agent can fetch a plugin's current source before editing it, the same "read
// before write" shape get_component/set_component already establish for components.
// ok:false (never a thrown error) for an unsafe name or a plugin that doesn't exist,
// matching every other plugin_* tool's not-found convention.
nlohmann::json tool_plugin_script_read(const std::string& project_dir, const nlohmann::json& args)
{
    const std::string name = require_string(args, "name");
    if (!mosaic::app::is_safe_source_name(name)) { return {{"ok", false}, {"source", nullptr}}; }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path path = fs::path(project_dir) / "plugins" / (name + ".py");
    if (!fs::is_regular_file(path, ec)) { return {{"ok", false}, {"source", nullptr}}; }
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return {{"ok", true}, {"source", ss.str()}};
}

// Writes (creating or overwriting) `project_dir`/plugins/<name>.py, but only after
// test-loading it via a throwaway PluginHost against a scratch directory containing
// ONLY the new file -- mirrors set_script's "throwaway compile-check" (a fresh engine,
// still bound to the real live World/commands, exactly as PluginHost::load always runs
// plugin top-level code) while never re-running every OTHER already-installed plugin's
// side effects just to validate one new file, and never leaving a broken script behind
// in the project's real plugins/ on failure. ok:false (not a thrown error) for an unsafe
// name or a script that fails to load, mirroring set_script's own "leave it untouched on
// error" contract -- see `message` for the compile/dict-shape error.
nlohmann::json tool_plugin_script_write(World& world, const ComponentReflection& types,
                                        CommandStack& stack, const std::string& project_dir,
                                        const nlohmann::json& args,
                                        const ScriptEngineFactory& script_factory)
{
    const std::string name = require_string(args, "name");
    const std::string source = require_string(args, "source");
    if (!mosaic::app::is_safe_source_name(name))
    {
        return {{"ok", false}, {"message", "'" + name + "' is not a valid plugin name"}};
    }

    namespace fs = std::filesystem;
    static std::atomic<int> counter{0};
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path() /
        ("mosaic_mcp_plugin_script_write_" + std::to_string(::getpid()) + "_" +
         std::to_string(counter.fetch_add(1)));
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);
    {
        std::ofstream out(scratch / (name + ".py"), std::ios::binary | std::ios::trunc);
        out << source;
    }

    mosaic::studio::PluginHost throwaway(script_factory);
    const std::vector<std::string> errors = throwaway.load(scratch.string(), world, types, stack);
    fs::remove_all(scratch, ec);
    if (!errors.empty())
    {
        return {{"ok", false}, {"message", errors.front()}};
    }

    const fs::path plugins_dir = fs::path(project_dir) / "plugins";
    fs::create_directories(plugins_dir, ec);
    if (ec)
    {
        return {{"ok", false}, {"message", "failed to create " + plugins_dir.string() + ": " + ec.message()}};
    }
    std::ofstream out(plugins_dir / (name + ".py"), std::ios::binary | std::ios::trunc);
    out << source;
    if (!out)
    {
        return {{"ok", false}, {"message", "failed to write " + (plugins_dir / (name + ".py")).string()}};
    }

    nlohmann::json menu_items = nlohmann::json::array();
    for (const mosaic::studio::PluginMenuItem& item : throwaway.menu_items())
    {
        menu_items.push_back({{"label", item.label}, {"function", item.function_name}});
    }
    nlohmann::json panels = nlohmann::json::array();
    for (const mosaic::studio::PluginPanel& panel : throwaway.panels())
    {
        panels.push_back({{"label", panel.label}, {"function", panel.function_name}});
    }
    return {{"ok", true}, {"menu_items", menu_items}, {"panels", panels}};
}

// The remaining plugin_source_* tools operate on the user's global
// ~/.config/mosaic/sources.toml (via each function's own registry_path
// default), not anything project-scoped -- matching `mosaic plugin source`'s
// own CLI behavior exactly. Unlike every other tool in this file, they have
// no `project_dir` dependency at all.
nlohmann::json tool_plugin_source_add(World&, const ComponentReflection&, CommandStack&,
                                      const std::string&, const nlohmann::json& args)
{
    const std::string name = require_string(args, "name");
    const std::string path = require_string(args, "path");
    const bool ok = mosaic::app::is_git_url(path) ? mosaic::app::add_named_source_from_git(name, path)
                                                  : mosaic::app::add_named_source(name, path);
    return {{"ok", ok}};
}

nlohmann::json tool_plugin_source_list(World&, const ComponentReflection&, CommandStack&,
                                       const std::string&, const nlohmann::json&)
{
    nlohmann::json sources = nlohmann::json::array();
    for (const mosaic::app::NamedSource& s : mosaic::app::list_named_sources())
    {
        sources.push_back({{"name", s.name}, {"path", s.path}, {"git_url", s.git_url}});
    }
    return {{"sources", sources}};
}

nlohmann::json tool_plugin_source_remove(World&, const ComponentReflection&, CommandStack&,
                                         const std::string&, const nlohmann::json& args)
{
    const std::string name = require_string(args, "name");
    return {{"ok", mosaic::app::remove_named_source(name)}};
}

nlohmann::json tool_plugin_source_pull(World&, const ComponentReflection&, CommandStack&,
                                       const std::string&, const nlohmann::json& args)
{
    const std::string name = require_string(args, "name");
    return {{"ok", mosaic::app::pull_named_source(name)}};
}

class AssetRefCollector final : public mosaic::engine::reflection::FieldVisitor
{
public:
    AssetRefCollector(const World& world, const AssetRegistry& registry, nlohmann::json& out,
                      EntityID entity, const std::string& component)
        : world_(world), registry_(registry), out_(out), entity_(entity), component_(component)
    {
    }

    void field_asset(std::string_view name, AssetID& value) override
    {
        if (value.is_nil()) { return; }
        if (registry_.contains(value)) { return; }
        out_.push_back({{"kind", "asset"},
                        {"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)},
                        {"asset_id", value.to_string()}});
    }

    void field_handle_asset(std::string_view name, const AssetID& value) override
    {
        if (value.is_nil()) { return; }
        if (registry_.contains(value)) { return; }
        out_.push_back({{"kind", "asset"},
                        {"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)},
                        {"asset_id", value.to_string()}});
    }

    void field_entity(std::string_view name, mosaic::foundation::EntityID value) override
    {
        if (!value) { return; } // nil -- no reference set
        if (world_.is_valid(value)) { return; }
        out_.push_back({{"kind", "entity"},
                        {"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)},
                        {"entity_id", value.value()}});
    }

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view name) override { path_.push_back(std::string(name)); }
    void end_group() override { path_.pop_back(); }

private:
    std::string path_with(std::string_view leaf) const
    {
        std::string result;
        for (const std::string& seg : path_) { result += seg + "."; }
        result += leaf;
        return result;
    }

    const World& world_;
    const AssetRegistry& registry_;
    nlohmann::json& out_;
    EntityID entity_;
    std::string component_;
    std::vector<std::string> path_;
};

nlohmann::json tool_broken_references(World& world, const ComponentReflection& types,
                                      const AssetRegistry& asset_registry)
{
    nlohmann::json broken = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e))
            {
                AssetRefCollector collector(world, asset_registry, broken, e, name);
                types.inspect(world, e, name, collector);
            }
        });
    return {{"broken", broken}};
}

class ReferencedAssetGatherer final : public mosaic::engine::reflection::FieldVisitor
{
public:
    explicit ReferencedAssetGatherer(std::unordered_set<AssetID>& out) : out_(out) {}

    void field_asset(std::string_view, AssetID& value) override
    {
        if (!value.is_nil()) { out_.insert(value); }
    }

    void field_handle_asset(std::string_view, const AssetID& value) override
    {
        if (!value.is_nil()) { out_.insert(value); }
    }

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}

    // Unlike AssetRefCollector, this visitor never needs a field path (only which ids
    // exist, not where each came from), so no group-path stack to maintain here.
    void begin_group(std::string_view) override {}
    void end_group() override {}

private:
    std::unordered_set<AssetID>& out_;
};

nlohmann::json tool_orphaned_assets(World& world, const ComponentReflection& types,
                                    const AssetRegistry& asset_registry)
{
    std::unordered_set<AssetID> referenced;
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e))
            {
                ReferencedAssetGatherer gatherer(referenced);
                types.inspect(world, e, name, gatherer);
            }
        });

    nlohmann::json orphaned = nlohmann::json::array();
    asset_registry.for_each(
        [&](const mosaic::engine::assets::AssetMetadata& meta)
        {
            if (referenced.find(meta.uuid) == referenced.end())
            {
                orphaned.push_back({{"asset_id", meta.uuid.to_string()},
                                    {"type", meta.type},
                                    {"source", meta.source}});
            }
        });
    return {{"orphaned", orphaned}};
}

class AssetReferenceCollector final : public mosaic::engine::reflection::FieldVisitor
{
public:
    AssetReferenceCollector(AssetID target, nlohmann::json& out, EntityID entity,
                            const std::string& component)
        : target_(target), out_(out), entity_(entity), component_(component)
    {
    }

    void field_asset(std::string_view name, AssetID& value) override
    {
        if (value != target_) { return; }
        out_.push_back({{"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)}});
    }

    void field_handle_asset(std::string_view name, const AssetID& value) override
    {
        if (value != target_) { return; }
        out_.push_back({{"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)}});
    }

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view name) override { path_.push_back(std::string(name)); }
    void end_group() override { path_.pop_back(); }

private:
    std::string path_with(std::string_view leaf) const
    {
        std::string result;
        for (const std::string& seg : path_) { result += seg + "."; }
        result += leaf;
        return result;
    }

    AssetID target_;
    nlohmann::json& out_;
    EntityID entity_;
    std::string component_;
    std::vector<std::string> path_;
};

nlohmann::json tool_entities_referencing(World& world, const ComponentReflection& types, CommandStack&,
                                         const std::string&, const nlohmann::json& args)
{
    const std::string asset_id_str = require_string(args, "asset_id");
    AssetID target;
    try
    {
        target = AssetID::from_string(asset_id_str);
    }
    catch (const SerializationError& e)
    {
        throw McpToolError(std::string("invalid 'asset_id': ") + e.what());
    }

    nlohmann::json matches = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e))
            {
                AssetReferenceCollector collector(target, matches, e, name);
                types.inspect(world, e, name, collector);
            }
        });
    return {{"matches", matches}};
}

nlohmann::json tool_bulk_set_field(World& world, const ComponentReflection& types, CommandStack& stack,
                                   const std::string&, const nlohmann::json& args)
{
    const std::string component = require_string(args, "component");
    if (!args.contains("set") || !args.at("set").is_object())
    {
        throw McpToolError("missing or invalid 'set' (expected an object with 'field' and 'value')");
    }
    const nlohmann::json& set_args = args.at("set");
    const std::string set_field = require_string(set_args, "field");
    if (!set_args.contains("value"))
    {
        throw McpToolError("missing 'set.value'");
    }
    const nlohmann::json set_value = set_args.at("value");
    const std::vector<std::string> set_path = mosaic::engine::animation::split_path(set_field, '.');

    std::optional<std::string> where_op;
    nlohmann::json where_value;
    std::vector<std::string> where_path;
    const bool has_where = args.contains("where");
    if (has_where)
    {
        if (!args.at("where").is_object())
        {
            throw McpToolError("invalid 'where' (expected an object with 'field', 'op', 'value')");
        }
        const nlohmann::json& where_args = args.at("where");
        const std::string where_field = require_string(where_args, "field");
        where_op = require_string(where_args, "op");
        if (!where_args.contains("value"))
        {
            throw McpToolError("missing 'where.value'");
        }
        if (valid_compare_ops().find(*where_op) == valid_compare_ops().end())
        {
            throw McpToolError("unknown where.op: " + *where_op + " (expected one of eq, ne, gt, gte, lt, lte)");
        }
        where_value = where_args.at("value");
        where_path = mosaic::engine::animation::split_path(where_field, '.');
    }

    std::vector<EntityID> matched;
    std::vector<Value> patched;
    world.for_each_entity(
        [&](EntityID e)
        {
            const auto v = types.component_value(world, e, component);
            if (!v) { return; }
            nlohmann::json component_json = value_to_json(*v);
            if (has_where)
            {
                const auto field_value = json_at_path(component_json, where_path);
                if (!field_value) { return; }
                if (!compare(*field_value, *where_op, where_value)) { return; }
            }
            if (!json_set_at(component_json, set_path, set_value))
            {
                throw McpToolError("'set.field' does not resolve on component '" + component + "': " + set_field);
            }
            matched.push_back(e);
            patched.push_back(json_to_value(component_json));
        });

    if (matched.empty())
    {
        return {{"updated", 0}, {"entities", nlohmann::json::array()}};
    }

    std::vector<std::unique_ptr<Command>> commands;
    std::vector<mosaic::engine::commands::SetComponentCommand*> set_commands;
    nlohmann::json updated_entities = nlohmann::json::array();
    for (std::size_t i = 0; i < matched.size(); ++i)
    {
        auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(
            world, types, matched[i], component, patched[i]);
        set_commands.push_back(cmd.get());
        commands.push_back(std::move(cmd));
        updated_entities.push_back(matched[i].value());
    }
    stack.execute(std::make_unique<CompositeCommand>("mcp.bulk_set_field", std::move(commands)));

    // json_set_at only validated that the field PATH resolves, not that set.value's type
    // matches the field's expected type (e.g. writing a number into a string field) -- that
    // only surfaces once SetComponentCommand::execute() actually tries to deserialize it via
    // reflection. Check every sub-command's valid() now, after the batch ran: if the type was
    // wrong, it was wrong identically for every matched entity of this component type, so
    // undo the whole composite (a correct no-op for any sub-command that never applied) and
    // report the failure, rather than silently reporting success for a no-op write.
    for (const auto* cmd : set_commands)
    {
        if (!cmd->valid())
        {
            stack.undo();
            throw McpToolError("'set.value' is not valid for field '" + set_field +
                               "' on component '" + component + "'");
        }
    }

    return {{"updated", static_cast<std::uint64_t>(matched.size())}, {"entities", updated_entities}};
}

const std::unordered_map<std::string, ToolHandler>& tool_handlers()
{
    static const std::unordered_map<std::string, ToolHandler> handlers = {
        {"create_entity", &tool_create_entity},   {"destroy_entity", &tool_destroy_entity},
        {"get_component", &tool_get_component},   {"set_component", &tool_set_component},
        {"add_component", &tool_add_component},   {"remove_component", &tool_remove_component},
        {"has_component", &tool_has_component},   {"component_names", &tool_component_names},
        {"entities", &tool_entities},             {"entities_with", &tool_entities_with},
        {"entities_where", &tool_entities_where},
        {"entity_count", &tool_entity_count},     {"undo", &tool_undo},
        {"redo", &tool_redo},                     {"undo_depth", &tool_undo_depth},
        {"save_scene", &tool_save_scene},
        {"describe_scene", &tool_describe_scene},
        {"entities_near", &tool_entities_near},
        {"scene_bounds", &tool_scene_bounds},
        {"tick", &tool_tick},
        {"tick_until", &tool_tick_until},
        {"script_profile", &tool_script_profile},
        {"bulk_set_field", &tool_bulk_set_field},
        {"entities_referencing", &tool_entities_referencing},
        {"plugin_list", &tool_plugin_list},
        {"plugin_source_add", &tool_plugin_source_add},
        {"plugin_source_list", &tool_plugin_source_list},
        {"plugin_source_remove", &tool_plugin_source_remove},
        {"plugin_source_pull", &tool_plugin_source_pull},
    };
    return handlers;
}

nlohmann::json schema_none()
{
    return {{"type", "object"}, {"properties", nlohmann::json::object()}, {"required", nlohmann::json::array()}};
}

nlohmann::json schema_entity()
{
    return {{"type", "object"},
           {"properties", {{"entity", {{"type", "integer"}, {"minimum", 0}}}}},
           {"required", {"entity"}}};
}

nlohmann::json schema_entity_component()
{
    return {{"type", "object"},
           {"properties",
            {{"entity", {{"type", "integer"}, {"minimum", 0}}}, {"component", {{"type", "string"}}}}},
           {"required", {"entity", "component"}}};
}

nlohmann::json schema_entity_component_value()
{
    nlohmann::json s = schema_entity_component();
    s["properties"]["value"] = {{"description", "The component's fields as JSON"}};
    s["required"].push_back("value");
    return s;
}

nlohmann::json schema_component()
{
    return {{"type", "object"}, {"properties", {{"component", {{"type", "string"}}}}}, {"required", {"component"}}};
}

nlohmann::json schema_asset_id()
{
    return {{"type", "object"},
           {"properties", {{"asset_id", {{"type", "string"}}}}},
           {"required", {"asset_id"}}};
}

nlohmann::json schema_entities_where()
{
    return {{"type", "object"},
           {"properties",
            {{"component", {{"type", "string"}}},
             {"field", {{"type", "string"},
                       {"description", "Dotted path into the component's fields, e.g. \"position.x\""}}},
             {"op", {{"type", "string"}, {"enum", {"eq", "ne", "gt", "gte", "lt", "lte"}}}},
             {"value", {{"description", "The value to compare against"}}}}},
           {"required", {"component", "field", "op", "value"}}};
}

nlohmann::json schema_entities_near()
{
    return {{"type", "object"},
           {"properties",
            {{"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}, {"z", {{"type", "number"}}},
             {"radius", {{"type", "number"}, {"minimum", 0}}}}},
           {"required", {"x", "y", "z", "radius"}}};
}

nlohmann::json schema_bulk_set_field()
{
    nlohmann::json where_schema = {
        {"type", "object"},
        {"properties",
         {{"field", {{"type", "string"}}},
          {"op", {{"type", "string"}, {"enum", {"eq", "ne", "gt", "gte", "lt", "lte"}}}},
          {"value", {{"description", "The value to compare against"}}}}},
        {"required", {"field", "op", "value"}}};
    nlohmann::json set_schema = {
        {"type", "object"},
        {"properties",
         {{"field", {{"type", "string"},
                    {"description", "Dotted path into the component's fields, e.g. \"value\" or \"position.x\""}}},
          {"value", {{"description", "The new value to write at that field"}}}}},
        {"required", {"field", "value"}}};
    return {{"type", "object"},
           {"properties",
            {{"component", {{"type", "string"}}},
             {"where", where_schema},
             {"set", set_schema}}},
           {"required", {"component", "set"}}};
}

nlohmann::json schema_entity_source()
{
    return {{"type", "object"},
           {"properties",
            {{"entity", {{"type", "integer"}, {"minimum", 0}}}, {"source", {{"type", "string"}}}}},
           {"required", {"entity", "source"}}};
}

nlohmann::json schema_source()
{
    return {{"type", "object"}, {"properties", {{"source", {{"type", "string"}}}}}, {"required", {"source"}}};
}

nlohmann::json schema_tick()
{
    return {{"type", "object"},
           {"properties",
            {{"count", {{"type", "integer"}, {"minimum", 0}}}, {"dt", {{"type", "number"}}}}},
           {"required", nlohmann::json::array()}};
}

nlohmann::json schema_tick_until()
{
    return {{"type", "object"},
           {"properties",
            {{"entity", {{"type", "integer"}, {"minimum", 0}}},
             {"component", {{"type", "string"}}},
             {"field", {{"type", "string"},
                       {"description", "Dotted path into the component's fields, e.g. \"position.x\""}}},
             {"op", {{"type", "string"}, {"enum", {"eq", "ne", "gt", "gte", "lt", "lte"}}}},
             {"value", {{"description", "The value to compare against"}}},
             {"max_ticks", {{"type", "integer"}, {"minimum", 0}}},
             {"dt", {{"type", "number"}}}}},
           {"required", {"entity", "component", "field", "op", "value"}}};
}

nlohmann::json schema_build()
{
    return {{"type", "object"}, {"properties", {{"out_dir", {{"type", "string"}}}}}, {"required", nlohmann::json::array()}};
}

nlohmann::json schema_plugin_list()
{
    return {{"type", "object"}, {"properties", {{"source", {{"type", "string"}}}}}, {"required", nlohmann::json::array()}};
}

nlohmann::json schema_plugin_install()
{
    return {{"type", "object"},
           {"properties", {{"name", {{"type", "string"}}}, {"source", {{"type", "string"}}}}},
           {"required", {"name"}}};
}

nlohmann::json schema_plugin_remove()
{
    return {{"type", "object"},
           {"properties", {{"name", {{"type", "string"}}}}},
           {"required", {"name"}}};
}

nlohmann::json schema_plugin_script_read()
{
    return {{"type", "object"},
           {"properties", {{"name", {{"type", "string"}}}}},
           {"required", {"name"}}};
}

nlohmann::json schema_plugin_script_write()
{
    return {{"type", "object"},
           {"properties", {{"name", {{"type", "string"}}}, {"source", {{"type", "string"}}}}},
           {"required", {"name", "source"}}};
}

nlohmann::json schema_plugin_source_add()
{
    return {{"type", "object"},
           {"properties",
            {{"name", {{"type", "string"}}},
             {"path", {{"description", "A local directory path, or a git URL (auto-detected)"}}}}},
           {"required", {"name", "path"}}};
}

nlohmann::json tool_import_asset(const std::string& project_dir, const nlohmann::json& args,
                                 AssetRegistry* live_asset_registry)
{
    const std::string source = require_string(args, "source");
    const auto asset_id = mosaic::app::import_asset(project_dir, source);
    if (asset_id && live_asset_registry != nullptr)
    {
        // Re-scan is idempotent (AssetRegistry::register_asset overwrites by uuid), so
        // rerunning the exact same trusted startup loader is simplest and safest -- no new
        // per-type loading logic to keep in sync with load_project_assets' own.
        mosaic::app::load_project_assets(*live_asset_registry,
                                         (std::filesystem::path(project_dir) / "assets").string());
    }
    return {{"asset_id", asset_id ? nlohmann::json(asset_id->to_string()) : nlohmann::json(nullptr)}};
}
} // namespace

nlohmann::json tools_list_schema()
{
    nlohmann::json tools = nlohmann::json::array();
    tools.push_back({{"name", "create_entity"},
                     {"description", "Create a new entity and return its id."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "destroy_entity"},
                     {"description", "Destroy an entity (deferred to the next World tick); "
                                     "ok is false if the id isn't valid."},
                     {"inputSchema", schema_entity()}});
    tools.push_back({{"name", "get_component"},
                     {"description", "Return a component's fields as JSON, or null if the "
                                     "entity doesn't have it."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "set_component"},
                     {"description", "Set a component's fields from JSON; routes through the "
                                     "undo stack. ok is false if the set failed."},
                     {"inputSchema", schema_entity_component_value()}});
    tools.push_back({{"name", "add_component"},
                     {"description", "Attach a default-constructed component to an entity by name."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "remove_component"},
                     {"description", "Remove a component from an entity by name."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "has_component"},
                     {"description", "Return whether an entity currently has a component by name."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "component_names"},
                     {"description", "Return the names of every component currently attached to an entity."},
                     {"inputSchema", schema_entity()}});
    tools.push_back({{"name", "entities"},
                     {"description", "Return the ids of every live entity."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "entities_with"},
                     {"description", "Return the ids of every entity that currently has the named component."},
                     {"inputSchema", schema_component()}});
    tools.push_back({{"name", "entities_where"},
                     {"description", "Return the ids and actual field values of every entity whose "
                                     "component has a field matching a condition (op: eq, ne, gt, "
                                     "gte, lt, lte). A missing component, an unresolvable field path, "
                                     "or a numeric op against a non-numeric value gracefully excludes "
                                     "that entity rather than erroring."},
                     {"inputSchema", schema_entities_where()}});
    tools.push_back({{"name", "entity_count"},
                     {"description", "Return the number of live entities."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "undo"},
                     {"description", "Undo the last recorded command; ok is false if there's nothing to undo."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "redo"},
                     {"description", "Redo the last undone command; ok is false if there's nothing to redo."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "undo_depth"},
                     {"description", "Return how many commands are currently undoable."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "save_scene"},
                     {"description", "Write the current World back to the project's scene file. "
                                     "Returns the path written."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "describe_scene"},
                     {"description", "Return a snapshot of the scene's shape: entity count, a "
                                     "component-type histogram, and the Transform parent-child "
                                     "hierarchy as a tree."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "entities_near"},
                     {"description", "Every entity with a Transform whose live world position "
                                     "is within `radius` of (x, y, z), nearest first, each with "
                                     "its actual position and distance. Position is computed on "
                                     "demand honoring the parent chain (the same composition "
                                     "TransformSystem itself uses) -- always live, never a stale "
                                     "cached WorldTransform, so it reflects edits made earlier in "
                                     "this same session."},
                     {"inputSchema", schema_entities_near()}});
    tools.push_back({{"name", "scene_bounds"},
                     {"description", "The point-cloud bounding box (min/max corners) over every "
                                     "entity with a Transform's live world position, plus how "
                                     "many entities contributed. This is entity positions only, "
                                     "not mesh geometry extents -- an entity's actual rendered "
                                     "footprint (mesh bounds, particle emitter radius, etc.) can "
                                     "extend beyond this box. min/max are both null with "
                                     "entity_count 0 for a scene with no positioned entities."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "set_script"},
                     {"description", "Test-compile a Python script against the live World and, on "
                                     "success, write it into the entity's existing ScriptComponent. "
                                     "Attach a ScriptComponent first via add_component. ok is false "
                                     "(not an error) on a compile failure or a missing component -- "
                                     "see message for details."},
                     {"inputSchema", schema_entity_source()}});
    tools.push_back({{"name", "import_asset"},
                     {"description", "Import a file into the project's asset pipeline (images, "
                                     ".py scripts, .wav audio, .ttf/.otf fonts, or a project's "
                                     "MOSAIC_IMPORTERS plugin extensions), cooked and registered "
                                     "exactly like `mosaic import`. asset_id is null on failure "
                                     "(unsupported format, missing source, or a stem collision) -- "
                                     "the specific reason is only visible in the server's own log "
                                     "output, not returned here. On success the new asset is "
                                     "immediately live: broken_references/orphaned_assets see it "
                                     "right away in the same session, no mosaic mcp restart "
                                     "needed."},
                     {"inputSchema", schema_source()}});
    tools.push_back({{"name", "tick"},
                     {"description", "Advance the simulation by `count` ticks (default 1, max "
                                     "10000 per call) of `dt` seconds each (default 1/60). Runs "
                                     "ScriptSystem, PhysicsSystem, and every other system "
                                     "mosaic run/editor use at play time. Returns the number of "
                                     "ticks actually run."},
                     {"inputSchema", schema_tick()}});
    tools.push_back({{"name", "tick_until"},
                     {"description", "Tick the simulation (dt seconds each, default 1/60) one "
                                     "tick at a time, rechecking `component.field` on `entity` "
                                     "against `op`/`value` after each tick, stopping as soon as "
                                     "it matches or `max_ticks` (default/max 10000) is reached. "
                                     "Checks before ticking at all, so an already-met condition "
                                     "costs zero ticks. An unresolvable entity/component/field "
                                     "never matches (same convention as entities_where) rather "
                                     "than erroring, so it simply runs to max_ticks. Returns "
                                     "ticks actually run, whether it was met, and the last "
                                     "observed field value (null if never resolved)."},
                     {"inputSchema", schema_tick_until()}});
    tools.push_back({{"name", "build_project"},
                     {"description", "Package the project into a standalone bundle (default "
                                     "output <project>/dist, or a given out_dir), embedding this "
                                     "running mosaic binary as the runtime -- the same operation "
                                     "`mosaic build` performs. ok is false on any failure (the "
                                     "specific reason is only visible in the server's own log "
                                     "output, matching import_asset's same convention)."},
                     {"inputSchema", schema_build()}});
    tools.push_back({{"name", "plugin_list"},
                     {"description", "List available packages in a package source -- the same "
                                     "listing `mosaic plugin list` prints. `source` is optional: "
                                     "a registered named source (see `mosaic plugin source "
                                     "list`) resolves to its path, anything else is used as a "
                                     "literal path, and omitting it uses the compile-time bundled "
                                     "packages/ directory. A missing or empty source directory "
                                     "returns an empty list, never an error."},
                     {"inputSchema", schema_plugin_list()}});
    tools.push_back({{"name", "plugin_install"},
                     {"description", "Install package `name` into the project (same effect as "
                                     "`mosaic plugin install <name> <project>`): copies the "
                                     "package's *.py files into plugins/ and records it in "
                                     "mosaic.toml. `source` resolves the same way plugin_list's "
                                     "does. ok is false (not an error) if the package isn't found "
                                     "in that source, or if install fails for any other reason -- "
                                     "the specific reason is only visible in the server's own log "
                                     "output, matching import_asset's same convention. "
                                     "Reinstalling the same name/version is a no-op success; a "
                                     "different version overwrites the package's own files only."},
                     {"inputSchema", schema_plugin_install()}});
    tools.push_back({{"name", "plugin_remove"},
                     {"description", "Remove package `name` from the project (same effect as "
                                     "`mosaic plugin remove <name> <project>`): deletes the "
                                     "*.py files it owns from plugins/ and drops its mosaic.toml "
                                     "entry. Removing a package that isn't installed is a no-op "
                                     "success (ok:true), not an error."},
                     {"inputSchema", schema_plugin_remove()}});
    tools.push_back({{"name", "plugin_script_list"},
                     {"description", "List the project's authored plugins/*.py files by name "
                                     "(sorted), no execution. Distinct from plugin_list, which "
                                     "lists installable packages in a package source -- this "
                                     "lists what's already on disk under the project's own "
                                     "plugins/. Empty if plugins/ doesn't exist yet."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "plugin_script_read"},
                     {"description", "Read a project-local plugin's current source by name "
                                     "(no .py extension), for editing before calling "
                                     "plugin_script_write. ok is false (not an error) if the "
                                     "name is invalid or no such plugin exists."},
                     {"inputSchema", schema_plugin_script_read()}});
    tools.push_back({{"name", "plugin_script_write"},
                     {"description", "Author a project-local plugin: write `source` to "
                                     "plugins/<name>.py, declaring MOSAIC_MENU_ITEMS/"
                                     "MOSAIC_PANELS dicts mapping a label to a zero-argument "
                                     "function name to register a Studio menu item/panel (see "
                                     "the bundled hello-plugin package for an example). Before "
                                     "writing, test-loads the script via a throwaway plugin "
                                     "host bound to the live World -- ok is false (not an "
                                     "error, see message) on a compile error or a malformed "
                                     "MOSAIC_MENU_ITEMS/MOSAIC_PANELS dict, and the real "
                                     "plugins/ directory is left untouched. On success, "
                                     "creates or overwrites the file and returns the "
                                     "menu_items/panels it registered; call Studio's "
                                     "\"Reload Plugins\" (or restart mosaic mcp) to pick up "
                                     "the change in a running session."},
                     {"inputSchema", schema_plugin_script_write()}});
    tools.push_back({{"name", "plugin_source_add"},
                     {"description", "Register a named package source (same effect as "
                                     "`mosaic plugin source add <name> <path-or-git-url>`), "
                                     "usable afterward as `plugin_list`/`plugin_install`'s "
                                     "`source` argument. `path` is auto-detected as a git URL "
                                     "or a local directory path. A git URL is cloned into "
                                     "~/.config/mosaic/sources/<name>/ first. Operates on the "
                                     "user's global source registry, not anything project-"
                                     "scoped. ok is false if `name` is already registered, the "
                                     "path isn't a real directory, or the clone fails."},
                     {"inputSchema", schema_plugin_source_add()}});
    tools.push_back({{"name", "plugin_source_list"},
                     {"description", "List every registered named package source (same effect "
                                     "as `mosaic plugin source list`): name, path, and git_url "
                                     "(empty for a plain local-path source) for each."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "plugin_source_remove"},
                     {"description", "Unregister a named package source (same effect as "
                                     "`mosaic plugin source remove <name>`); for a git-backed "
                                     "source, also deletes its clone directory. Removing a name "
                                     "that isn't registered is a no-op success (ok:true)."},
                     {"inputSchema", schema_plugin_remove()}});
    tools.push_back({{"name", "plugin_source_pull"},
                     {"description", "Run `git pull` on a git-backed named source's clone "
                                     "directory (same effect as `mosaic plugin source pull "
                                     "<name>`). ok is false if `name` isn't registered, isn't "
                                     "git-backed, or the pull itself fails."},
                     {"inputSchema", schema_plugin_remove()}});
    tools.push_back({{"name", "script_profile"},
                     {"description", "Return per-scripted-entity on_update timing accumulated "
                                     "so far this session (via ScriptSystem::profile() -- the "
                                     "same machinery `mosaic run --script-profile` uses): calls, "
                                     "total_ms, and max_ms per entity. Empty if nothing has "
                                     "ticked yet, or if no ScriptSystem is wired."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "broken_references"},
                     {"description", "Scan every entity's every component for two kinds of "
                                     "broken reference, each entry tagged with a 'kind' field: "
                                     "'asset' -- an AssetID field (bare or inside a Handle<R>) "
                                     "that is set (non-nil) but doesn't resolve in the live "
                                     "AssetRegistry (entry has 'asset_id'); 'entity' -- an "
                                     "EntityID field that is set (non-nil) but doesn't name a "
                                     "currently-valid entity (entry has 'entity_id'). The "
                                     "AssetRegistry stays live across the whole session -- "
                                     "import_asset refreshes it immediately, so an asset imported "
                                     "earlier this session is already resolvable here, not a "
                                     "false positive."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "orphaned_assets"},
                     {"description", "Return every registered asset that no entity's component "
                                     "field currently references (gathered via the same AssetID "
                                     "field walk broken_references uses, covering both bare "
                                     "AssetID fields and Handle<R> fields). An asset referenced "
                                     "only via an EntityID field wouldn't be detected -- EntityID "
                                     "reference fields aren't distinguishable from other scalar "
                                     "fields yet. The AssetRegistry stays live across the whole "
                                     "session -- import_asset refreshes it immediately, so an "
                                     "asset imported earlier this session is already enumerated "
                                     "here, not silently absent."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "bulk_set_field"},
                     {"description", "Set one field to a new value on every entity matching an "
                                     "optional filter (reusing entities_where's exact "
                                     "where.field/op/value shape; omit 'where' to target every "
                                     "entity with 'component'). Applied atomically as one "
                                     "undoable step -- a single undo call reverts every entity "
                                     "this touched, not just the last one. Covers bulk renames "
                                     "(e.g. set field \"value\" on Name) as the common case. A "
                                     "'set.field' that doesn't resolve on a matched entity's "
                                     "component throws before anything is mutated -- note this is "
                                     "only checked against entities that actually match, so "
                                     "updated:0 doesn't distinguish 'nothing matched the filter' "
                                     "from 'set.field would have been invalid'; a 'set.value' "
                                     "whose type doesn't fit the field also throws (after undoing "
                                     "any partial apply) rather than silently reporting success "
                                     "for a no-op write."},
                     {"inputSchema", schema_bulk_set_field()}});
    tools.push_back({{"name", "entities_referencing"},
                     {"description", "Return every entity/component/field whose AssetID field "
                                     "(bare or inside a Handle<R>) matches the given asset_id "
                                     "(the reverse of asking 'what does this entity reference' -- "
                                     "this asks 'what references this asset'). A nil AssetID field "
                                     "never matches. asset_id must be a well-formed UUID string; "
                                     "a malformed one throws."},
                     {"inputSchema", schema_asset_id()}});
    return {{"tools", tools}};
}

nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments, const ScriptEngineFactory& script_factory,
                                const std::string& project_dir, const AssetRegistry& asset_registry,
                                AssetRegistry* live_asset_registry)
{
    if (tool_name == "set_script")
    {
        return tool_set_script(world, types, stack, arguments, script_factory);
    }
    if (tool_name == "import_asset")
    {
        return tool_import_asset(project_dir, arguments, live_asset_registry);
    }
    if (tool_name == "build_project")
    {
        return tool_build_project(types, project_dir, arguments);
    }
    if (tool_name == "plugin_install")
    {
        return tool_plugin_install(project_dir, arguments);
    }
    if (tool_name == "plugin_remove")
    {
        return tool_plugin_remove(project_dir, arguments);
    }
    if (tool_name == "plugin_script_list")
    {
        return tool_plugin_script_list(project_dir, arguments);
    }
    if (tool_name == "plugin_script_read")
    {
        return tool_plugin_script_read(project_dir, arguments);
    }
    if (tool_name == "plugin_script_write")
    {
        return tool_plugin_script_write(world, types, stack, project_dir, arguments, script_factory);
    }
    if (tool_name == "broken_references")
    {
        return tool_broken_references(world, types, asset_registry);
    }
    if (tool_name == "orphaned_assets")
    {
        return tool_orphaned_assets(world, types, asset_registry);
    }
    const auto& handlers = tool_handlers();
    const auto it = handlers.find(tool_name);
    if (it == handlers.end())
    {
        throw McpToolError("unknown tool: " + tool_name);
    }
    return it->second(world, types, stack, scene_path, arguments);
}
} // namespace mosaic::app::mcp
