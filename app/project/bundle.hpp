#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "app/project/project.hpp"    // ProjectManifest, kManifestName
#include "app/project/project_io.hpp" // load_manifest
#include "app/run/scene_io.hpp"       // save_scene_file
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml

namespace mosaic::app
{
// A directory is treated as a prior (regenerable) bundle only when it contains the distinctive
// build outputs: the copied mosaic binary AND run.sh.  A plain project directory (which has only
// mosaic.toml) is intentionally NOT a bundle, so `-o <project>` is refused rather than deleted.
inline bool looks_like_bundle(const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::exists(dir / "mosaic", ec)
        && std::filesystem::exists(dir / "run.sh", ec);
}

// Package `project_dir` into a self-contained bundle at `out_dir`, copying `runtime_binary` (the
// mosaic executable) in. Returns false (and logs) on failure; rolls back a bundle-owned out_dir.
// `types` is threaded for a future re-serialize step; v0.1 copies the scene verbatim.
inline bool build_bundle(const std::string& project_dir, const std::string& out_dir,
                         const std::string& runtime_binary,
                         const mosaic::engine::reflection::ComponentReflection& /*types*/)
{
    namespace fs = std::filesystem;

    // 1. Validate the project.
    std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("build", "not a Mosaic project: {}", project_dir);
        return false;
    }

    // 2a. Refuse a self-build (output == project).
    {
        std::error_code eqec;
        if (std::filesystem::weakly_canonical(std::filesystem::path(out_dir), eqec) ==
            std::filesystem::weakly_canonical(std::filesystem::path(project_dir), eqec))
        {
            mosaic::foundation::log::error("build", "output directory is the project itself: {}", out_dir);
            return false;
        }
    }

    // 2b. Overwrite rule: regenerate a prior bundle; refuse a non-bundle non-empty dir.
    const fs::path out(out_dir);
    std::error_code ec;
    if (fs::exists(out, ec) && !fs::is_empty(out, ec))
    {
        if (looks_like_bundle(out))
        {
            fs::remove_all(out, ec);
            if (ec)
            {
                mosaic::foundation::log::error("build", "could not clear prior bundle: {}", out_dir);
                return false;
            }
        }
        else
        {
            mosaic::foundation::log::error(
                "build", "refusing to overwrite non-bundle directory: {}", out_dir);
            return false; // leave it untouched
        }
    }

    // On any failure after this point, roll back the bundle dir we own.
    auto fail = [&](const std::string& msg) -> bool {
        mosaic::foundation::log::error("build", "{}", msg);
        std::error_code rmec;
        fs::remove_all(out, rmec);
        return false;
    };

    // 3. Copy the default scene (creating its parent dirs under the bundle).
    const fs::path scene_dst = out / manifest->default_scene;
    fs::create_directories(scene_dst.parent_path(), ec);
    if (ec) { return fail("could not create bundle directories: " + out_dir); }
    fs::copy_file(fs::path(project_dir) / manifest->default_scene, scene_dst,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) { return fail("could not copy scene: " + manifest->default_scene); }

    // 4. Write the manifest.
    const std::string manifest_toml = mosaic::foundation::serialization::to_toml(
        mosaic::foundation::serialization::to_value(*manifest));
    if (!save_scene_file((out / kManifestName).string(), manifest_toml))
    {
        return fail("could not write bundle manifest: " + out_dir);
    }

    // 4.5 Copy the project's assets/ tree (imported/cooked assets) if present.
    const fs::path assets_src = fs::path(project_dir) / "assets";
    if (fs::exists(assets_src, ec))
    {
        fs::copy(assets_src, out / "assets",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { return fail("could not copy assets/: " + assets_src.string()); }
    }

    // 5. Copy the runtime binary + set the executable bit.
    fs::copy_file(runtime_binary, out / "mosaic", fs::copy_options::overwrite_existing, ec);
    if (ec) { return fail("could not copy runtime binary: " + runtime_binary); }
    fs::permissions(out / "mosaic",
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) { return fail("could not set executable bit on the runtime binary"); }

    // 6. Write run.sh + set the executable bit.
    const std::string run_sh =
        "#!/bin/sh\n"
        "exec \"$(dirname \"$0\")/mosaic\" run \"$(dirname \"$0\")\" --window \"$@\"\n";
    if (!save_scene_file((out / "run.sh").string(), run_sh))
    {
        return fail("could not write run.sh");
    }
    fs::permissions(out / "run.sh",
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) { return fail("could not set executable bit on run.sh"); }

    mosaic::foundation::log::info("build", "built bundle at {}", out_dir);
    return true;
}
} // namespace mosaic::app
