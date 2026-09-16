#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace mosaic::app
{

// Which subcommand the user invoked.
enum class Command { Run, Editor, Create, Build, Import, Docs, PluginList, PluginInstall, PluginRemove,
                    PluginSourceAdd, PluginSourceList, PluginSourceRemove, PluginSourcePull };

// Parsed CLI arguments (for the `run` and `editor` subcommands). A plain value
// type: no CLI11 in this header.
struct Options
{
    Command                      command = Command::Run;
    std::optional<std::string>   scene;        // --scene <file>
    std::optional<std::string>   open_path;    // positional: project dir or scene file (run/editor)
    std::optional<std::uint16_t> host_port;      // --host <port>
    std::optional<std::string>   connect_address; // --connect <host>:<port>
    std::optional<std::string>   create_path;  // positional: project dir to create (create)
    std::optional<std::string>   template_name; // --template <name> (create)
    std::optional<std::string>   build_project; // positional: project dir to package (build)
    std::optional<std::string>   build_out;     // -o/--output: bundle output dir (build)
    std::optional<std::string>   import_project; // positional 1: project dir (import)
    std::optional<std::string>   import_file;    // positional 2: source asset file (import)
    std::optional<std::string>   plugin_name;    // positional: package name (plugin install/remove)
    std::optional<std::string>   plugin_project; // positional: project dir (plugin install/remove)
    std::optional<std::string>   package_source; // --source (plugin list/install)
    std::optional<std::string>   source_name;    // positional: source name (plugin source add/remove)
    std::optional<std::string>   source_path;    // positional: local dir (plugin source add)
    std::optional<std::string>   assets_dir;     // computed: <project>/assets to load at startup
    std::optional<double>        duration_s;   // --duration <seconds>
    std::optional<std::uint64_t> ticks;        // --ticks <n>
    bool                         json = false;   // --json
    bool                         window = false; // --window : open a render window (else headless)
    bool                         script_thread = false; // --script-thread
    bool                         script_profile = false; // --script-profile : dump on_update timing at exit
    double                       fps = 60.0;      // --fps  (pump wake target)
    double                       fixed_hz = 60.0; // --fixed-hz (sim timestep)
};

// Result of parse(): either a populated Options, or an exit_code to return from
// main (set when CLI11 handled --help or reported an arg error).
struct ParseResult
{
    Options              options;
    std::optional<int>   exit_code;
};

// Parse argv for the `mosaic run [flags]` command line using CLI11.
ParseResult parse(int argc, char** argv);

// Split a validated "<host>:<port>" address (as produced by --connect) into its parts.
// Only called on addresses that already passed the CLI validator, so the split always
// succeeds — this is not a general-purpose parser.
std::pair<std::string, std::uint16_t> split_host_port(const std::string& address);

} // namespace mosaic::app
