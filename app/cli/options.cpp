#include "app/cli/options.hpp"

#include <CLI/CLI.hpp>

namespace mosaic::app
{

ParseResult parse(int argc, char** argv)
{
    ParseResult result;

    CLI::App app{"Mosaic engine runner"};
    app.require_subcommand(1); // exactly one subcommand (only `run` for now)

    CLI::App* run = app.add_subcommand("run", "Run a world in real time");

    std::string  scene;
    double       duration_s = 0.0;
    std::uint64_t ticks = 0;
    run->add_option("--scene", scene, "Scene JSON file to load before running");
    CLI::Option* duration_opt =
        run->add_option("--duration", duration_s, "Stop after N seconds");
    CLI::Option* ticks_opt =
        run->add_option("--ticks", ticks, "Stop after N fixed ticks");
    run->add_flag("--json", result.options.json, "Emit machine-readable status");
    run->add_option("--fps", result.options.fps, "Target wake frame rate")
        ->capture_default_str();
    run->add_option("--fixed-hz", result.options.fixed_hz, "Simulation timestep rate")
        ->capture_default_str();

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        result.exit_code = app.exit(e); // prints help/error, yields exit code
        return result;
    }

    if (!scene.empty())
    {
        result.options.scene = scene;
    }
    if (*duration_opt)
    {
        result.options.duration_s = duration_s;
    }
    if (*ticks_opt)
    {
        result.options.ticks = ticks;
    }
    return result;
}

} // namespace mosaic::app
