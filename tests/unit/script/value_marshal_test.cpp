#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <pybind11/embed.h>

#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "modules/script_python/value_marshal.hpp"

using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;
using mosaic::script::python::py_to_value;
using mosaic::script::python::value_to_py;

namespace
{
// Force the process-once interpreter up. It no longer leaves the GIL held on this
// thread -- nothing does -- so each case below takes it explicitly.
void ensure_interpreter()
{
    static auto engine = create_python_script_engine();
    (void)engine;
}
} // namespace

TEST_CASE("value marshaling round-trips every DOM scalar and container", "[script]")
{
    ensure_interpreter();
    const pybind11::gil_scoped_acquire gil;
    auto rt = [](const Value& v) { return py_to_value(value_to_py(v)); };

    REQUIRE(rt(Value{}) == Value{});                                    // monostate <-> None
    REQUIRE(rt(Value(true)) == Value(true));                           // bool
    REQUIRE(rt(Value(false)) == Value(false));
    REQUIRE(rt(Value(static_cast<std::int64_t>(-42))) == Value(static_cast<std::int64_t>(-42))); // int64
    REQUIRE(rt(Value(3.5)) == Value(3.5));                             // double
    REQUIRE(rt(Value(std::string("hello"))) == Value(std::string("hello"))); // string

    Array arr;                                                         // Array of int64
    arr.push_back(Value(static_cast<std::int64_t>(1)));
    arr.push_back(Value(static_cast<std::int64_t>(2)));
    REQUIRE(rt(Value(arr)) == Value(arr));

    Table nested;                                                      // nested Table + Array
    nested["x"] = Value(static_cast<std::int64_t>(7));
    Array inner;
    inner.push_back(Value(std::string("a")));
    inner.push_back(Value(std::string("b")));
    nested["tags"] = Value(std::move(inner));
    REQUIRE(rt(Value(nested)) == Value(nested));
}

TEST_CASE("value_to_py marshals a uint64 to a Python int", "[script]")
{
    ensure_interpreter();
    const pybind11::gil_scoped_acquire gil;
    // py_to_value collapses Python int -> int64, so a uint64 Value does not round-trip as uint64.
    // Exercise the uint64 arm of value_to_py directly.
    namespace py = pybind11;
    py::object o = value_to_py(Value(static_cast<std::uint64_t>(123)));
    REQUIRE(o.cast<std::uint64_t>() == 123u);
}
