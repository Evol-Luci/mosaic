# foundation/logging — Design

**Issue:** [#33 foundation/logging: structured leveled logging facility](https://github.com/Evol-Luci/mosaic/issues/33)
**Status:** Accepted
**Date:** 2026-08-16

## Purpose

A leveled, categorized logging facility in the `foundation/` tier. It is the
nearest of the remaining v0.1 foundation pieces: the app/CLI shell, rendering,
the asset registry, and jobs all want it. Dependency-free and header-first like
the rest of `foundation/`.

## Scope

In scope (from the issue's acceptance criteria):

- Five levels: `trace / debug / info / warn / error`.
- A **category** (subsystem) tag per message (e.g. `"ecs"`, `"assets"`, `"render"`).
- A `Sink` interface + a default **console sink**; a runtime minimum-level filter.
- Thread-safe: jobs and rendering will log off-thread, with no torn/interleaved lines.

Out of scope (future work):

- File / rotating sinks, structured JSON sink, async logging queue, per-category
  level configuration.
- Timestamps and source-location capture — they ride in with the macro layer
  below, not in v0.1.

## Design decisions

Two design forks were settled toward "the simplest thing that ships in v0.1, with
a clean additive path to the richer version afterward."

### 1. Logger access — global surface over an internal `Logger` class

Logging is cross-cutting: a trace call deep in a call stack should not have to be
handed a logger reference. So v0.1 exposes a **process-global free-function
surface** (`log::info(cat, fmt, args...)`).

To keep this from becoming hidden global state that is hard to test or grow, the
core is built as a complete **`Logger` class** (sink list + min-level + mutex).
The global surface forwards to a single function-local-static default `Logger`.
Promoting the `Logger` class to public API later (so callers can instantiate and
inject their own logger, EventBus-style) is then a **purely additive,
non-breaking** change — no caller edits, no re-plumbing.

This confines the one global to this single facility, with the class already
structured for the eventual instance-based API.

### 2. Message API — `std::format`-style free functions

C++20's `std::format` gives type-safe, compile-checked format strings with no
macros, matching the codebase's clean-header style:

```cpp
log::info("render", "frame {} took {:.2f}ms", n, dt);
```

Trade-off accepted for v0.1: arguments are formatted **before** the min-level
check, so a filtered-out `trace`/`debug` still pays its formatting cost. The
post-v0.1 follow-up is a thin **macro layer** (`MOSAIC_LOG_TRACE(cat, ...)`) that
checks min-level *before* formatting and can auto-capture `source_location`.
Because the macros would simply wrap these same free functions behind a level
check, that layer is additive too.

### 3. Thread-safety — a single mutex guarding dispatch

The `Logger` holds one `std::mutex`. `log()` early-returns on the min-level check
(no lock needed to drop a message), otherwise locks and iterates the sink list.
`add_sink` / `clear_sinks` / `set_min_level` lock the same mutex. This serializes
log calls — the simplest correct approach that satisfies "no torn lines" — and is
adequate for v0.1 (a lock-free or per-sink-buffered path is future work if the log
path ever becomes hot).

### 4. Header-only

The global default `Logger` is a function-local static (Meyers singleton); the
free functions and `ConsoleSink` are `inline`. No `.cpp` and no ODR hazard,
matching the header-first foundation units (events, reflection, math).

## Architecture

One header: `foundation/logging/log.hpp`, namespace `mosaic::foundation::log`.

### `Level`

```cpp
enum class Level { Trace, Debug, Info, Warn, Error };
```

Ordered so the min-level filter is a `level >= min_level` comparison. A
`constexpr std::string_view to_string(Level)` renders the tag for sinks.

### `Record`

The immutable value handed to a sink. The message is already formatted by the
time a sink sees it.

```cpp
struct Record {
    Level            level;
    std::string_view category;
    std::string_view message;
};
```

### `Sink`

```cpp
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const Record&) noexcept = 0;
};
```

`write` is `noexcept` by contract: logging happens during error handling, so a
sink must never throw into the caller. A sink that does I/O swallows its own
failures (a failed console write does nothing rather than propagate).

### `ConsoleSink`

An `inline` `Sink` that writes `"[LEVEL] category: message\n"` to `std::cerr`.

### `Logger`

The complete core (internal in v0.1, promotable to public later):

```cpp
class Logger {
public:
    void add_sink(std::shared_ptr<Sink>);
    void clear_sinks();
    void set_min_level(Level);
    void log(Level, std::string_view category, std::string_view message);
private:
    std::vector<std::shared_ptr<Sink>> sinks_;
    Level      min_level_ = Level::Info;
    std::mutex mutex_;
};
```

`log` early-returns when `level < min_level_`; otherwise it locks `mutex_` and
calls `write` on each sink in registration order.

### Global surface (v0.1 caller API)

```cpp
Logger& default_logger();                       // function-local static

void set_min_level(Level);
void add_sink(std::shared_ptr<Sink>);
void clear_sinks();                             // reset; tests rely on it

template <class... A> void trace(std::string_view cat, std::format_string<A...>, A&&...);
template <class... A> void debug(std::string_view cat, std::format_string<A...>, A&&...);
template <class... A> void info (std::string_view cat, std::format_string<A...>, A&&...);
template <class... A> void warn (std::string_view cat, std::format_string<A...>, A&&...);
template <class... A> void error(std::string_view cat, std::format_string<A...>, A&&...);
```

Each level function builds the string with `std::format` and forwards to
`default_logger().log(level, cat, str)`. The `set_min_level` / `add_sink` /
`clear_sinks` free functions forward to the same default logger.

## Data flow

```
log::info("assets", "loaded {}", id)
    -> std::format("loaded {}", id)              // formatted string
    -> default_logger().log(Info, "assets", str)
    -> if Info < min_level_: return              // filtered, no lock
    -> lock mutex_
    -> for each sink: sink->write({Info,"assets",str})
```

## Error handling

- Logging never throws into a caller. `std::format` can throw `std::format_error`
  only on a malformed format string — a call-site programmer bug surfaced in
  development — so it is not wrapped.
- `Sink::write` is `noexcept`; sinks absorb their own I/O failures.
- Sink registration and logging share the mutex, so `add_sink` concurrent with
  `log` is safe.

## Testing

`tests/unit/logging/`, tag `[logging]`, following the per-subsystem test layout.
A small capturing test sink records the `Record`s it receives. Each test calls
`clear_sinks()` and resets the min-level so cases are isolated on the global.

- **Levels route to sinks** — log at each level into a capturing sink; assert the
  level, category, and message land.
- **Min-level filtering** — set `Warn`; assert `trace/debug/info` are dropped and
  `warn/error` pass.
- **Multiple sinks** — two registered sinks both receive each record.
- **Format substitution** — `"{}"` arguments render into the captured message.
- **Thread-safety** — N threads each log M messages into a mutex-backed counting
  sink; assert total received == N*M, and (via a sink that appends whole lines)
  that no line is torn or interleaved.

## Acceptance criteria (from #33)

- [ ] Log at each level with a category; messages route to registered sinks.
- [ ] A `Sink` abstraction + a console sink; runtime min-level filtering drops
      lower levels.
- [ ] Thread-safe concurrent logging (no interleaved/torn lines).
- [ ] Unit tests under `tests/unit/` capturing via a test sink.

## Tier / dependency contract

`foundation/logging` depends on nothing above it and nothing else in
`foundation/` — only the standard library (`<format>`, `<mutex>`, `<memory>`,
`<vector>`, `<string>`, `<string_view>`, `<iostream>`). It is consumed by the
app/CLI shell and every later subsystem.
