#include "app/run/build.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "app/project/bundle.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_build(const Options& options)
{
    if (!options.build_project)
    {
        mosaic::foundation::log::error("build", "build: no project given");
        return 1;
    }
    const std::string project = *options.build_project;
    // Default output is <project>/dist.
    const std::string out =
        options.build_out.value_or((std::filesystem::path(project) / "dist").string());

    // Embed this running executable as the bundle's runtime.
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
    {
        mosaic::foundation::log::error("build", "could not resolve the running executable path");
        return 1;
    }

    // register_builtin_components fills `types` (registry is a required side-effect out-param).
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    return build_bundle(project, out, exe.string(), types) ? 0 : 1;
}
} // namespace mosaic::app
