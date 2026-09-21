#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>

namespace mosaic::studio
{

// Detects when a project's plugins/ directory has changed on disk since the last
// poll() -- the mechanism behind hot-reload, letting StudioApp call the existing
// (previously manual-only) "Reload Plugins" logic automatically instead of requiring
// the user to click the menu item every time they edit a *.py file. Deliberately
// mtime-polling rather than a real filesystem-notification API (inotify/kqueue/
// ReadDirectoryChangesW): plugins/ directories are tiny (a handful of files), a
// non-recursive `directory_iterator` + `last_write_time` scan per poll() call is cheap
// enough to run once per editor frame, and it keeps this file header-only/stdlib-only
// like the rest of studio/'s plugin machinery -- no new third-party dependency or
// per-platform backend for a feature this small.
class PluginFileWatcher
{
public:
    // Scans `directory` (non-recursive, *.py only -- mirrors PluginHost::load's own
    // scan) and compares each file's last-write-time and the overall file set against
    // the previous poll() call. Returns true if anything changed (a file added,
    // removed, or modified) -- false on the very first poll() for a given directory
    // (there is nothing to compare against yet; the caller's own initial load already
    // covers that case) and false for a missing or non-directory `directory` (not an
    // error, matching PluginHost::load's own "no plugins/ folder yet" convention).
    // Calling poll() with a *different* directory resets the baseline for the new
    // path; the watcher only ever tracks one directory at a time, matching
    // PluginHost's own single-directory design.
    bool poll(const std::string& directory)
    {
        namespace fs = std::filesystem;

        if (directory != tracked_directory_)
        {
            tracked_directory_ = directory;
            has_baseline_      = false;
            snapshot_.clear();
        }

        std::unordered_map<std::string, fs::file_time_type> current;
        std::error_code                                     ec;
        if (fs::is_directory(directory, ec))
        {
            for (auto it = fs::directory_iterator(directory, ec);
                !ec && it != fs::directory_iterator(); it.increment(ec))
            {
                const auto& entry = *it;
                std::error_code file_ec;
                if (!entry.is_regular_file(file_ec) || file_ec ||
                    entry.path().extension() != ".py")
                {
                    continue;
                }
                std::error_code mtime_ec;
                const fs::file_time_type mtime = fs::last_write_time(entry.path(), mtime_ec);
                current.emplace(entry.path().filename().string(), mtime_ec ? fs::file_time_type{} : mtime);
            }
        }

        const bool first  = !has_baseline_;
        const bool changed = has_baseline_ && current != snapshot_;
        snapshot_          = std::move(current);
        has_baseline_      = true;
        return !first && changed;
    }

private:
    std::string                                             tracked_directory_;
    std::unordered_map<std::string, std::filesystem::file_time_type> snapshot_;
    bool                                                     has_baseline_ = false;
};

} // namespace mosaic::studio
