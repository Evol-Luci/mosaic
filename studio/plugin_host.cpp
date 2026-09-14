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
    panels_.clear();
    std::vector<std::string> errors;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) { return errors; }

    std::error_code it_ec;
    for (auto it = fs::directory_iterator(directory, ec); it != fs::directory_iterator();
        it.increment(it_ec))
    {
        if (it_ec) { break; }
        const auto& entry = *it;
        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec || entry.path().extension() != ".py") { continue; }

        const std::string name = entry.path().stem().string();
        if (!factory_)
        {
            errors.push_back(name + ": scripting is not available in this build");
            continue;
        }
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        if (!engine)
        {
            errors.push_back(name + ": failed to create a script engine");
            continue;
        }
        engine->bind_world(world, types);
        engine->bind_commands(commands);

        const auto run = engine->run_string(read_file(entry.path()));
        if (!run.ok)
        {
            errors.push_back(name + ": " + run.error);
            continue;
        }

        bool item_error = false;

        std::vector<PluginMenuItem> plugin_items;
        const auto menu_dict = engine->get_global("MOSAIC_MENU_ITEMS");
        if (menu_dict.has_value())
        {
            if (!menu_dict->is<ser::Table>())
            {
                errors.push_back(name + ": MOSAIC_MENU_ITEMS must be a dict");
                item_error = true;
            }
            else
            {
                for (const auto& [label, value] : menu_dict->as<ser::Table>())
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

        std::vector<PluginPanel> plugin_panels;
        if (!item_error)
        {
            const auto panel_dict = engine->get_global("MOSAIC_PANELS");
            if (panel_dict.has_value())
            {
                if (!panel_dict->is<ser::Table>())
                {
                    errors.push_back(name + ": MOSAIC_PANELS must be a dict");
                    item_error = true;
                }
                else
                {
                    for (const auto& [label, value] : panel_dict->as<ser::Table>())
                    {
                        if (!value.is<std::string>())
                        {
                            errors.push_back(name + ": MOSAIC_PANELS['" + label +
                                             "'] must be a string function name");
                            item_error = true;
                            break;
                        }
                        plugin_panels.push_back(PluginPanel{name, label, value.as<std::string>()});
                    }
                }
            }
        }

        if (item_error) { continue; }

        plugins_.push_back(LoadedPlugin{name, std::move(engine)});
        for (auto& item : plugin_items) { menu_items_.push_back(std::move(item)); }
        for (auto& panel : plugin_panels) { panels_.push_back(std::move(panel)); }
    }

    return errors;
}

mosaic::engine::script::ScriptResult PluginHost::call_by_name(const std::string& plugin_name,
                                                               const std::string& function_name)
{
    for (const auto& plugin : plugins_)
    {
        if (plugin.name == plugin_name)
        {
            return plugin.engine->call_function(function_name);
        }
    }
    mosaic::engine::script::ScriptResult result;
    result.error = "plugin '" + plugin_name + "' is not loaded";
    return result;
}

mosaic::engine::script::ScriptResult PluginHost::invoke(const PluginMenuItem& item)
{
    return call_by_name(item.plugin_name, item.function_name);
}

mosaic::engine::script::ScriptResult PluginHost::invoke_panel(const PluginPanel& item)
{
    return call_by_name(item.plugin_name, item.function_name);
}

const PluginPanel* PluginHost::find_panel(const std::string& name) const
{
    for (const auto& panel : panels_)
    {
        if (panel.label == name) { return &panel; }
    }
    return nullptr;
}
} // namespace mosaic::studio
