#pragma once

#include <memory>

#include "engine/script/script_engine.hpp"

namespace mosaic::script::python
{
// Ensures the embedded CPython interpreter is initialized (idempotent, process-wide
// std::call_once guard -- safe to call from multiple translation units/modules).
void ensure_interpreter();

// Create a CPython-backed ScriptEngine. Initializes the embedded interpreter once per process
// (never finalized). Injected like create_box3d_backend.
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine();
} // namespace mosaic::script::python
