#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::studio
{

// One registered menu item, read out of a plugin's MOSAIC_MENU_ITEMS dict.
struct PluginMenuItem
{
    std::string plugin_name;   // filename stem, e.g. "hello" -- for error messages
    std::string label;         // e.g. "Say Hello"
    std::string function_name; // e.g. "on_say_hello"
};

// Discovers and runs project-local *.py plugins, giving each its own ScriptEngine bound
// to the current World/reflection/commands (the same globals gameplay ScriptComponents
// already get: get_component, set_component, entities_with, undo/redo, ...). A plugin
// registers menu items declaratively via a module-level MOSAIC_MENU_ITEMS dict mapping
// label -> zero-argument function name; PluginHost reads that dict back via
// ScriptEngine::get_global and dispatches clicks via ScriptEngine::call_function -- no
// pybind11/Python.h dependency here, or anywhere else in studio/.
class PluginHost
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit PluginHost(EngineFactory factory) : factory_(std::move(factory)) {}

    // Scans `directory` (non-recursive) for *.py files, running each through a fresh
    // engine bound to world/types/commands. Replaces any previously loaded plugins
    // (this is what "Reload Plugins" calls). Returns one human-readable error string
    // per plugin that failed to load (empty on full success) -- a broken plugin file
    // never prevents the others in the same directory from loading. A missing or
    // non-directory `directory` is not an error: returns an empty vector, zero menu
    // items (a project with no plugins/ folder at all is the common case).
    std::vector<std::string> load(const std::string& directory, mosaic::engine::World& world,
                                  const mosaic::engine::reflection::ComponentReflection& types,
                                  mosaic::foundation::commands::CommandStack& commands);

    // Every registered menu item across all successfully loaded plugins, in load order
    // then MOSAIC_MENU_ITEMS declaration order.
    const std::vector<PluginMenuItem>& menu_items() const { return menu_items_; }

    // Invokes item's function on its plugin's engine.
    mosaic::engine::script::ScriptResult invoke(const PluginMenuItem& item);

private:
    struct LoadedPlugin
    {
        std::string name;
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine;
    };

    EngineFactory               factory_;
    std::vector<LoadedPlugin>   plugins_;
    std::vector<PluginMenuItem> menu_items_;
};

} // namespace mosaic::studio
