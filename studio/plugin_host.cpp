#include "studio/plugin_host.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "foundation/serialization/value.hpp"

namespace mosaic::studio
{
namespace
{
namespace fs = std::filesystem;
namespace ser = mosaic::foundation::serialization;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

std::vector<std::string> PluginHost::load(const std::string& directory,
                                          mosaic::engine::World& world,
                                          const mosaic::engine::reflection::ComponentReflection& types,
                                          mosaic::foundation::commands::CommandStack& commands)
{
    plugins_.clear();
    menu_items_.clear();
    std::vector<std::string> errors;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) { return errors; }

    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".py") { continue; }

        const std::string name = entry.path().stem().string();
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        engine->bind_world(world, types);
        engine->bind_commands(commands);

        const auto run = engine->run_string(read_file(entry.path()));
        if (!run.ok)
        {
            errors.push_back(name + ": " + run.error);
            continue;
        }

        const auto items = engine->get_global("MOSAIC_MENU_ITEMS");
        std::vector<PluginMenuItem> plugin_items;
        bool item_error = false;
        if (items.has_value())
        {
            if (!items->is<ser::Table>())
            {
                errors.push_back(name + ": MOSAIC_MENU_ITEMS must be a dict");
                item_error = true;
            }
            else
            {
                for (const auto& [label, value] : items->as<ser::Table>())
                {
                    if (!value.is<std::string>())
                    {
                        errors.push_back(name + ": MOSAIC_MENU_ITEMS['" + label +
                                         "'] must be a string function name");
                        item_error = true;
                        break;
                    }
                    plugin_items.push_back(PluginMenuItem{name, label, value.as<std::string>()});
                }
            }
        }
        if (item_error) { continue; }

        plugins_.push_back(LoadedPlugin{name, std::move(engine)});
        for (auto& item : plugin_items) { menu_items_.push_back(std::move(item)); }
    }

    return errors;
}

mosaic::engine::script::ScriptResult PluginHost::invoke(const PluginMenuItem& item)
{
    for (const auto& plugin : plugins_)
    {
        if (plugin.name == item.plugin_name)
        {
            return plugin.engine->call_function(item.function_name);
        }
    }
    mosaic::engine::script::ScriptResult result;
    result.error = "plugin '" + item.plugin_name + "' is not loaded";
    return result;
}
} // namespace mosaic::studio
