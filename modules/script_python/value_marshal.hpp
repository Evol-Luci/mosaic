#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>

#include "foundation/serialization/value.hpp"

namespace mosaic::script::python
{
namespace py = pybind11;

// #10 Value DOM -> Python object. Requires a running interpreter + the GIL (held by the caller).
inline py::object value_to_py(const mosaic::foundation::serialization::Value& v)
{
    namespace ser = mosaic::foundation::serialization;
    if (v.is<std::monostate>()) { return py::none(); }
    if (v.is<bool>()) { return py::bool_(v.as<bool>()); }
    if (v.is<std::int64_t>()) { return py::int_(v.as<std::int64_t>()); }
    if (v.is<std::uint64_t>()) { return py::int_(v.as<std::uint64_t>()); }
    if (v.is<double>()) { return py::float_(v.as<double>()); }
    if (v.is<std::string>()) { return py::str(v.as<std::string>()); }
    if (v.is<ser::Array>())
    {
        py::list list;
        for (const auto& e : v.as<ser::Array>()) { list.append(value_to_py(e)); }
        return list;
    }
    if (v.is<ser::Table>())
    {
        py::dict dict;
        for (const auto& [key, val] : v.as<ser::Table>()) { dict[py::str(key)] = value_to_py(val); }
        return dict;
    }
    return py::none();
}

// Python object -> #10 Value DOM. Throws std::runtime_error on an unsupported type (surfaces to
// the script as a normal exception).
inline mosaic::foundation::serialization::Value py_to_value(const py::handle& o)
{
    using mosaic::foundation::serialization::Array;
    using mosaic::foundation::serialization::Table;
    using mosaic::foundation::serialization::Value;
    if (o.is_none()) { return Value{}; }
    if (py::isinstance<py::bool_>(o)) { return Value(o.cast<bool>()); }
    if (py::isinstance<py::int_>(o)) { return Value(o.cast<std::int64_t>()); }
    if (py::isinstance<py::float_>(o)) { return Value(o.cast<double>()); }
    if (py::isinstance<py::str>(o)) { return Value(o.cast<std::string>()); }
    if (py::isinstance<py::list>(o) || py::isinstance<py::tuple>(o))
    {
        Array a;
        for (const auto& e : o) { a.push_back(py_to_value(e)); }
        return Value(std::move(a));
    }
    if (py::isinstance<py::dict>(o))
    {
        Table t;
        for (const auto& item : py::reinterpret_borrow<py::dict>(o))
        {
            t[item.first.cast<std::string>()] = py_to_value(item.second);
        }
        return Value(std::move(t));
    }
    throw std::runtime_error("unsupported Python type for a Mosaic Value");
}
} // namespace mosaic::script::python
