#include "app/run/import.hpp"

#include "app/asset/asset_io.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
int run_import(const Options& options)
{
    if (!options.import_project || !options.import_file)
    {
        mosaic::foundation::log::error("import", "usage: mosaic import <project> <file>");
        return 1;
    }
    return import_asset(*options.import_project, *options.import_file).has_value() ? 0 : 1;
}
} // namespace mosaic::app
