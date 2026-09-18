#include "app/mcp/mcp_tools.hpp"

#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>

#include "app/asset/asset_io.hpp"
#include "app/run/scene_io.hpp"
#include "engine/commands/set_component_command.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "engine/scene/scene.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "studio/script_editor.hpp"

namespace mosaic::app::mcp
{
namespace
{
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::EntityID;
using mosaic::foundation::commands::CommandStack;
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

nlohmann::json tool_set_script(World& world, const ComponentReflection& types,
                               const nlohmann::json& args, const ScriptEngineFactory& script_factory)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string source = require_string(args, "source");
    mosaic::studio::ScriptEditorModel model(script_factory);
    const mosaic::studio::ScriptApplyResult result = model.apply(source, world, types, e);
    return {{"ok", result.kind == mosaic::studio::ScriptApplyResult::Kind::Ok}, {"message", result.message}};
}

const std::unordered_map<std::string, ToolHandler>& tool_handlers()
{
    static const std::unordered_map<std::string, ToolHandler> handlers = {
        {"create_entity", &tool_create_entity},   {"destroy_entity", &tool_destroy_entity},
        {"get_component", &tool_get_component},   {"set_component", &tool_set_component},
        {"add_component", &tool_add_component},   {"remove_component", &tool_remove_component},
        {"has_component", &tool_has_component},   {"component_names", &tool_component_names},
        {"entities", &tool_entities},             {"entities_with", &tool_entities_with},
        {"entity_count", &tool_entity_count},     {"undo", &tool_undo},
        {"redo", &tool_redo},                     {"undo_depth", &tool_undo_depth},
        {"save_scene", &tool_save_scene},
        {"describe_scene", &tool_describe_scene},
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

nlohmann::json tool_import_asset(const std::string& project_dir, const nlohmann::json& args)
{
    const std::string source = require_string(args, "source");
    const auto asset_id = mosaic::app::import_asset(project_dir, source);
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
                                     "output, not returned here."},
                     {"inputSchema", schema_source()}});
    return {{"tools", tools}};
}

nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments, const ScriptEngineFactory& script_factory,
                                const std::string& project_dir)
{
    if (tool_name == "set_script")
    {
        return tool_set_script(world, types, arguments, script_factory);
    }
    if (tool_name == "import_asset")
    {
        return tool_import_asset(project_dir, arguments);
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
