#pragma once

#include <string>

namespace mosaic::engine::script
{
// A script asset's payload: the Python source text. Registered in the
// AssetRegistry under a script AssetID; ScriptSystem resolves + runs it.
struct ScriptSource
{
    std::string text;
};
} // namespace mosaic::engine::script
