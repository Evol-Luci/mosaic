#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "app/asset/image_cook.hpp"
#include "app/asset/audio_cook.hpp"
#include "app/project/project_io.hpp" // load_manifest
#include "app/run/scene_io.hpp"       // save_scene_file / load_scene_file
#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "engine/script/script_source.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"     // AssetID, SerializationError
#include "foundation/serialization/reflect_value.hpp" // to_value / from_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml / from_toml

namespace mosaic::app
{
// A fresh random 16-byte AssetID (no external UUID dependency).
inline mosaic::foundation::serialization::AssetID random_asset_id()
{
    std::random_device rd;
    std::array<std::uint8_t, 16> b{};
    for (auto& byte : b) { byte = static_cast<std::uint8_t>(rd() & 0xFF); }
    return mosaic::foundation::serialization::AssetID(b);
}

// Import an external PPM image into <project_dir>/assets/: cook it and write
// <assets>/<stem>.image + <assets>/<stem>.toml (AssetMetadata). Returns the new AssetID, or
// nullopt (and logs) on failure. Refuses (nullopt) rather than overwriting when an asset with
// the same stem already exists, so a re-import or a stem collision never orphans a prior
// AssetID. Never leaves a half-written asset (removes the blob if the metadata write fails).
inline std::optional<mosaic::foundation::serialization::AssetID>
import_asset(const std::string& project_dir, const std::string& source_file)
{
    namespace fs = std::filesystem;

    if (!load_manifest(project_dir))
    {
        mosaic::foundation::log::error("import", "not a Mosaic project: {}", project_dir);
        return std::nullopt;
    }
    const std::optional<std::string> src = load_scene_file(source_file);
    if (!src)
    {
        mosaic::foundation::log::error("import", "source not found: {}", source_file);
        return std::nullopt;
    }
    const std::string ext = fs::path(source_file).extension().string();
    std::string blob_bytes;
    std::string blob_ext;
    std::string asset_type;
    if (ext == ".py")
    {
        blob_bytes = *src;       // script blob is the source text verbatim
        blob_ext   = ".script";
        asset_type = "script";
    }
    else if (ext == ".wav")
    {
        const std::optional<mosaic::engine::audio::AudioClip> clip = read_wav(*src);
        if (!clip)
        {
            mosaic::foundation::log::error("import", "not a supported WAV (16-bit PCM or float32): {}", source_file);
            return std::nullopt;
        }
        blob_bytes = cook_audio(*clip);
        blob_ext   = ".audioclip";
        asset_type = "audio";
    }
    else
    {
        const std::optional<mosaic::engine::render::Image> img = read_ppm(*src);
        if (!img)
        {
            mosaic::foundation::log::error("import", "not a valid PPM (P6, maxval 255): {}", source_file);
            return std::nullopt;
        }
        blob_bytes = cook_image(*img);
        blob_ext   = ".image";
        asset_type = "texture";
    }

    const std::string stem = fs::path(source_file).stem().string();
    const fs::path assets = fs::path(project_dir) / "assets";
    std::error_code ec;
    fs::create_directories(assets, ec);
    if (ec) { mosaic::foundation::log::error("import", "could not create assets/ in {}", project_dir); return std::nullopt; }

    const fs::path blob_path = assets / (stem + blob_ext);
    const fs::path meta_path = assets / (stem + ".toml");
    if (fs::exists(blob_path) || fs::exists(meta_path))
    {
        mosaic::foundation::log::error(
            "import", "asset '{}' already exists in {} (would overwrite); rename the source or remove the existing asset",
            stem, assets.string());
        return std::nullopt;
    }
    if (!save_scene_file(blob_path.string(), blob_bytes))
    {
        mosaic::foundation::log::error("import", "could not write cooked asset: {}", blob_path.string());
        return std::nullopt;
    }

    const mosaic::foundation::serialization::AssetID id = random_asset_id();
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = id;
    meta.type = asset_type;
    meta.version = 1;
    meta.source = source_file;
    const std::string meta_toml = mosaic::foundation::serialization::to_toml(
        mosaic::foundation::serialization::to_value(meta));
    if (!save_scene_file(meta_path.string(), meta_toml))
    {
        std::error_code rmec; fs::remove(blob_path, rmec); // no half-written asset
        mosaic::foundation::log::error("import", "could not write asset metadata: {}", meta_path.string());
        return std::nullopt;
    }

    mosaic::foundation::log::info("import", "imported {} as {}", source_file, id.to_string());
    return id;
}

// Scan <assets_dir>/*.toml, register each cooked Image into `registry` under its metadata uuid.
// Missing/empty dir -> 0. A malformed metadata, or a missing/corrupt sibling .image, logs a
// warning and is skipped (one bad asset must not abort startup). Returns the count registered.
inline int load_project_assets(mosaic::engine::assets::AssetRegistry& registry,
                               const std::string& assets_dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(assets_dir, ec)) { return 0; }

    int count = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(assets_dir, ec))
    {
        if (entry.path().extension() != ".toml") { continue; }
        const std::optional<std::string> meta_text = load_scene_file(entry.path().string());
        if (!meta_text) { continue; }

        mosaic::engine::assets::AssetMetadata meta;
        try
        {
            mosaic::foundation::serialization::from_value(
                mosaic::foundation::serialization::from_toml(*meta_text), meta);
        }
        catch (const mosaic::foundation::serialization::SerializationError& e)
        {
            mosaic::foundation::log::warn("import", "skipping bad asset metadata {}: {}",
                                          entry.path().string(), e.what());
            continue;
        }

        if (meta.type == "script")
        {
            const fs::path script_path = entry.path().parent_path() / (entry.path().stem().string() + ".script");
            const std::optional<std::string> text = load_scene_file(script_path.string());
            if (!text) { mosaic::foundation::log::warn("import", "missing script blob {}", script_path.string()); continue; }
            registry.register_resource<mosaic::engine::script::ScriptSource>(
                meta, std::make_shared<mosaic::engine::script::ScriptSource>(
                          mosaic::engine::script::ScriptSource{*text}));
            ++count;
            continue;
        }

        if (meta.type == "audio")
        {
            const fs::path clip_path =
                entry.path().parent_path() / (entry.path().stem().string() + ".audioclip");
            const std::optional<std::string> blob = load_scene_file(clip_path.string());
            if (!blob) { mosaic::foundation::log::warn("import", "missing cooked audio {}", clip_path.string()); continue; }
            const std::optional<mosaic::engine::audio::AudioClip> clip = uncook_audio(*blob);
            if (!clip) { mosaic::foundation::log::warn("import", "corrupt cooked audio {}", clip_path.string()); continue; }
            registry.register_resource<mosaic::engine::audio::AudioClip>(
                meta, std::make_shared<mosaic::engine::audio::AudioClip>(*clip));
            ++count;
            continue;
        }

        const fs::path blob_path = entry.path().parent_path() / (entry.path().stem().string() + ".image");
        const std::optional<std::string> blob = load_scene_file(blob_path.string());
        if (!blob) { mosaic::foundation::log::warn("import", "missing cooked image {}", blob_path.string()); continue; }
        const std::optional<mosaic::engine::render::Image> img = uncook_image(*blob);
        if (!img) { mosaic::foundation::log::warn("import", "corrupt cooked image {}", blob_path.string()); continue; }

        registry.register_resource<mosaic::engine::render::Image>(
            meta, std::make_shared<mosaic::engine::render::Image>(*img));
        ++count;
    }
    return count;
}
} // namespace mosaic::app
