#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_install.hpp"
#include "app/project/project_io.hpp"
#include "engine/reflection/component_reflection.hpp"

using mosaic::app::available_plugin_updates;
using mosaic::app::InstalledPlugin;
using mosaic::app::install_package;
using mosaic::app::load_manifest;
using mosaic::app::PackageInfo;
using mosaic::app::PluginUpdate;
using mosaic::app::remove_package;
using mosaic::app::set_plugin_enabled;

namespace
{
std::filesystem::path make_fixture_project(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("mosaic_pkginstall_test_" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::ofstream manifest(root / "mosaic.toml", std::ios::binary | std::ios::trunc);
    manifest << "name = \"testproj\"\ndefault_scene = \"scenes/main.json\"\n";
    return root;
}

std::filesystem::path make_fixture_package(const std::string& tag, const std::string& name,
                                           const std::string& version)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("mosaic_pkg_test_" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::ofstream py(root / "widget.py", std::ios::binary | std::ios::trunc);
    py << "MOSAIC_MENU_ITEMS = {}\n";
    return root;
}
} // namespace

TEST_CASE("install_package copies files and records the manifest entry", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_basic");
    const std::filesystem::path pkg_dir = make_fixture_package("install_basic", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE(install_package(project.string(), pkg));

    REQUIRE(std::filesystem::exists(project / "plugins" / "widget.py"));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->installed_plugins.size() == 1);
    REQUIRE(manifest->installed_plugins[0].name == "widget");
    REQUIRE(manifest->installed_plugins[0].version == "1.0.0");
    REQUIRE(manifest->installed_plugins[0].files == std::vector<std::string>{"widget.py"});
}

TEST_CASE("install_package refuses to overwrite an untracked file", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_conflict");
    std::filesystem::create_directories(project / "plugins");
    std::ofstream hand_written(project / "plugins" / "widget.py", std::ios::binary | std::ios::trunc);
    hand_written << "# hand-written, not from any package\n";
    hand_written.close();

    const std::filesystem::path pkg_dir = make_fixture_package("install_conflict", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE_FALSE(install_package(project.string(), pkg));

    // The hand-written file must survive untouched.
    std::ifstream check(project / "plugins" / "widget.py");
    std::string first_line;
    std::getline(check, first_line);
    REQUIRE(first_line == "# hand-written, not from any package");

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->installed_plugins.empty());
}

TEST_CASE("installing the same package at the same version is a no-op success", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_idempotent");
    const std::filesystem::path pkg_dir = make_fixture_package("install_idempotent", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE(install_package(project.string(), pkg));
    REQUIRE(install_package(project.string(), pkg)); // second call, same version

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins.size() == 1); // not duplicated
}

TEST_CASE("installing a newer version overwrites the package's own files and updates the record", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_upgrade");
    const std::filesystem::path pkg_dir = make_fixture_package("install_upgrade", "widget", "1.0.0");
    const PackageInfo v1{"widget", "1.0.0", "A widget", pkg_dir};
    REQUIRE(install_package(project.string(), v1));

    const PackageInfo v2{"widget", "2.0.0", "A widget", pkg_dir};
    REQUIRE(install_package(project.string(), v2));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins.size() == 1);
    REQUIRE(manifest->installed_plugins[0].version == "2.0.0");
}

TEST_CASE("install_package fails on a non-project directory", "[app]")
{
    namespace fs = std::filesystem;
    const fs::path not_a_project = fs::temp_directory_path() / "mosaic_pkginstall_test_not_a_project";
    std::error_code ec;
    fs::remove_all(not_a_project, ec);
    fs::create_directories(not_a_project, ec); // exists, but has no mosaic.toml

    const std::filesystem::path pkg_dir = make_fixture_package("not_a_project", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE_FALSE(install_package(not_a_project.string(), pkg));
}

TEST_CASE("remove_package deletes owned files and the manifest entry, leaving unrelated files alone", "[app]")
{
    const std::filesystem::path project = make_fixture_project("remove_basic");
    const std::filesystem::path pkg_dir = make_fixture_package("remove_basic", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};
    REQUIRE(install_package(project.string(), pkg));

    std::ofstream unrelated(project / "plugins" / "unrelated.py", std::ios::binary | std::ios::trunc);
    unrelated << "# not part of any package\n";
    unrelated.close();

    REQUIRE(remove_package(project.string(), "widget"));

    REQUIRE_FALSE(std::filesystem::exists(project / "plugins" / "widget.py"));
    REQUIRE(std::filesystem::exists(project / "plugins" / "unrelated.py"));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins.empty());
}

TEST_CASE("remove_package on a package that was never installed succeeds as a no-op", "[app]")
{
    const std::filesystem::path project = make_fixture_project("remove_noop");
    REQUIRE(remove_package(project.string(), "never-installed"));
}

