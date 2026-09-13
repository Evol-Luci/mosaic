#pragma once

#include <string>

namespace mosaic::engine
{

// A human-readable, optional entity label. Reflected + registered as "Name" (authorable
// in scenes/inspector, script-mutable). Absent on most entities — Studio's Hierarchy panel
// falls back to "Entity <index>" when it isn't present.
struct Name
{
    std::string value;
};

template <class V>
void reflect(V& visitor, Name& n)
{
    visitor.member("value", n.value);
}

} // namespace mosaic::engine
