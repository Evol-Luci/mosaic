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

// A reflectable carrying the two field kinds that are easy to confuse: `int` must reach
// field_int, while `bool` must keep going to its own branch rather than being swept up as an
// integral type.
struct IntHolder
{
    int  order = 3;
    bool flag  = true;
};

template <class V>
void reflect(V& visitor, IntHolder& h)
{
    visitor.member("order", h.order);
    visitor.member("flag", h.flag);
}

// Does NOT override field_int, to prove the default falls back to field_readonly -- the same
// pattern field_asset, field_string and field_entity each follow, and what keeps the other
// thirteen FieldVisitor implementors in this repository compiling unchanged.
struct NoIntOverrideVisitor : FieldVisitor
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

// Overrides field_int and writes back, the way ImGuiFieldVisitor's DragInt-backed override
// does.
struct IntWritingVisitor : FieldVisitor
{
    int                      new_value;
    std::vector<std::string> seen;
    explicit IntWritingVisitor(int v) : new_value(v) {}
    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view n, const std::string& d) override
    {
        seen.push_back("ro:" + std::string(n) + "=" + d);
    }
    void field_int(std::string_view n, int& value) override
    {
        seen.push_back("int:" + std::string(n));
        value = new_value;
    }
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

// Overrides field_bool and writes back, the way ImGuiFieldVisitor's Checkbox-backed override
// does. Deliberately does NOT override field_int, so the same visitor shows both halves of
// the dispatch: the `int` member must NOT arrive at field_bool, and the `bool` member must.
struct BoolWritingVisitor : FieldVisitor
{
    bool                     new_value;
    std::vector<std::string> seen;
    explicit BoolWritingVisitor(bool v) : new_value(v) {}
    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view n, const std::string& d) override
    {
        seen.push_back("ro:" + std::string(n) + "=" + d);
    }
    void field_bool(std::string_view n, bool& value) override
    {
        seen.push_back("bool:" + std::string(n));
        value = new_value;
    }
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

// Does NOT override field_bool, to prove the default falls back to field_readonly with the
// same `true`/`false` rendering the adapter emitted inline before field_bool existed -- the
// migration guarantee for the five other FieldVisitor implementations in this repository.
struct NoBoolOverrideVisitor : FieldVisitor
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

TEST_CASE("FieldVisitorAdapter falls back an EntityID field to field_readonly when field_entity is not overridden", "[reflection]")
{
    Transform t;
    t.parent = mosaic::foundation::EntityID(5, 2);
    RecordingVisitor v;
    FieldVisitorAdapter adapter{v};
    reflect(adapter, t);

    bool saw_parent_ro = false;
    for (const std::string& e : v.events)
    {
        if (e == "ro:parent=" + std::to_string(t.parent.value())) { saw_parent_ro = true; }
    }
    REQUIRE(saw_parent_ro);
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

TEST_CASE("FieldVisitorAdapter routes an int field to field_int when overridden", "[reflection]")
{
    IntHolder           h;
    IntWritingVisitor   vis(42);
    FieldVisitorAdapter adapter{vis};
    reflect(adapter, h);

    REQUIRE(vis.seen.size() == 2);
    REQUIRE(vis.seen[0] == "int:order");
    // The bool did NOT become an int: it goes to field_bool, which this visitor does not
    // override, so it falls back to field_readonly with the true/false rendering. This is
    // belt-and-braces rather than the real defence -- a probe confirmed that a branch broad
    // enough to swallow bool does not compile at all, since field_int takes `int&` and a
    // `bool&` cannot bind to it.
    REQUIRE(vis.seen[1] == "ro:flag=true");
}

TEST_CASE("FieldVisitorAdapter writes int edits into the live object", "[reflection]")
{
    IntHolder           h;
    IntWritingVisitor   vis(42);
    FieldVisitorAdapter adapter{vis};
    reflect(adapter, h);

    REQUIRE(h.order == 42); // the edit reached the component, not a copy
    REQUIRE(h.flag);        // and nothing else moved
}

TEST_CASE("FieldVisitorAdapter falls back an int field to field_readonly when field_int is not overridden",
          "[reflection]")
{
    // The migration guarantee. Thirteen other FieldVisitor implementors do not override this,
    // including the MarkdownFieldVisitor that generates docs/reference/components.md -- and
    // the fallback renders std::to_string, byte-identical to what the generic arithmetic
    // branch produced before, so that generated file does not move.
    IntHolder            h;
    NoIntOverrideVisitor vis;
    FieldVisitorAdapter  adapter{vis};
    reflect(adapter, h);

    REQUIRE(vis.events.size() == 2);
    REQUIRE(vis.events[0] == "ro:order=3");
    REQUIRE(vis.events[1] == "ro:flag=true");
}

TEST_CASE("FieldVisitorAdapter routes a bool field to field_bool when overridden, and leaves the int alone",
          "[reflection]")
{
    // MUTATION GUARD. Replacing the adapter's bool branch body with its pre-field_bool form,
    //     visitor.field_readonly(name, value ? "true" : "false");
    // turns this red: seen[1] becomes "ro:flag=true" instead of "bool:flag".
    IntHolder           h;
    BoolWritingVisitor  vis(false);
    FieldVisitorAdapter adapter{vis};
    reflect(adapter, h);

    REQUIRE(vis.seen.size() == 2);
    REQUIRE(vis.seen[0] == "ro:order=3"); // the int did NOT arrive at field_bool
    REQUIRE(vis.seen[1] == "bool:flag");
}

TEST_CASE("FieldVisitorAdapter writes bool edits into the live object", "[reflection]")
{
    IntHolder           h; // flag defaults true
    BoolWritingVisitor  vis(false);
    FieldVisitorAdapter adapter{vis};
    reflect(adapter, h);

    REQUIRE(h.flag == false); // the edit reached the component, not a copy
    REQUIRE(h.order == 3);    // and nothing else moved
}

TEST_CASE("FieldVisitorAdapter falls back a bool field to field_readonly when field_bool is not overridden",
          "[reflection]")
{
    // The migration guarantee, AND the ordering pin. `bool` is arithmetic, so if the bool
    // branch were deleted or demoted below the adapter's generic is_arithmetic_v branch, this
    // would read "ro:flag=1" -- which no compiler would object to. Five other FieldVisitor
    // implementations rely on this exact rendering, including the MarkdownFieldVisitor that
    // generates docs/reference/components.md.
    IntHolder             h;
    NoBoolOverrideVisitor vis;
    FieldVisitorAdapter   adapter{vis};
    reflect(adapter, h);

    REQUIRE(vis.events.size() == 2);
    REQUIRE(vis.events[0] == "ro:order=3");
    REQUIRE(vis.events[1] == "ro:flag=true");
}
