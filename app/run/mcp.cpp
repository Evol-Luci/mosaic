#include "app/run/mcp.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "app/mcp/mcp_protocol.hpp"
#include "app/mcp/mcp_tools.hpp"
#include "app/project/project.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_load.hpp"
#include "app/asset/asset_io.hpp"
#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

namespace mosaic::app
{
int run_mcp(const Options& options)
{
    if (!options.mcp_project)
    {
        mosaic::foundation::log::error("mcp", "no project directory given");
        return 1;
    }
    const std::string& project_dir = *options.mcp_project;

    const std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("mcp", "not a Mosaic project (no mosaic.toml): {}", project_dir);
        return 1;
    }
    const std::string scene_path =
        (std::filesystem::path(project_dir) / manifest->default_scene).string();

    mosaic::foundation::reflection::TypeRegistry registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    mosaic::engine::World world;
    mosaic::foundation::commands::CommandStack stack;

    Options load_options;
    load_options.scene = scene_path;
    if (!populate_world(world, load_options, types))
    {
        mosaic::foundation::log::error("mcp", "failed to load scene: {}", scene_path);
        return 1;
    }

    mosaic::engine::assets::AssetRegistry asset_registry;
    load_project_assets(asset_registry, (std::filesystem::path(project_dir) / "assets").string());

    const mcp::ScriptEngineFactory script_factory =
        [] { return mosaic::script::python::create_python_script_engine(); };

    setup_runtime(world, asset_registry, types, script_factory);

    const mcp::ToolDispatch dispatch =
        [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    {
        return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments,
                                     script_factory, project_dir, asset_registry, &asset_registry);
    };

    mosaic::foundation::log::info("mcp", "serving project {} ({} entities)", project_dir,
                                  world.entity_count());
    mcp::run_stdio_loop(std::cin, std::cout, dispatch);
    return 0;
}
} // namespace mosaic::app
