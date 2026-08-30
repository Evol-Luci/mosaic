#include "app/cli/options.hpp"

#include <CLI/CLI.hpp>

namespace mosaic::app
{

ParseResult parse(int argc, char** argv)
{
    ParseResult result;

    CLI::App app{"Mosaic engine runner"};
    app.require_subcommand(1); // exactly one subcommand (`run`, `editor`, `create`, or `build`)

    CLI::App* run = app.add_subcommand("run", "Run a world in real time");

    std::string  scene;
    std::string  open_path;
    std::string  create_name;
    std::string  build_project_name;
    std::string  build_out_dir;
    double       duration_s = 0.0;
    std::uint64_t ticks = 0;
    CLI::Option* run_scene = run->add_option("--scene", scene, "Scene JSON file to load before running");
    CLI::Option* run_path  = run->add_option("path", open_path, "Project directory or scene file to open");
    run_path->excludes(run_scene);
    CLI::Option* duration_opt =
        run->add_option("--duration", duration_s, "Stop after N seconds");
    CLI::Option* ticks_opt =
        run->add_option("--ticks", ticks, "Stop after N fixed ticks");
    run->add_flag("--json", result.options.json, "Emit machine-readable status");
    run->add_flag("--window", result.options.window, "Open a render window (Vulkan)");
    run->add_option("--fps", result.options.fps, "Target wake frame rate")
        ->capture_default_str()
        ->check(CLI::PositiveNumber);
    run->add_option("--fixed-hz", result.options.fixed_hz, "Simulation timestep rate")
        ->capture_default_str()
        ->check(CLI::PositiveNumber);

    CLI::App* editor = app.add_subcommand("editor", "Open the Studio editor window");
    editor->add_option("--duration", duration_s, "Stop after N seconds");
    editor->add_option("--ticks", ticks, "Stop after N fixed ticks");
    CLI::Option* editor_scene = editor->add_option("--scene", scene, "Scene JSON file to load before editing");
    CLI::Option* editor_path  = editor->add_option("path", open_path, "Project directory or scene file to open");
    editor_path->excludes(editor_scene);

    CLI::App* create = app.add_subcommand("create", "Scaffold a new project directory");
    create->add_option("name", create_name, "Project directory to create")->required();

    CLI::App* build = app.add_subcommand("build", "Package a project into a standalone bundle");
    build->add_option("project", build_project_name, "Project directory to package")->required();
    build->add_option("-o,--output", build_out_dir, "Output bundle directory (default <project>/dist)");

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        result.exit_code = app.exit(e); // prints help/error, yields exit code
        return result;
    }

    if (editor->parsed())
    {
        result.options.command = Command::Editor;
    }
    if (create->parsed())
    {
        result.options.command = Command::Create;
    }
    if (build->parsed())
    {
        result.options.command = Command::Build;
    }
    if (!build_project_name.empty())
    {
        result.options.build_project = build_project_name;
    }
    if (!build_out_dir.empty())
    {
        result.options.build_out = build_out_dir;
    }
    if (!open_path.empty())
    {
        result.options.open_path = open_path;
    }
    if (!create_name.empty())
    {
        result.options.create_path = create_name;
    }

    if (!scene.empty())
    {
        result.options.scene = scene;
    }
    if (run->parsed() ? static_cast<bool>(*duration_opt) : editor->count("--duration") > 0)
    {
        result.options.duration_s = duration_s;
    }
    if (run->parsed() ? static_cast<bool>(*ticks_opt) : editor->count("--ticks") > 0)
    {
        result.options.ticks = ticks;
    }
    return result;
}

} // namespace mosaic::app
