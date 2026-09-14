#pragma once

#include <pybind11/pybind11.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::script::python
{
namespace py = pybind11;

// Installs the shared gameplay-script globals (get_component, set_component,
// create_entity, destroy_entity, entity_count, entities, is_valid, add_component,
// remove_component, has_component, component_names, entities_with, emit, undo, redo,
// undo_depth) into `globals`. world_ptr/types_ptr/stack_ptr are pointers-to-pointer:
// the installed lambdas read *world_ptr/*types_ptr/*stack_ptr at CALL time, not
// install time, so a caller can install once in bind_world() and later just update
// *stack_ptr in bind_commands() without reinstalling anything -- undo/redo/undo_depth
// stay functional (returning false/0) even before *stack_ptr is set.
void install_world_globals(py::dict& globals,
                           mosaic::engine::World** world_ptr,
                           const mosaic::engine::reflection::ComponentReflection** types_ptr,
                           mosaic::foundation::commands::CommandStack** stack_ptr);
} // namespace mosaic::script::python
