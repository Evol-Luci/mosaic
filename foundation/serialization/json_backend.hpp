#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// Default cap on Value DOM nesting depth for from_json. Bounds the recursive
// descent so adversarial deeply-nested input throws SerializationError instead
// of overflowing the stack (DoS / UB).
inline constexpr int kDefaultJsonMaxDepth = 128;

// Value <-> JSON text. Used for scenes and other deeply-nested graphs. Knows
// nothing of reflection; it only projects the format-neutral Value DOM.
std::string to_json(const Value& value);
Value from_json(const std::string& text, int max_depth = kDefaultJsonMaxDepth);

} // namespace mosaic::foundation::serialization
