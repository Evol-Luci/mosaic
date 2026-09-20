#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::app::mcp
{
// Thrown for protocol-level misuse of a tool call: an unrecognized tool name, or a
// request missing/mistyping a required argument. NEVER thrown for an ordinary "not
// found" outcome (unknown entity id, unregistered component name) -- those come back
// inside a normal successful result (`{"ok": false}` / `{"value": null}`), exactly
// like the existing Python scripting globals (world_globals.cpp) already behave for
// the same cases. Caught at the protocol boundary (app/mcp/mcp_protocol.cpp) and
// turned into an MCP tool-error result -- never crashes the server process.
struct McpToolError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Constructs a fresh, unbound ScriptEngine for set_script's throwaway compile-check --
// the exact same factory shape mosaic editor injects into its own Script Editor
// (app/run/editor.cpp), just threaded through the MCP dispatch instead of Studio's UI.
using ScriptEngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 34 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + tick_until + build_project + script_profile +
// entities_where + broken_references + orphaned_assets + bulk_set_field +
// entities_referencing + plugin_list + plugin_install + plugin_remove +
// plugin_source_add/list/remove/pull).
nlohmann::json tools_list_schema();

// Dispatches one tool call by name. `scene_path` is the manifest's default_scene path,
// used only by `save_scene`. `script_factory` is used only by `set_script`.
// `project_dir` is used only by `import_asset`, `build_project`, `plugin_install`, and
// `plugin_remove`. `asset_registry` is used only by `broken_references` and
// `orphaned_assets` (all four unused by every other
// tool, threaded through uniformly -- the same "every closure takes everything it might
// need" shape world_globals.cpp uses for world_ptr/types_ptr/stack_ptr). All four default
// so every existing call site keeps compiling unchanged; the affected tool reports a
// graceful failure (not a crash) if the relevant one wasn't actually supplied -- for
// `broken_references`/`orphaned_assets`, a default-constructed empty `AssetRegistry`
// simply makes every non-nil AssetID look unresolved / every registered asset look
// orphaned. `live_asset_registry` is used only by `import_asset`: when non-null (and
// pointing at the SAME object `asset_registry` refers to, as `app/run/mcp.cpp`'s dispatch
// always arranges), a successful import re-registers the new asset into it immediately,
// via a fresh `load_project_assets` rescan -- so a same-session `broken_references`/
// `orphaned_assets` call afterward sees it right away, no `mosaic mcp` restart needed
// (`entities_referencing` never consulted `asset_registry` at all -- it was never affected
// by the staleness gap this fixes). Defaults to nullptr (no refresh attempted), matching
// every existing call site's behavior unchanged. Throws McpToolError for an unknown tool
// name or malformed arguments; otherwise never throws.
nlohmann::json handle_tool_call(mosaic::engine::World& world,
                                const mosaic::engine::reflection::ComponentReflection& types,
                                mosaic::foundation::commands::CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{},
                                const std::string& project_dir = std::string{},
                                const mosaic::engine::assets::AssetRegistry& asset_registry =
                                    mosaic::engine::assets::AssetRegistry{},
                                mosaic::engine::assets::AssetRegistry* live_asset_registry = nullptr);
} // namespace mosaic::app::mcp
