#include <filesystem>
#include <optional>
#include <string>

#include <unistd.h> // ::getpid

#include <catch2/catch_test_macros.hpp>

#include "app/asset/asset_io.hpp"
#include "app/asset/font_cook.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/font_atlas.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::create_project;
using mosaic::app::import_asset;
using mosaic::app::load_project_assets;
using mosaic::app::load_scene_file;
using mosaic::app::save_scene_file;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::ui::FontAtlas;
using mosaic::foundation::reflection::TypeRegistry;

TEST_CASE("import_asset cooks a .ttf into a font asset, and load registers a FontAtlas", "[app]")
{
    namespace fs = std::filesystem;

    // Scaffold a project (create_project needs a ComponentReflection, like audio_asset_test).
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    const std::string root = (fs::temp_directory_path() /
        ("mosaic_font_" + std::to_string(::getpid()))).string();
    fs::remove_all(root);
    REQUIRE(create_project(root, types));

    // Write the fixture TTF as the import source (outside the project tree).
    const std::string src = root + "_glyphs.ttf";
    const auto ttf = load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(ttf.has_value());
    REQUIRE(save_scene_file(src, *ttf));

    const std::optional<mosaic::foundation::serialization::AssetID> id =
        import_asset(root, src);
    REQUIRE(id.has_value());

    // The cooked blob + metadata exist under assets/ (source stem is the fixture file name).
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(root) / "assets" / (stem + ".font")));
    REQUIRE(fs::exists(fs::path(root) / "assets" / (stem + ".toml")));

    // Load registers a resolvable FontAtlas under the import id (resolver is load<T>).
    AssetRegistry registry;
    const int n = load_project_assets(registry, (fs::path(root) / "assets").string());
    REQUIRE(n >= 1);
    REQUIRE(registry.contains(*id));
    const std::shared_ptr<FontAtlas> font = registry.load<FontAtlas>(*id);
    REQUIRE(font != nullptr);
    REQUIRE(font->glyphs.size() == 95);

    fs::remove_all(root);
    fs::remove(src);
}
