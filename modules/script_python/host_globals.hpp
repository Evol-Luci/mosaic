#pragma once

#include <pybind11/pybind11.h>

namespace mosaic::script::python
{
namespace py = pybind11;

// Installs the ScriptHost-backed globals (raycast, play, pause, stop, apply_impulse)
// with no host attached, purely so the docs generator can render their signatures and
// docstrings. Every one is a no-op in this state, which is exactly ScriptHost's
// documented behaviour for an unset hook.
void install_host_globals_for_docs(py::dict& globals);

} // namespace mosaic::script::python
