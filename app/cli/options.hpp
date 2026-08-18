#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mosaic::app
{

// Parsed `mosaic run` arguments. A plain value type: no CLI11 in this header.
struct Options
{
    std::optional<std::string>   scene;        // --scene <file>
    std::optional<double>        duration_s;   // --duration <seconds>
    std::optional<std::uint64_t> ticks;        // --ticks <n>
    bool                         json = false;   // --json
    bool                         window = false; // --window : open a render window (else headless)
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

} // namespace mosaic::app
