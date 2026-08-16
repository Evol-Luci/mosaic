#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// Default cap on Value DOM nesting depth for from_toml. Bounds the recursive
// descent so adversarial deeply-nested input throws SerializationError instead
// of overflowing the stack (DoS / UB). The root table counts as one level.
inline constexpr int kDefaultTomlMaxDepth = 128;

// Value <-> TOML text. Used for human-authored config and asset metadata. Knows
// nothing of reflection; it only projects the format-neutral Value DOM. The
// top-level Value must be a Table (TOML documents are tables).
std::string to_toml(const Value& value);
Value from_toml(const std::string& text, int max_depth = kDefaultTomlMaxDepth);

} // namespace mosaic::foundation::serialization
