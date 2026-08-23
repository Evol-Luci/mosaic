#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/field_visitor.hpp"
#include "engine/transform/transform.hpp"
#include "foundation/math/vec3.hpp"

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
