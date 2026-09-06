# Script Debugger (tracepoint inspection) — Design

## Problem

No way to inspect script execution — where breakpoints are hit or what local
variables held. A *pause-and-step* debugger would deadlock in inline mode
(the script runs on the ImGui thread; blocking the trace hook freezes the UI
that must draw Step/Continue). So v0.1 is a **non-blocking tracepoint
inspector** (maintainer-approved Option A): breakpoints record hits + locals
without pausing. It's the foundation a future worker-thread stepping mode builds
on.

## Design

### 1. `engine/script/script_debugger.hpp` (new)

Neutral (no Python types):

```cpp
struct BreakpointHit
{
    int line;
    std::vector<std::pair<std::string, Value>> locals; // frame locals at the hit (marshaled)
};

class ScriptDebugger
{
public:
    void set_breakpoint(int line);
    void clear_breakpoint(int line);
    void clear_breakpoints();
    bool has_breakpoint(int line) const;         // hot path for the trace hook
    const std::set<int>& breakpoints() const;

    void record_hit(BreakpointHit hit);          // called by the backend trace hook
    const std::vector<BreakpointHit>& hits() const;
    void clear_hits();
private:
    std::set<int>              breakpoints_;
    std::vector<BreakpointHit> hits_;
};
```

### 2. `ScriptEngine::bind_debugger(ScriptDebugger*)`

Non-pure default no-op; `nullptr` detaches. When bound, `run_string` installs a
`sys.settrace` hook around `py::exec` and uninstalls it on every exit path.

### 3. Python backend

- Members: `ScriptDebugger* debugger_ = nullptr; py::object trace_fn_;`.
- `bind_debugger(ScriptDebugger* d) override { debugger_ = d; }`.
- Lazily build `trace_fn_` (a `py::cpp_function`) once. It returns **itself** so
  it acts as both the global (`'call'`) and per-frame (`'line'`) trace:
  ```cpp
  // on 'line': if debugger_->has_breakpoint(frame.f_lineno), snapshot f_locals
  //   (each marshaled via py_to_value; non-marshalable -> null Value) and
  //   debugger_->record_hit({line, locals}). Always returns trace_fn_ to keep tracing.
  ```
- In `run_string`, when `debugger_ != nullptr`: `sys.settrace(trace_fn_)` before
  `py::exec`; `sys.settrace(None)` in the stream-restore guard (all exit paths).
  Non-blocking — the hook never pauses, so no deadlock. Overhead (per-line
  callback) is paid only while a debugger is attached (editor debug runs), never
  on the runtime hot path.

### 4. `studio/script_editor.hpp` — a `debug()` entry point

```cpp
ScriptApplyResult debug(const std::string& source, World&, const ComponentReflection&,
                        EntityID e, ScriptDebugger& debugger);
```
Binds world + `entity`, binds the debugger, clears its hits, runs the source
(defines `on_update`), then calls `on_update(0)` **once** so breakpoints inside
`on_update` fire. Detaches the debugger. Fills `debugger.hits()`. Never throws.

### 5. Studio glue (`studio_app.cpp`, Script Editor panel) — thin, GUI

A breakpoint line-number input + a **Debug** button (calls `debug()`), and a
**Hits** list showing `line` + captured locals. Thin ImGui over the model;
needs visual acceptance (headless-unverifiable, like the other editor panels).

## Testing (headless)

1. **`ScriptDebugger`** (`script_debugger_test.cpp`): set/clear/has/breakpoints;
   record/hits/clear_hits.
2. **Backend trace** (`script_debug_engine_test.cpp`): `bind_debugger` with a
   breakpoint on a known line; run a script; assert one hit at that line whose
   locals include the expected name→value. No breakpoint ⇒ no hits. Detaching
   (`bind_debugger(nullptr)`) ⇒ no further hits.
3. **Editor `debug()`** (`script_editor_test.cpp`): a breakpoint on a line inside
   `on_update`; `debug()` runs it once and the hit is recorded (proves
   `on_update` is exercised, not just top-level).

## Out of scope / follow-ups

- **True pause/step** on the worker thread (the "real" debugger) — the big
  visual follow-up this foundation enables.
- Debugging **live in Play mode** (bind a debugger into `ScriptSystem`).
- Conditional breakpoints, call stacks, watch expressions.
