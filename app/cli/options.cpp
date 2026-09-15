#include "app/cli/options.hpp"

#include <stdexcept>

#include <CLI/CLI.hpp>

namespace mosaic::app
{

ParseResult parse(int argc, char** argv)
{
    ParseResult result;

    CLI::App app{"Mosaic engine runner"};
    app.require_subcommand(1); // exactly one subcommand (`run`, `editor`, `create`, `build`, or `import`)

    CLI::App* run = app.add_subcommand("run", "Run a world in real time");

    std::string  scene;
    std::string  open_path;
    std::string  create_name;
    std::string  build_project_name;
    std::string  build_out_dir;
    std::string  import_project_name;
    std::string  import_file_name;
    double       duration_s = 0.0;
    std::uint64_t ticks = 0;
    CLI::Option* run_scene = run->add_option("--scene", scene, "Scene JSON file to load before running");
    CLI::Option* run_path  = run->add_option("path", open_path, "Project directory or scene file to open");
    run_path->excludes(run_scene);
    std::uint16_t host_port = 0;
    std::string   connect_address;
    CLI::Option* host_opt = run->add_option(
        "--host", host_port, "Host a server on <port>");
    host_opt->check(CLI::Range(std::uint16_t{1}, std::uint16_t{65535}));
    CLI::Option* connect_opt = run->add_option(
        "--connect", connect_address, "Connect to <host>:<port> as a client");
    connect_opt->check(
        [](const std::string& val) -> std::string
        {
            const auto colon = val.rfind(':');
            if (colon == std::string::npos || colon + 1 == val.size())
            {
                return "expected <host>:<port>";
            }
            try
            {
                const int port = std::stoi(val.substr(colon + 1));
                if (port < 1 || port > 65535) { return "port out of range in <host>:<port>"; }
            }
            catch (const std::exception&)
            {
                return "invalid port in <host>:<port>";
            }
            return "";
        });
    host_opt->excludes(connect_opt);
    connect_opt->excludes(host_opt);
    CLI::Option* duration_opt =
        run->add_option("--duration", duration_s, "Stop after N seconds");
    CLI::Option* ticks_opt =
        run->add_option("--ticks", ticks, "Stop after N fixed ticks");
    run->add_flag("--json", result.options.json, "Emit machine-readable status");
    run->add_flag("--window", result.options.window, "Open a render window (Vulkan)");
    run->add_flag("--script-thread", result.options.script_thread, "Run scripts on a background thread (1-frame deferred)");
    run->add_flag("--script-profile", result.options.script_profile, "Print per-entity on_update timing at exit");
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
    std::string template_name;
    create->add_option("--template", template_name,
                       "Named template to scaffold from (default: \"default\")");

    CLI::App* build = app.add_subcommand("build", "Package a project into a standalone bundle");
    build->add_option("project", build_project_name, "Project directory to package")->required();
    build->add_option("-o,--output", build_out_dir, "Output bundle directory (default <project>/dist)");

    CLI::App* import = app.add_subcommand("import", "Import an asset file into a project");
    import->add_option("project", import_project_name, "Project directory")->required();
    import->add_option("file", import_file_name, "Source asset file (PPM image)")->required();

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
    if (import->parsed())
    {
        result.options.command = Command::Import;
    }
    if (!import_project_name.empty())
    {
        result.options.import_project = import_project_name;
    }
    if (!import_file_name.empty())
    {
        result.options.import_file = import_file_name;
    }
    if (!open_path.empty())
    {
        result.options.open_path = open_path;
    }
    if (!create_name.empty())
    {
        result.options.create_path = create_name;
    }
    if (!template_name.empty())
    {
        result.options.template_name = template_name;
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
    if (host_opt->count() > 0)
    {
        result.options.host_port = host_port;
    }
    if (connect_opt->count() > 0)
    {
        result.options.connect_address = connect_address;
    }
    return result;
}

std::pair<std::string, std::uint16_t> split_host_port(const std::string& address)
{
    const auto colon = address.rfind(':');
    const std::string host = address.substr(0, colon);
    const int port = std::stoi(address.substr(colon + 1));
    return {host, static_cast<std::uint16_t>(port)};
}

} // namespace mosaic::app
