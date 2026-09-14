#include "app/asset/import_plugins.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "foundation/serialization/value.hpp"

namespace mosaic::app
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

std::vector<std::string> ImportPluginHost::load(const std::string& directory)
{
    plugins_.clear();
    importers_.clear();
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

        const auto run = engine->run_string(read_file(entry.path()));
        if (!run.ok)
        {
            errors.push_back(name + ": " + run.error);
            continue;
        }

        bool item_error = false;
        std::vector<PluginImporter> plugin_importers;
        const auto importer_dict = engine->get_global("MOSAIC_IMPORTERS");
        if (importer_dict.has_value())
        {
            if (!importer_dict->is<ser::Table>())
            {
                errors.push_back(name + ": MOSAIC_IMPORTERS must be a dict");
                item_error = true;
            }
            else
            {
                for (const auto& [extension, value] : importer_dict->as<ser::Table>())
                {
                    if (!value.is<std::string>())
                    {
                        errors.push_back(name + ": MOSAIC_IMPORTERS['" + extension +
                                         "'] must be a string function name");
                        item_error = true;
                        break;
                    }
                    plugin_importers.push_back(PluginImporter{name, extension, value.as<std::string>()});
                }
            }
        }

        if (item_error) { continue; }

        plugins_.push_back(LoadedPlugin{name, std::move(engine)});
        for (auto& importer : plugin_importers) { importers_.push_back(std::move(importer)); }
    }

    return errors;
}

const PluginImporter* ImportPluginHost::find_importer(const std::string& extension) const
{
    for (const auto& importer : importers_)
    {
        if (importer.extension == extension) { return &importer; }
    }
    return nullptr;
}

ImportPluginHost::Result ImportPluginHost::invoke(const PluginImporter& item,
                                                   const std::string& source_path)
{
    for (auto& plugin : plugins_)
    {
        if (plugin.name != item.plugin_name) { continue; }

        plugin.engine->set_global("__mosaic_import_source__", ser::Value(source_path));
        const auto run = plugin.engine->run_string(
            "__mosaic_import_result__ = " + item.function_name + "(__mosaic_import_source__)\n");
        if (!run.ok) { return Result{std::nullopt, run.error}; }

        const auto result = plugin.engine->get_global("__mosaic_import_result__");
        if (!result.has_value() || !result->is<std::string>())
        {
            return Result{std::nullopt, "'" + item.function_name + "' must return a string"};
        }
        return Result{result->as<std::string>(), ""};
    }
    return Result{std::nullopt, "plugin '" + item.plugin_name + "' is not loaded"};
}

} // namespace mosaic::app
