#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// Value <-> JSON text. Used for scenes and other deeply-nested graphs. Knows
// nothing of reflection; it only projects the format-neutral Value DOM.
std::string to_json(const Value& value);
Value from_json(const std::string& text);

} // namespace mosaic::foundation::serialization
