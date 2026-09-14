#pragma once

#include <memory>

#include "engine/script/script_engine.hpp"

namespace mosaic::script::python_studio
{
// Create a CPython-backed ScriptEngine for Studio plugins: every gameplay-script
// global (get_component, set_component, ...) plus imgui_* drawing globals for custom
// panels. Shares the SAME process-wide CPython interpreter modules/script_python
// initializes (see ensure_interpreter() in python_script_engine.hpp).
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_studio_plugin_script_engine();
} // namespace mosaic::script::python_studio
