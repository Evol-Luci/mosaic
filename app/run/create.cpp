#include "app/run/create.hpp"

#include "app/project/project_io.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_create(const Options& options)
{
    if (!options.create_path)
    {
        mosaic::foundation::log::error("project", "create: no project name given");
        return 1;
    }
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    return create_project(*options.create_path, types) ? 0 : 1;
}
} // namespace mosaic::app
