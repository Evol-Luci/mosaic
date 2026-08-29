#pragma once

#include <string>
#include <utility>

namespace mosaic::studio
{
// The editor's current-scene-file state: the path Save writes to, an unsaved-edits flag (a
// convenience indicator), and a one-shot save request set by the File menu and consumed by the
// editor loop. Pure data — no ImGui, no filesystem. Mirrors PlayState.
class SceneFileModel
{
public:
    explicit SceneFileModel(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }

    bool dirty() const { return dirty_; }
    void mark_dirty() { dirty_ = true; }
    void mark_clean() { dirty_ = false; }

    void request_save() { save_req_ = true; }
    bool consume_save_request()
    {
        const bool requested = save_req_;
        save_req_ = false;
        return requested;
    }

private:
    std::string path_;
    bool        dirty_    = false;
    bool        save_req_ = false;
};
} // namespace mosaic::studio
