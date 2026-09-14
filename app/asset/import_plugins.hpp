#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/script/script_engine.hpp"

namespace mosaic::app
{
// One registered importer, read out of a plugin's MOSAIC_IMPORTERS dict.
struct PluginImporter
{
    std::string plugin_name;   // filename stem, e.g. "csv_importer" -- for error messages
    std::string extension;     // e.g. ".csv" (leading dot, matches
                                // std::filesystem::path::extension())
    std::string function_name; // e.g. "import_csv"
};

// Discovers and runs project-local plugins/*.py files -- the same directory
// convention studio::PluginHost uses, but deliberately not built on top of it:
// mosaic_app does privately link mosaic_studio (run/editor.cpp launches the Studio
// GUI), but that dependency is scoped to the editor launcher only -- app/CMakeLists.txt
// keeps it (and ImGui) off every other app-tier header on purpose, and asset_io.hpp
// (this class's only caller) is included widely, including by code that must not pull
// in Studio/ImGui/pybind11-studio transitively. Unlike PluginHost, an ImportPluginHost's
// engines are never bound to a World/ComponentReflection/CommandStack -- an importer
// has no reason to touch entities or undo/redo, so get_component/set_component/etc.
// simply don't exist as globals here. A plugin registers importers declaratively via
// a module-level MOSAIC_IMPORTERS dict mapping a file extension (leading dot) to a
// one-argument function name; ImportPluginHost reads that dict back via
// ScriptEngine::get_global and invokes the function via set_global + run_string +
// get_global (passing the source path in, reading the returned string back out) --
// no pybind11/Python.h dependency here, or anywhere else in app/.
class ImportPluginHost
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ImportPluginHost(EngineFactory factory) : factory_(std::move(factory)) {}

    // Scans `directory` (non-recursive) for *.py files, running each through a fresh
    // engine. Replaces any previously loaded plugins (this is what a repeated call
    // does). Returns one human-readable error string per plugin that failed to load
    // (empty on full success) -- a broken plugin file never prevents the others in
    // the same directory from loading. A missing or non-directory `directory` is not
    // an error: returns an empty vector, zero importers.
    std::vector<std::string> load(const std::string& directory);

    // Every registered importer across all successfully loaded plugins, in load
    // order then MOSAIC_IMPORTERS declaration order.
    const std::vector<PluginImporter>& importers() const { return importers_; }

    // Finds a registered importer by extension (leading dot, e.g. ".csv"), or
    // nullptr if none matches. The first plugin (by load order) to register a given
    // extension wins if more than one does.
    const PluginImporter* find_importer(const std::string& extension) const;

    // Result of invoking a plugin's importer function.
    struct Result
    {
        std::optional<std::string> blob;  // the function's returned string, on success
        std::string                error; // set (and blob empty) on any failure
    };

    // Invokes item's function with source_path as its one argument, returning its
    // string return value. Fails (blob=nullopt, error set) if the plugin isn't
    // loaded, the function raises, or the function doesn't return a string.
    Result invoke(const PluginImporter& item, const std::string& source_path);

private:
    struct LoadedPlugin
    {
        std::string name;
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine;
    };

    EngineFactory                factory_;
    std::vector<LoadedPlugin>    plugins_;
    std::vector<PluginImporter>  importers_;
};

} // namespace mosaic::app
