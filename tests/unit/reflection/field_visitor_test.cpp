#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/field_visitor.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::engine::Name;
using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::FieldVisitorAdapter;
using mosaic::engine::transform::Transform;

namespace
{
struct RecordingVisitor : FieldVisitor
{
    std::vector<std::string> events;
    void field_float(std::string_view n, float&) override { events.push_back("float:" + std::string(n)); }
    void field_readonly(std::string_view n, const std::string& d) override
    {
        events.push_back("ro:" + std::string(n) + "=" + d);
    }
    void begin_group(std::string_view n) override { events.push_back("begin:" + std::string(n)); }
    void end_group() override { events.push_back("end"); }
};

struct WritingVisitor : FieldVisitor
{
    float value;
    explicit WritingVisitor(float v) : value(v) {}
    void field_float(std::string_view, float& f) override { f = value; }
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

// A FieldVisitor that does NOT override field_string, to prove the default
// implementation falls back to field_readonly (existing visitors keep compiling
// and behaving as before, matching field_asset's own default-fallback pattern).
struct NoStringOverrideVisitor : FieldVisitor
{
    std::vector<std::string> events;
    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view n, const std::string& d) override
    {
        events.push_back("ro:" + std::string(n) + "=" + d);
    }
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

// A FieldVisitor that overrides field_string and writes a new value into the
// live field, the way ImGuiFieldVisitor's real InputText-backed override does.
struct StringWritingVisitor : FieldVisitor
{
    std::string new_value;
    explicit StringWritingVisitor(std::string v) : new_value(std::move(v)) {}
    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void field_string(std::string_view, std::string& value) override { value = new_value; }
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

// A FieldVisitor that overrides field_entity, recording the name/value it saw.
struct EntityCapturingVisitor : FieldVisitor
{
    std::string seen_name;
    mosaic::foundation::EntityID seen_value;
    bool saw_entity = false;

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
    void field_entity(std::string_view name, mosaic::foundation::EntityID value) override
    {
        saw_entity  = true;
        seen_name   = std::string(name);
        seen_value  = value;
    }
};
} // namespace

TEST_CASE("FieldVisitorAdapter visits a Transform as groups, floats, and a readonly parent", "[reflection]")
{
    Transform t;
    RecordingVisitor rec;
    FieldVisitorAdapter adapter{rec};
    reflect(adapter, t);

    REQUIRE(rec.events.front() == "begin:position");

    int floats = 0;
    int groups = 0;
    bool saw_parent_ro = false;
    for (const std::string& e : rec.events)
    {
        if (e.rfind("float:", 0) == 0) { ++floats; }
        if (e.rfind("begin:", 0) == 0) { ++groups; }
        if (e.rfind("ro:parent=", 0) == 0) { saw_parent_ro = true; }
    }
    REQUIRE(groups == 3);  // position, rotation, scale
    REQUIRE(floats == 10); // Vec3(3) + Quat(4) + Vec3(3)
    REQUIRE(saw_parent_ro);
}

TEST_CASE("FieldVisitorAdapter writes float edits into the live object", "[reflection]")
{
    Transform t;
    t.position = mosaic::foundation::math::Vec3{1.0f, 2.0f, 3.0f};
    WritingVisitor w{9.0f};
    FieldVisitorAdapter adapter{w};
    reflect(adapter, t);

    REQUIRE(t.position.x == 9.0f);
    REQUIRE(t.position.y == 9.0f);
    REQUIRE(t.scale.z == 9.0f);
}

TEST_CASE("FieldVisitorAdapter falls back a std::string field to field_readonly when field_string is not overridden", "[reflection]")
{
    Name n{"Cube"};
    NoStringOverrideVisitor v;
    FieldVisitorAdapter adapter{v};
    reflect(adapter, n);

    REQUIRE(v.events.size() == 1);
    REQUIRE(v.events[0] == "ro:value=Cube");
}

TEST_CASE("FieldVisitorAdapter routes a std::string field to field_string when overridden, and the write lands in the live object", "[reflection]")
{
    Name n{"OldName"};
    StringWritingVisitor v{"NewName"};
    FieldVisitorAdapter adapter{v};
    reflect(adapter, n);

    REQUIRE(n.value == "NewName");
}

TEST_CASE("FieldVisitorAdapter routes an EntityID field to field_entity when overridden", "[reflection]")
{
    Transform t;
    t.parent = mosaic::foundation::EntityID(3, 1);
    EntityCapturingVisitor v;
    FieldVisitorAdapter adapter{v};
    reflect(adapter, t);

    REQUIRE(v.saw_entity);
    REQUIRE(v.seen_name == "parent");
    REQUIRE(v.seen_value == t.parent);
}
