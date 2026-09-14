#include "modules/script_python/world_globals.hpp"

#include <memory>
#include <string>

#include "engine/commands/set_component_command.hpp"
#include "engine/script/script_event.hpp"
#include "foundation/core/entity_id.hpp"
#include "modules/script_python/value_marshal.hpp"

namespace mosaic::script::python
{
void install_world_globals(py::dict& globals,
                           mosaic::engine::World** world_ptr,
                           const mosaic::engine::reflection::ComponentReflection** types_ptr,
                           mosaic::foundation::commands::CommandStack** stack_ptr)
{
    globals["get_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> py::object
        {
            auto v = (*types_ptr)->component_value(**world_ptr, mosaic::foundation::EntityID(eid), name);
            return v ? value_to_py(*v) : py::none();
        });
    globals["set_component"] = py::cpp_function(
        [world_ptr, types_ptr, stack_ptr](std::uint64_t eid, const std::string& name, py::handle value) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            const mosaic::foundation::serialization::Value v = py_to_value(value);
            if (*stack_ptr != nullptr)
            {
                auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(
                    **world_ptr, **types_ptr, e, name, v);
                cmd->execute();
                if (!cmd->valid()) { return false; } // failed set: not recorded
                (*stack_ptr)->push_executed(std::move(cmd));
                return true;
            }
            return (*types_ptr)->set_value(**world_ptr, e, name, v);
        });
    globals["create_entity"] = py::cpp_function(
        [world_ptr]() -> std::uint64_t { return (*world_ptr)->create_entity().value(); });
    globals["destroy_entity"] = py::cpp_function(
        [world_ptr](std::uint64_t eid) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            if (!(*world_ptr)->is_valid(e)) { return false; }
            (*world_ptr)->destroy_entity(e);
            return true;
        });
    globals["entity_count"] = py::cpp_function(
        [world_ptr]() -> std::uint64_t { return static_cast<std::uint64_t>((*world_ptr)->entity_count()); });
    globals["entities"] = py::cpp_function(
        [world_ptr]() -> py::list
        {
            py::list out;
            (*world_ptr)->for_each_entity([&](mosaic::foundation::EntityID e)
                                          { out.append(py::int_(e.value())); });
            return out;
        });
    globals["is_valid"] = py::cpp_function(
        [world_ptr](std::uint64_t eid) -> bool
        { return (*world_ptr)->is_valid(mosaic::foundation::EntityID(eid)); });
    globals["add_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        { return (*types_ptr)->add_default(**world_ptr, mosaic::foundation::EntityID(eid), name); });
    globals["remove_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        { return (*types_ptr)->remove_value(**world_ptr, mosaic::foundation::EntityID(eid), name); });
    globals["has_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            for (const std::string& n : (*types_ptr)->components_on(**world_ptr, e))
            {
                if (n == name) { return true; }
            }
            return false;
        });
    globals["component_names"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid) -> py::list
        {
            py::list out;
            for (const std::string& n :
                 (*types_ptr)->components_on(**world_ptr, mosaic::foundation::EntityID(eid)))
            {
                out.append(py::str(n));
            }
            return out;
        });
    globals["entities_with"] = py::cpp_function(
        [world_ptr, types_ptr](const std::string& name) -> py::list
        {
            py::list out;
            (*world_ptr)->for_each_entity(
                [&](mosaic::foundation::EntityID e)
                {
                    for (const std::string& n : (*types_ptr)->components_on(**world_ptr, e))
                    {
                        if (n == name) { out.append(py::int_(e.value())); break; }
                    }
                });
            return out;
        });
    globals["emit"] = py::cpp_function(
        [world_ptr](const std::string& name, py::object payload)
        {
            (*world_ptr)->events().emit(
                mosaic::engine::script::ScriptEvent{name, py_to_value(payload)});
        },
        py::arg("name"), py::arg("payload") = py::none());
    globals["undo"] =
        py::cpp_function([stack_ptr]() -> bool { return *stack_ptr != nullptr && (*stack_ptr)->undo(); });
    globals["redo"] =
        py::cpp_function([stack_ptr]() -> bool { return *stack_ptr != nullptr && (*stack_ptr)->redo(); });
    globals["undo_depth"] = py::cpp_function(
        [stack_ptr]() -> std::uint64_t
        { return *stack_ptr != nullptr ? static_cast<std::uint64_t>((*stack_ptr)->undo_depth()) : 0; });
}
} // namespace mosaic::script::python
