#pragma once

#include <cstdlib>
#include <filesystem>

namespace mosaic::app
{

// $XDG_CONFIG_HOME/mosaic if set (and non-empty), else $HOME/.config/mosaic. This is
// the first user-level (not project-level, not compile-time-baked) directory anywhere
// in this codebase -- see docs/design/user-config-directory.md for the rationale and
// forward-looking notes on what else might live here later.
inline std::filesystem::path user_config_dir()
{
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
    {
        return fs::path(xdg) / "mosaic";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home != nullptr ? home : "") / ".config" / "mosaic";
}

} // namespace mosaic::app
