#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_json;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_json;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::Scene;
using mosaic_samples::SceneEntity;
using mosaic_samples::Transform;
using mosaic_samples::Vec3;

namespace
{
// Builds a scene of two entities, each carrying a Transform component encoded
// into the components table (type name -> nested component Value).
Scene sample_scene()
{
    Scene scene;

    SceneEntity a;
    a.id = EntityID{1, 1};
    Table a_components;
    Transform tr;
    tr.position = Vec3{1.0, 2.0, 3.0};
    tr.scale = Vec3{1.0, 1.0, 1.0};
    a_components["Transform"] = to_value(tr);
    a.components = Value{std::move(a_components)};

    SceneEntity b;
    b.id = EntityID{2, 1};
    Table b_components;
    b_components["Name"] = to_value(mosaic_samples::Name{"player"});
    b.components = Value{std::move(b_components)};

    scene.entities = {a, b};
    return scene;
}
} // namespace

TEST_CASE("Value round-trips through JSON text", "[json]")
{
    Table t;
    t["n"] = Value{std::int64_t{-3}};
    t["f"] = Value{2.5};
    t["s"] = Value{std::string{"hi"}};
    Value v{std::move(t)};

    Value back = from_json(to_json(v));
    REQUIRE(back == v);
}

TEST_CASE("scene JSON round-trip is identity on the schema", "[json]")
{
    Scene scene = sample_scene();
    Value v = to_value(scene);

    std::string json = to_json(v);
    Value parsed = from_json(json);

    Scene out;
    from_value(parsed, out);
    REQUIRE(out == scene);
}

TEST_CASE("from_json throws SerializationError on malformed input", "[json]")
{
    REQUIRE_THROWS_AS(from_json("{ not valid json "),
                      mosaic::foundation::serialization::SerializationError);
}