TEST_CASE("remove_package ignores a hand-edited manifest entry that tries to escape plugins/", "[app]")
{
    // mosaic.toml is user-editable -- install_package never records anything but a
    // plain filename, but remove_package must not trust `files` blindly. Simulate a
    // maliciously/accidentally hand-edited manifest naming a path outside plugins/.
    const std::filesystem::path project = make_fixture_project("remove_escape");
    std::ofstream victim(project.parent_path() / "mosaic_pkginstall_test_remove_escape_victim.txt",
                         std::ios::binary | std::ios::trunc);
    victim << "do not delete me\n";
    victim.close();

    std::ofstream manifest(project / "mosaic.toml", std::ios::binary | std::ios::trunc);
    manifest << "name = \"testproj\"\n"
                "default_scene = \"scenes/main.json\"\n"
                "[[installed_plugins]]\n"
                "name = \"evil\"\n"
                "version = \"1.0.0\"\n"
                "files = [\"../mosaic_pkginstall_test_remove_escape_victim.txt\"]\n";
    manifest.close();

    REQUIRE(remove_package(project.string(), "evil"));

    REQUIRE(std::filesystem::exists(
        project.parent_path() / "mosaic_pkginstall_test_remove_escape_victim.txt"));

    const auto after = load_manifest(project.string());
    REQUIRE(after->installed_plugins.empty()); // the manifest entry is still removed
}

TEST_CASE("a freshly installed plugin is enabled by default", "[app]")
{
    const std::filesystem::path project = make_fixture_project("enabled_default");
    const std::filesystem::path pkg_dir = make_fixture_package("enabled_default", "widget", "1.0.0");
    REQUIRE(install_package(project.string(), PackageInfo{"widget", "1.0.0", "A widget", pkg_dir}));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins[0].enabled);
}

TEST_CASE("set_plugin_enabled toggles the flag and persists it", "[app]")
{
    const std::filesystem::path project = make_fixture_project("enable_toggle");
    const std::filesystem::path pkg_dir = make_fixture_package("enable_toggle", "widget", "1.0.0");
    REQUIRE(install_package(project.string(), PackageInfo{"widget", "1.0.0", "A widget", pkg_dir}));

    REQUIRE(set_plugin_enabled(project.string(), "widget", false));
    REQUIRE_FALSE(load_manifest(project.string())->installed_plugins[0].enabled);

    REQUIRE(set_plugin_enabled(project.string(), "widget", true));
    REQUIRE(load_manifest(project.string())->installed_plugins[0].enabled);
}

TEST_CASE("set_plugin_enabled fails for a package that isn't installed", "[app]")
{
    const std::filesystem::path project = make_fixture_project("enable_missing");
    REQUIRE_FALSE(set_plugin_enabled(project.string(), "never-installed", false));
}

TEST_CASE("set_plugin_enabled fails on a non-project directory", "[app]")
{
    namespace fs = std::filesystem;
    const fs::path not_a_project = fs::temp_directory_path() / "mosaic_pkginstall_test_enable_notproj";
    std::error_code ec;
    fs::remove_all(not_a_project, ec);
    fs::create_directories(not_a_project, ec);
    REQUIRE_FALSE(set_plugin_enabled(not_a_project.string(), "widget", false));
}

TEST_CASE("available_plugin_updates reports installed packages whose source version moved", "[app]")
{
    namespace fs = std::filesystem;
    const fs::path project = make_fixture_project("updates_basic");
    const fs::path source  = fs::temp_directory_path() / "mosaic_pkginstall_test_updates_basic_source";
    fs::remove_all(source);
    fs::create_directories(source / "widget");
    {
        std::ofstream toml(source / "widget" / "package.toml", std::ios::binary | std::ios::trunc);
        toml << "name = \"widget\"\nversion = \"1.0.0\"\ndescription = \"A widget\"\n";
        std::ofstream py(source / "widget" / "widget.py", std::ios::binary | std::ios::trunc);
        py << "MOSAIC_MENU_ITEMS = {}\n";
    }
    REQUIRE(install_package(project.string(), PackageInfo{"widget", "1.0.0", "A widget", source / "widget"}));

    // Nothing changed yet -- installed version matches the source's.
    REQUIRE(available_plugin_updates(project.string(), source.string()).empty());

    // Bump the source's own version without reinstalling.
    {
        std::ofstream toml(source / "widget" / "package.toml", std::ios::binary | std::ios::trunc);
        toml << "name = \"widget\"\nversion = \"1.1.0\"\ndescription = \"A widget\"\n";
    }

    const std::vector<PluginUpdate> updates = available_plugin_updates(project.string(), source.string());
    REQUIRE(updates.size() == 1);
    REQUIRE(updates[0].name == "widget");
    REQUIRE(updates[0].installed_version == "1.0.0");
    REQUIRE(updates[0].available_version == "1.1.0");
}

TEST_CASE("available_plugin_updates ignores an installed package no longer in the source", "[app]")
{
    namespace fs = std::filesystem;
    const fs::path project = make_fixture_project("updates_gone");
    const fs::path pkg_dir = make_fixture_package("updates_gone", "widget", "1.0.0");
    REQUIRE(install_package(project.string(), PackageInfo{"widget", "1.0.0", "A widget", pkg_dir}));

    const fs::path empty_source =
        fs::temp_directory_path() / "mosaic_pkginstall_test_updates_gone_source";
    fs::remove_all(empty_source);
    fs::create_directories(empty_source);

    REQUIRE(available_plugin_updates(project.string(), empty_source.string()).empty());
}
