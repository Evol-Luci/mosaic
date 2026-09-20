#include "app/run/create.hpp"

#include "app/project/project_io.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
int run_create(const Options& options)
{
    if (!options.create_path)
    {
        mosaic::foundation::log::error("project", "create: no project name given");
        return 1;
    }
    return create_project(*options.create_path, options.template_name.value_or("default")) ? 0 : 1;
}
} // namespace mosaic::app
