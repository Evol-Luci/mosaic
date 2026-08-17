#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::assets
{

using AssetID = mosaic::foundation::serialization::AssetID;
using Value = mosaic::foundation::serialization::Value;

// Normative asset metadata (RFC 0001 / data-model). Reflected, so it round-trips
// through the #10 serialization bridge in any backend (TOML for human-readable
// project metadata). `metadata` and `import_settings` are open-ended Value blobs.
struct AssetMetadata
{
    AssetID uuid;
    std::string type;
    std::uint32_t version = 0;
    std::string source;
    std::vector<AssetID> dependencies;
    Value metadata;
    Value import_settings;
};

template <class V>
void reflect(V& visitor, AssetMetadata& m)
{
    visitor.member("uuid", m.uuid);
    visitor.member("type", m.type);
    visitor.member("version", m.version);
    visitor.member("source", m.source);
    visitor.member("dependencies", m.dependencies);
    visitor.member("metadata", m.metadata);
    visitor.member("import_settings", m.import_settings);
}

} // namespace mosaic::engine::assets
