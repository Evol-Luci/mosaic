#include "app/run/docs.hpp"

#include <filesystem>
#include <fstream>

#include "app/docs/component_reference.hpp"
#include "app/docs/scripting_globals_reference.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_docs(const Options&)
{
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    const std::filesystem::path out_dir = std::filesystem::path(MOSAIC_DOCS_DIR) / "reference";
    std::filesystem::create_directories(out_dir);

    const std::string components_md = generate_component_reference_markdown(types);
    const std::filesystem::path components_file = out_dir / "components.md";
    std::ofstream components_out(components_file, std::ios::trunc);
    if (!components_out)
    {
        mosaic::foundation::log::error("docs", "failed to open {} for writing", components_file.string());
        return 1;
    }
    components_out << components_md;
    mosaic::foundation::log::info("docs", "wrote {}", components_file.string());

    const std::string scripting_md = generate_scripting_globals_reference_markdown();
    const std::filesystem::path scripting_file = out_dir / "scripting-globals.md";
    std::ofstream scripting_out(scripting_file, std::ios::trunc);
    if (!scripting_out)
    {
        mosaic::foundation::log::error("docs", "failed to open {} for writing", scripting_file.string());
        return 1;
    }
    scripting_out << scripting_md;
    mosaic::foundation::log::info("docs", "wrote {}", scripting_file.string());

    return 0;
}
} // namespace mosaic::app
