#pragma once

#include <pybind11/embed.h>

namespace mosaic::script::python
{
// Replaces globals["__builtins__"] with a restricted builtins namespace for
// less-trusted code (Studio plugins, asset importers). Defense-in-depth: this
// blocks the obvious attack surface (eval/exec/compile/__import__ of anything
// not on a small allowlist, plus open() unless allow_open is set) but does NOT
// claim to be a hard security boundary -- CPython has no supported way to fully
// sandbox arbitrary code running in-process (a determined attacker can still
// escape via object introspection, e.g.
// ().__class__.__bases__[0].__subclasses__()). Real isolation for genuinely
// untrusted code needs OS-level separation; see docs/design/plugin-sandboxing.md.
//
// allow_open exists because asset importers' entire purpose is reading their
// source file (the real, shipped tools/example_plugins/csv_importer.py does
// `with open(source_path) as f: ...`) -- blocking open() for them would break
// the feature, not secure it. Studio plugins have no such need, so their caller
// leaves allow_open at its default false.
void install_restricted_builtins(pybind11::dict& globals, bool allow_open = false);
} // namespace mosaic::script::python
