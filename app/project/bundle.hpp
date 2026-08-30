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
// A directory that already holds a mosaic.toml is a prior bundle / project — safe to regenerate.
inline bool looks_like_bundle(const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::exists(dir / kManifestName, ec);
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

    // 2. Overwrite rule: regenerate a prior bundle; refuse a non-bundle non-empty dir.
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
