#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::foundation::serialization::from_json;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_json;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;

namespace
{

struct Flags
{
    std::uint32_t color = 0;
    std::uint64_t mask  = 0;
    std::int32_t  signed_val = 0;
    friend bool operator==(const Flags&, const Flags&) = default;
};

template <class V>
void reflect(V& v, Flags& f)
{
    v.member("color",      f.color);
    v.member("mask",       f.mask);
    v.member("signed_val", f.signed_val);
}

// A Flags instance with non-trivial unsigned values.
// mask=42 stays within int64 range, exercising the tolerant uint64<-int64 path
// after TOML/JSON canonicalize it back to int64.
Flags sample() { return {0xFF8800u, 42u, -7}; }

} // namespace

TEST_CASE("Unsigned fields round-trip through Value directly", "[uint]")
{
    Flags original = sample();
    Flags decoded;
    from_value(to_value(original), decoded);
    REQUIRE(decoded == original);
}

TEST_CASE("Unsigned fields round-trip through TOML text", "[uint]")
{
    Flags original = sample();
    Flags decoded;
    from_value(from_toml(to_toml(to_value(original))), decoded);
    REQUIRE(decoded == original);
}

TEST_CASE("Unsigned fields round-trip through JSON text", "[uint]")
{
    Flags original = sample();
    Flags decoded;
    from_value(from_json(to_json(to_value(original))), decoded);
    REQUIRE(decoded == original);
}
