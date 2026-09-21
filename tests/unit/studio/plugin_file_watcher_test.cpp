#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "studio/plugin_file_watcher.hpp"

using mosaic::studio::PluginFileWatcher;

namespace fs = std::filesystem;

namespace
{
fs::path make_dir(const std::string& tag)
{
    const fs::path dir =
        fs::temp_directory_path() / ("mosaic_pfw_test_" + tag + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

// last_write_time on some filesystems has ~1s resolution -- give a modification time
// to actually move forward rather than flaking under fast test runs.
void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(1100)); }
} // namespace

TEST_CASE("PluginFileWatcher.poll returns false on the first call (baseline only)", "[studio]")
{
    const fs::path dir = make_dir("first_call");
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir.string()));

    fs::remove_all(dir);
}

TEST_CASE("PluginFileWatcher.poll returns false when nothing has changed", "[studio]")
{
    const fs::path dir = make_dir("unchanged");
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir.string()));
    REQUIRE_FALSE(watcher.poll(dir.string()));
    REQUIRE_FALSE(watcher.poll(dir.string()));

    fs::remove_all(dir);
}

TEST_CASE("PluginFileWatcher.poll detects a modified file's mtime", "[studio]")
{
    const fs::path dir = make_dir("modified");
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir.string()));

    settle();
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n# edited\n");
    REQUIRE(watcher.poll(dir.string()));

    // The change was consumed -- the next poll with no further edits is quiet again.
    REQUIRE_FALSE(watcher.poll(dir.string()));

    fs::remove_all(dir);
}

TEST_CASE("PluginFileWatcher.poll detects an added file", "[studio]")
{
    const fs::path dir = make_dir("added");
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir.string()));

    write_file(dir / "goodbye.py", "MOSAIC_MENU_ITEMS = {}\n");
    REQUIRE(watcher.poll(dir.string()));

    fs::remove_all(dir);
}

TEST_CASE("PluginFileWatcher.poll detects a removed file", "[studio]")
{
    const fs::path dir = make_dir("removed");
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");
    write_file(dir / "goodbye.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir.string()));

    fs::remove(dir / "goodbye.py");
    REQUIRE(watcher.poll(dir.string()));

    fs::remove_all(dir);
}

TEST_CASE("PluginFileWatcher.poll ignores non-.py files", "[studio]")
{
    const fs::path dir = make_dir("ignore_non_py");
    write_file(dir / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir.string()));

    write_file(dir / "readme.txt", "not a plugin\n");
    REQUIRE_FALSE(watcher.poll(dir.string()));

    fs::remove_all(dir);
}

TEST_CASE("PluginFileWatcher.poll returns false for a missing or non-directory path", "[studio]")
{
    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll("/no/such/mosaic_pfw_path_xyz"));
    REQUIRE_FALSE(watcher.poll("/no/such/mosaic_pfw_path_xyz"));
}

TEST_CASE("PluginFileWatcher.poll resets its baseline when the tracked directory changes",
         "[studio]")
{
    const fs::path dir_a = make_dir("switch_a");
    const fs::path dir_b = make_dir("switch_b");
    write_file(dir_a / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");
    write_file(dir_b / "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginFileWatcher watcher;
    REQUIRE_FALSE(watcher.poll(dir_a.string()));
    // Switching tracked directories re-baselines rather than comparing dir_b's
    // contents against dir_a's stale snapshot.
    REQUIRE_FALSE(watcher.poll(dir_b.string()));
    REQUIRE_FALSE(watcher.poll(dir_b.string()));

    fs::remove_all(dir_a);
    fs::remove_all(dir_b);
}
