#include "studio/studio_layout.hpp"

namespace mosaic::studio
{

StudioLayout::StudioLayout()
    : panels_{{"Hierarchy", true}, {"Viewport", true}, {"Inspector", true}, {"Console", true}}
{
}

bool StudioLayout::is_open(std::string_view name) const
{
    for (const Panel& p : panels_)
    {
        if (p.name == name)
        {
            return p.open;
        }
    }
    return false;
}

void StudioLayout::toggle(std::string_view name)
{
    for (Panel& p : panels_)
    {
        if (p.name == name)
        {
            p.open = !p.open;
            return;
        }
    }
}

} // namespace mosaic::studio
