#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace mosaic::app
{
// Reads a whole file to a string; std::nullopt if it cannot be opened. The single filesystem
// read seam for scenes (engine/scene stays in-memory-only).
inline std::optional<std::string> load_scene_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Writes text to a file; false on failure. The single filesystem write seam for scenes.
inline bool save_scene_file(const std::string& path, const std::string& contents)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << contents;
    return static_cast<bool>(file);
}
} // namespace mosaic::app
