#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// Value <-> TOML text. Used for human-authored config and asset metadata. Knows
// nothing of reflection; it only projects the format-neutral Value DOM. The
// top-level Value must be a Table (TOML documents are tables).
std::string to_toml(const Value& value);
Value from_toml(const std::string& text);

} // namespace mosaic::foundation::serialization
