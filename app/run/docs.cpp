#include "app/run/docs.hpp"

#include <filesystem>
#include <fstream>

#include "app/docs/component_reference.hpp"
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

    const std::string markdown = generate_component_reference_markdown(types);

    const std::filesystem::path out_dir  = std::filesystem::path(MOSAIC_DOCS_DIR) / "reference";
    const std::filesystem::path out_file = out_dir / "components.md";
    std::filesystem::create_directories(out_dir);

    std::ofstream out(out_file, std::ios::trunc);
    if (!out)
    {
        mosaic::foundation::log::error("docs", "failed to open {} for writing", out_file.string());
        return 1;
    }
    out << markdown;
    mosaic::foundation::log::info("docs", "wrote {}", out_file.string());
    return 0;
}
} // namespace mosaic::app
