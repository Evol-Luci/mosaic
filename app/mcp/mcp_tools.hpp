#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
// for all 17 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene + set_script).
nlohmann::json tools_list_schema();

// Dispatches one tool call by name. `scene_path` is the manifest's default_scene path,
// used only by `save_scene`. `script_factory` is used only by `set_script` (both unused
// by every other tool, threaded through uniformly -- the same "every closure takes
// everything it might need" shape world_globals.cpp uses for
// world_ptr/types_ptr/stack_ptr). Defaulted to an empty factory so every existing call
// site keeps compiling unchanged; `set_script` reports a graceful failure (not a crash)
// if no factory was actually supplied at the call site. Throws McpToolError for an
// unknown tool name or malformed arguments; otherwise never throws.
nlohmann::json handle_tool_call(mosaic::engine::World& world,
                                const mosaic::engine::reflection::ComponentReflection& types,
                                mosaic::foundation::commands::CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{});
} // namespace mosaic::app::mcp
