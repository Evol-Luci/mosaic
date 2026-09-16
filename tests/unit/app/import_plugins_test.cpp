#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/asset/import_plugins.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::ImportPluginHost;
using mosaic::app::PluginImporter;
using mosaic::script::python::create_python_script_engine;
using mosaic::script::python::create_restricted_python_script_engine;

namespace
{
namespace fs = std::filesystem;

// A fresh, uniquely-named temp directory for one test's plugin files.
fs::path make_plugin_dir(const char* tag)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
        (std::string("mosaic_import_plugin_test_") + tag + "_" + std::to_string(now));
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}
} // namespace

TEST_CASE("ImportPluginHost loads a plugin importer and invoke returns its string result",
         "[app]")
{
    const fs::path dir = make_plugin_dir("basic");
    write_file(dir / "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    with open(source_path) as f:\n"
        "        return f.read().upper()\n");
    const fs::path source = make_plugin_dir("basic_src") / "data.csv";
    write_file(source, "a,b,c\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.empty());
    REQUIRE(host.importers().size() == 1);
    REQUIRE(host.importers()[0].extension == ".csv");

    const auto* found = host.find_importer(".csv");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, source.string());
    REQUIRE(result.blob.has_value());
    REQUIRE(*result.blob == "A,B,C\n");

    fs::remove_all(dir);
    fs::remove_all(source.parent_path());
}

TEST_CASE("ImportPluginHost treats a non-table MOSAIC_IMPORTERS as a load error", "[app]")
{
    const fs::path dir = make_plugin_dir("badtable");
    write_file(dir / "bad.py", "MOSAIC_IMPORTERS = 5\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.size() == 1);
    REQUIRE(host.importers().empty());

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost treats a non-string function name as a load error", "[app]")
{
    const fs::path dir = make_plugin_dir("badvalue");
    write_file(dir / "bad.py", "MOSAIC_IMPORTERS = {\".x\": 5}\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.size() == 1);
    REQUIRE(host.importers().empty());

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost with the restricted factory rejects os but still allows reading its own source file",
         "[app]")
{
    // Regression coverage that the sandboxing restriction (Task 1-2 of
    // this plan) behaves correctly when driven through ImportPluginHost
    // specifically -- real plugin file discovery + invoke(), not just
    // run_string() directly (already covered at that lower level by
    // tests/unit/script/python_script_engine_test.cpp). asset_io.hpp's
    // production construction site uses this same restricted factory.
    const fs::path dir = make_plugin_dir("restricted_ok");
    write_file(dir / "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    with open(source_path) as f:\n"
        "        return f.read().upper()\n");
    const fs::path source = make_plugin_dir("restricted_ok_src") / "data.csv";
    write_file(source, "a,b,c\n");

    ImportPluginHost host(create_restricted_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".csv");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, source.string());
    REQUIRE(result.blob.has_value());
    REQUIRE(*result.blob == "A,B,C\n");

    fs::remove_all(dir);
    fs::remove_all(source.parent_path());
}

TEST_CASE("ImportPluginHost with the restricted factory rejects an os import", "[app]")
{
    const fs::path dir = make_plugin_dir("restricted_evil");
    write_file(dir / "evil.py",
        "MOSAIC_IMPORTERS = {\".evil\": \"do_evil\"}\n\n"
        "def do_evil(source_path):\n"
        "    import os\n"
        "    return os.listdir('.')\n");
    const fs::path source = make_plugin_dir("restricted_evil_src") / "data.evil";
    write_file(source, "x\n");

    ImportPluginHost host(create_restricted_python_script_engine);
    REQUIRE(host.load(dir.string()).empty()); // the file itself loads fine -- only the os usage fails
    const auto* found = host.find_importer(".evil");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, source.string());
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE_FALSE(result.error.empty());

    fs::remove_all(dir);
    fs::remove_all(source.parent_path());
}

TEST_CASE("ImportPluginHost invoke fails when the function raises", "[app]")
{
    const fs::path dir = make_plugin_dir("raises");
    write_file(dir / "boom.py",
        "MOSAIC_IMPORTERS = {\".boom\": \"import_boom\"}\n\n"
        "def import_boom(source_path):\n"
        "    raise ValueError(\"nope\")\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".boom");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, "/whatever");
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE_FALSE(result.error.empty());

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost invoke fails when the function returns a non-string", "[app]")
{
    const fs::path dir = make_plugin_dir("badreturn");
    write_file(dir / "num.py",
        "MOSAIC_IMPORTERS = {\".num\": \"import_num\"}\n\n"
        "def import_num(source_path):\n"
        "    return 42\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".num");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, "/whatever");
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE(result.error == "'import_num' must return a string");

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost::find_importer returns nullptr for an unknown extension", "[app]")
{
    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.find_importer(".nope") == nullptr);
}

TEST_CASE("ImportPluginHost.load a second time replaces the previous plugin set", "[app]")
{
    const fs::path dir = make_plugin_dir("reload");
    write_file(dir / "a.py", "MOSAIC_IMPORTERS = {\".a\": \"f\"}\ndef f(p):\n    return \"a\"\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    REQUIRE(host.importers().size() == 1);

    fs::remove(dir / "a.py");
    write_file(dir / "b.py", "MOSAIC_IMPORTERS = {\".b\": \"g\"}\ndef g(p):\n    return \"b\"\n");
    REQUIRE(host.load(dir.string()).empty());
    REQUIRE(host.importers().size() == 1);
    REQUIRE(host.importers()[0].extension == ".b");
    REQUIRE(host.find_importer(".a") == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost gives importer plugins no gameplay globals", "[app]")
{
    const fs::path dir = make_plugin_dir("noworld");
    write_file(dir / "touches_world.py",
        "MOSAIC_IMPORTERS = {\".w\": \"import_w\"}\n\n"
        "def import_w(source_path):\n"
        "    return str(get_component(1, \"Name\"))\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".w");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, "/whatever");
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE_FALSE(result.error.empty()); // NameError: get_component is not defined

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost.load on a missing directory returns no errors and no importers",
         "[app]")
{
    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load("/no/such/mosaic_import_plugins_dir").empty());
    REQUIRE(host.importers().empty());
}

TEST_CASE("ImportPluginHost treats an empty-string MOSAIC_IMPORTERS key as a load error",
         "[app]")
{
    // Regression test: an empty key would previously make find_importer("") match any
    // extensionless source file, and import_asset's ext.substr(1) on that empty string
    // threw std::out_of_range uncaught (a real crash reproduced against the CLI).
    const fs::path dir = make_plugin_dir("emptykey");
    write_file(dir / "bad.py", "MOSAIC_IMPORTERS = {\"\": \"f\"}\ndef f(p):\n    return \"x\"\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.size() == 1);
    REQUIRE(host.importers().empty());
    REQUIRE(host.find_importer("") == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost treats a key without a leading dot as a load error", "[app]")
{
    const fs::path dir = make_plugin_dir("nodotkey");
    write_file(dir / "bad.py", "MOSAIC_IMPORTERS = {\"csv\": \"f\"}\ndef f(p):\n    return \"x\"\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.size() == 1);
    REQUIRE(host.importers().empty());

    fs::remove_all(dir);
}
