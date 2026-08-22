#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mosaic::studio
{

struct Panel
{
    std::string name;
    bool        open;
};

// The editor shell's window state: which panels exist / are open, and whether the
// user asked to quit (File > Exit). Pure data + small mutators; no ImGui, no window.
class StudioLayout
{
public:
    StudioLayout();

    const std::vector<Panel>& panels() const { return panels_; }
    bool                      is_open(std::string_view name) const;
    void                      toggle(std::string_view name);

    bool exit_requested() const { return exit_; }
    void request_exit() { exit_ = true; }

private:
    std::vector<Panel> panels_;
    bool               exit_ = false;
};

} // namespace mosaic::studio
