# Studio 37e — Play/Stop + Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a toolbar Play/Stop (+ Step) that pauses/resumes the editor's simulation via `RuntimeLoop`, and a Console panel that surfaces #33 log output live inside `mosaic editor`.

**Architecture:** A headless `PlayState` model drives `RuntimeLoop::start()/stop()/step()` from the editor frame loop. A thread-safe `ConsoleBuffer` (capacity-capped ring) plus a `ConsoleSink : log::Sink` capture log records (as owned copies) into the buffer; the editor registers the sink on the process-global default logger and the Console panel renders the buffer. All logic lives in the `studio/` tier; the draw + editor wiring are functionally verified.

**Tech Stack:** C++20, Dear ImGui (docking), `engine/runtime` (`RuntimeLoop`), `foundation/logging`, Catch2, CMake.

## Global Constraints

- **C++20**, header-only where the surrounding studio tier is (`viewport.hpp`, `hierarchy.hpp`, `selection.hpp` are header-only).
- **Tier contract** ([ARCHITECTURE.md](../../architecture/ARCHITECTURE.md)): `PlayState`/`ConsoleBuffer`/`ConsoleSink` live in `studio/`, which already depends on `engine/` (`RuntimeLoop` is `engine/runtime`) and `foundation/` (`log::Sink`). `foundation/`/`engine/` never depend on `studio/`.
- **`Sink` contract:** `ConsoleSink::write` is `noexcept` and must never call back into the logger. Wrap the capturing push in try/catch so an allocation failure drops the line instead of terminating.
- **`Record` lifetime:** `Record::category`/`Record::message` are `std::string_view`s valid only during `write` — the buffer MUST copy them into owned `std::string`.
- **Play/stop is pause/resume, not reset** — `stop()` does not clear the world. `RuntimeLoop::advance` returns early while stopped before banking time, so resume causes no catch-up burst.
- **No new third-party dependencies.**
- **Catch2 test names must not start with `--`** (breaks `catch_discover_tests`/`ctest`).
- **GUI verification is functional only** (sandbox cannot screenshot): `mosaic editor --ticks N` launches, exits 0. Note the app installs no logging sink, so stderr silence is NOT proof of no errors — the capture path is covered by unit tests instead.
- **Build/test commands (real presets — binaryDir is `build/debug`, exe at `build/debug/app/mosaic`):**
  - Configure (once): `cmake --preset debug`
  - Build: `cmake --build build/debug -j`
  - Test: `ctest --test-dir build/debug --output-on-failure`

---

## File Structure

- `studio/play_state.hpp` — **create.** Header-only `PlayState` (is-playing + play/stop/toggle + one-shot step request). No ImGui, no engine.
- `studio/console.hpp` — **create.** Header-only `ConsoleLine`, `ConsoleBuffer` (thread-safe ring), `ConsoleSink : log::Sink`.
- `studio/studio_app.{hpp,cpp}` — **modify.** Own a `PlayState` + a `shared_ptr<ConsoleBuffer>`; menu-bar Play/Stop/Step buttons; Console panel body.
- `app/run/editor.cpp` — **modify.** Register the console sink; sync `PlayState` → `RuntimeLoop` each frame; clear sinks on exit.
- `tests/unit/studio/play_state_test.cpp` — **create.** `PlayState` unit tests.
- `tests/unit/studio/console_test.cpp` — **create.** `ConsoleBuffer`/`ConsoleSink` unit tests.
- `tests/CMakeLists.txt` — **modify.** Register the two new test files.
- `docs/project/next-steps.md` — **modify.** Mark 37e built; Studio #37 v0.1 complete.

---

## Task 1: PlayState model

**Files:**
- Create: `studio/play_state.hpp`
- Create: `tests/unit/studio/play_state_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::studio::PlayState` with `bool is_playing() const`, `void play()`, `void stop()`, `void toggle()`, `void request_step()`, `bool consume_step_request()`. Default-constructed state is playing.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/studio/play_state_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "studio/play_state.hpp"

using mosaic::studio::PlayState;

TEST_CASE("PlayState defaults to playing", "[studio][play]")
{
    PlayState play;
    REQUIRE(play.is_playing());
}

TEST_CASE("PlayState play/stop are idempotent and toggle flips", "[studio][play]")
{
    PlayState play;
    play.stop();
    REQUIRE_FALSE(play.is_playing());
    play.stop(); // idempotent
    REQUIRE_FALSE(play.is_playing());
    play.play();
    REQUIRE(play.is_playing());
    play.play(); // idempotent
    REQUIRE(play.is_playing());
    play.toggle();
    REQUIRE_FALSE(play.is_playing());
    play.toggle();
    REQUIRE(play.is_playing());
}

TEST_CASE("PlayState step request is consumed once", "[studio][play]")
{
    PlayState play;
    REQUIRE_FALSE(play.consume_step_request()); // nothing queued
    play.request_step();
    REQUIRE(play.consume_step_request());       // returns true once
    REQUIRE_FALSE(play.consume_step_request());  // and clears
}
```

Register it in `tests/CMakeLists.txt` next to the other `unit/studio/...` entries:

```cmake
  unit/studio/play_state_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `studio/play_state.hpp` not found.

- [ ] **Step 3: Write the PlayState header**

Create `studio/play_state.hpp`:

```cpp
#pragma once

namespace mosaic::studio
{

// The editor's simulation run-state. The toolbar mutates it; the editor frame loop
// reads it each frame to drive the RuntimeLoop. Default is playing (the viewport is
// live on launch, matching 37d). stop() is a pause: it does not reset the world.
class PlayState
{
public:
    bool is_playing() const { return playing_; }
    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    void toggle() { playing_ = !playing_; }

    // Queue a single tick, honored while paused (RuntimeLoop::step). One-shot.
    void request_step() { step_requested_ = true; }
    bool consume_step_request()
    {
        const bool requested = step_requested_;
        step_requested_ = false;
        return requested;
    }

private:
    bool playing_       = true;
    bool step_requested_ = false;
};

} // namespace mosaic::studio
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "PlayState"`
Expected: PASS (3 cases).

- [ ] **Step 5: Commit**

```bash
git add studio/play_state.hpp tests/unit/studio/play_state_test.cpp tests/CMakeLists.txt
git commit -m "studio 37e: PlayState model (#37)"
```

---

## Task 2: ConsoleBuffer + ConsoleSink

**Files:**
- Create: `studio/console.hpp`
- Create: `tests/unit/studio/console_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::studio::ConsoleLine { mosaic::foundation::log::Level level; std::string category; std::string message; }`; `mosaic::studio::ConsoleBuffer` with `explicit ConsoleBuffer(std::size_t capacity = 1000)`, `void push(const mosaic::foundation::log::Record&)`, `std::vector<ConsoleLine> snapshot() const`, `std::size_t size() const`, `void clear()`; `mosaic::studio::ConsoleSink : mosaic::foundation::log::Sink` with `explicit ConsoleSink(std::shared_ptr<ConsoleBuffer>)` and `void write(const Record&) noexcept override`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/studio/console_test.cpp`:

```cpp
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "foundation/logging/log.hpp"
#include "studio/console.hpp"

using mosaic::foundation::log::Level;
using mosaic::foundation::log::Logger;
using mosaic::foundation::log::Record;
using mosaic::studio::ConsoleBuffer;
using mosaic::studio::ConsoleLine;
using mosaic::studio::ConsoleSink;

TEST_CASE("ConsoleSink captures log records into the buffer", "[studio][console]")
{
    auto   buffer = std::make_shared<ConsoleBuffer>();
    Logger logger;
    logger.add_sink(std::make_shared<ConsoleSink>(buffer));

    logger.log(Level::Warn, "vulkan", "validation blip");

    const auto snap = buffer->snapshot();
    REQUIRE(snap.size() == 1);
    REQUIRE(snap[0].level == Level::Warn);
    REQUIRE(snap[0].category == "vulkan");
    REQUIRE(snap[0].message == "validation blip");
}

TEST_CASE("ConsoleBuffer copies transient views so they outlive the record", "[studio][console]")
{
    ConsoleBuffer buffer;
    {
        // category/message live only in this scope; push must copy them.
        std::string category = "scope";
        std::string message  = "ephemeral";
        buffer.push(Record{Level::Info, category, message});
    }
    const auto snap = buffer.snapshot();
    REQUIRE(snap.size() == 1);
    REQUIRE(snap[0].category == "scope");
    REQUIRE(snap[0].message == "ephemeral");
}

TEST_CASE("ConsoleBuffer caps capacity, dropping the oldest", "[studio][console]")
{
    ConsoleBuffer buffer(3);
    for (int i = 0; i < 5; ++i)
    {
        buffer.push(Record{Level::Info, "c", std::to_string(i)});
    }
    const auto snap = buffer.snapshot();
    REQUIRE(snap.size() == 3);
    REQUIRE(snap[0].message == "2"); // oldest kept
    REQUIRE(snap[1].message == "3");
    REQUIRE(snap[2].message == "4"); // newest

    buffer.clear();
    REQUIRE(buffer.size() == 0);
}
```

Register it in `tests/CMakeLists.txt` next to the other `unit/studio/...` entries:

```cmake
  unit/studio/console_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `studio/console.hpp` not found.

- [ ] **Step 3: Write the console header**

Create `studio/console.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "foundation/logging/log.hpp"

namespace mosaic::studio
{

// One captured log line, with owned copies of the (transient) record strings.
struct ConsoleLine
{
    mosaic::foundation::log::Level level;
    std::string                    category;
    std::string                    message;
};

// A bounded, thread-safe ring buffer of captured log lines. push() may run on
// non-UI threads (Vulkan validation, jobs); snapshot() reads from the UI thread.
// Capacity-capped: the oldest line is dropped when full.
class ConsoleBuffer
{
public:
    explicit ConsoleBuffer(std::size_t capacity = 1000)
        : capacity_(capacity == 0 ? 1 : capacity)
    {
    }

    // Copies record.category / record.message (they are string_views valid only now).
    void push(const mosaic::foundation::log::Record& record)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(ConsoleLine{record.level, std::string(record.category),
                                     std::string(record.message)});
        while (lines_.size() > capacity_)
        {
            lines_.pop_front();
        }
    }

    std::vector<ConsoleLine> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<ConsoleLine>(lines_.begin(), lines_.end());
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }

private:
    mutable std::mutex      mutex_;
    std::deque<ConsoleLine> lines_;
    std::size_t             capacity_;
};

// A log::Sink that forwards records into a shared ConsoleBuffer. Held by the
// process-global default logger for the editor's lifetime. write() is noexcept and
// never calls back into the logger (honoring the Sink contract).
class ConsoleSink : public mosaic::foundation::log::Sink
{
public:
    explicit ConsoleSink(std::shared_ptr<ConsoleBuffer> buffer) : buffer_(std::move(buffer)) {}

    void write(const mosaic::foundation::log::Record& record) noexcept override
    {
        try
        {
            if (buffer_)
            {
                buffer_->push(record);
            }
        }
        catch (...)
        {
            // A sink must never throw into its caller. Drop the line on allocation
            // failure rather than terminate.
        }
    }

private:
    std::shared_ptr<ConsoleBuffer> buffer_;
};

} // namespace mosaic::studio
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "Console"`
Expected: PASS (3 cases).

- [ ] **Step 5: Commit**

```bash
git add studio/console.hpp tests/unit/studio/console_test.cpp tests/CMakeLists.txt
git commit -m "studio 37e: ConsoleBuffer + ConsoleSink log capture (#37)"
```

---

## Task 3: StudioApp — toolbar + Console panel

Wire `PlayState` and the `ConsoleBuffer` into `StudioApp`: menu-bar Play/Stop/Step buttons, and a real Console panel body. Draw code is functionally verified (ImGui needs a context), but the change compiles and is exercised by Task 4's editor run.

**Files:**
- Modify: `studio/studio_app.hpp`
- Modify: `studio/studio_app.cpp`

**Interfaces:**
- Consumes: `PlayState` (Task 1), `ConsoleBuffer` (Task 2).
- Produces: `StudioApp::play_state()` → `PlayState&`; `StudioApp::console()` → `std::shared_ptr<ConsoleBuffer>`; a Console panel body and Play/Stop/Step menu-bar controls.

- [ ] **Step 1: Add members + accessors to the header**

In `studio/studio_app.hpp`, add includes after the existing studio includes:

```cpp
#include "studio/console.hpp"
#include "studio/play_state.hpp"
```

Add the accessors in the `public:` section (after `viewport()`):

```cpp
    PlayState&                     play_state() { return play_; }
    std::shared_ptr<ConsoleBuffer> console() { return console_; }
```

Declare the panel body in the `private:` section (after `draw_viewport`):

```cpp
    void draw_console();
```

Add the members in the `private:` section (after `viewport_`):

```cpp
    PlayState                      play_;
    std::shared_ptr<ConsoleBuffer> console_ = std::make_shared<ConsoleBuffer>();
```

- [ ] **Step 2: Add the menu-bar Play/Stop/Step controls**

In `studio/studio_app.cpp`, inside `draw_ui`, extend the main menu bar — replace:

```cpp
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit"))
            {
                layout_.request_exit();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
```

with:

```cpp
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit"))
            {
                layout_.request_exit();
            }
            ImGui::EndMenu();
        }

        // Simulation toolbar: Play/Stop toggles running; Step ticks once while paused.
        if (ImGui::Button(play_.is_playing() ? "Stop" : "Play"))
        {
            play_.toggle();
        }
        ImGui::BeginDisabled(play_.is_playing());
        if (ImGui::Button("Step"))
        {
            play_.request_step();
        }
        ImGui::EndDisabled();

        ImGui::EndMainMenuBar();
    }
```

- [ ] **Step 3: Dispatch the Console panel**

In `draw_ui`, extend the panel dispatch chain — add a `Console` branch before the `else`:

```cpp
            else if (panel.name == "Inspector")
            {
                draw_inspector(world, reflection);
            }
            else if (panel.name == "Console")
            {
                draw_console();
            }
            else
            {
                ImGui::TextUnformatted("(empty)");
            }
```

- [ ] **Step 4: Implement `draw_console`**

In `studio/studio_app.cpp`, add the method (place it after `draw_viewport`):

```cpp
void StudioApp::draw_console()
{
    for (const ConsoleLine& line : console_->snapshot())
    {
        std::string text = "[" + std::string(mosaic::foundation::log::to_string(line.level)) +
                           "] " + line.category + ": " + line.message;
        ImGui::TextUnformatted(text.c_str());
    }
    // Auto-scroll to the newest line when the view is already pinned to the bottom.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
}
```

Add `#include <string>` to `studio/studio_app.cpp`'s includes if not already present (it uses `std::string` here).

- [ ] **Step 5: Build**

Run: `cmake --build build/debug -j 2>&1 | tail -5`
Expected: builds clean.

- [ ] **Step 6: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp
git commit -m "studio 37e: play/stop/step toolbar + Console panel body (#37)"
```

---

## Task 4: Editor wiring — drive the loop + capture logs

**Files:**
- Modify: `app/run/editor.cpp`

**Interfaces:**
- Consumes: `StudioApp::play_state()`, `StudioApp::console()` (Task 3); `ConsoleSink` (Task 2); `RuntimeLoop::start/stop/step` (existing); `log::add_sink`/`log::clear_sinks` (existing).

- [ ] **Step 1: Add includes**

In `app/run/editor.cpp`, add:

```cpp
#include "foundation/logging/log.hpp"
#include "studio/console.hpp"
```

- [ ] **Step 2: Register the console sink after StudioApp is constructed**

After `mosaic::studio::StudioApp studio;` (currently line ~85), add:

```cpp
    // Route all logging (StatusReporter, Vulkan validation, ...) into the Console panel.
    mosaic::foundation::log::add_sink(
        std::make_shared<mosaic::studio::ConsoleSink>(studio.console()));
```

- [ ] **Step 3: Drive the RuntimeLoop from PlayState each frame**

In the `on_frame` lambda, add `&loop` to the capture list and sync the run state at the top (right after `glfwPollEvents();`):

```cpp
    deps.on_frame = [window, &device, &studio, &world, &reflection, render_system, &asset_registry, &loop] {
        glfwPollEvents();

        // Toolbar-driven simulation control: pause = stop, resume = start, step = one tick.
        mosaic::studio::PlayState& play = studio.play_state();
        if (play.is_playing()) { loop.start(); } else { loop.stop(); }
        if (play.consume_step_request()) { loop.step(world); }

        device->begin_frame();
```

(The rest of the lambda body is unchanged. `window` remains captured though unused in this block — leave the existing capture list items intact.)

- [ ] **Step 4: Clear sinks on exit**

Before `return 0;` at the end of `run_editor` (after `glfwTerminate();`), add:

```cpp
    mosaic::foundation::log::clear_sinks(); // drop the console sink before the buffer dies
```

Place it immediately before `return 0;`.

- [ ] **Step 5: Build**

Run: `cmake --build build/debug -j 2>&1 | tail -5`
Expected: builds clean.

- [ ] **Step 6: Verify the full suite + the editor functionally**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all tests PASS (including Task 1–2 additions).

Run: `./build/debug/app/mosaic editor --ticks 120; echo "exit=$?"`
Expected: `exit=0`. The editor window shows a Play/Stop + Step toolbar in the menu bar and a Console panel with log lines (the StatusReporter progress/summary now has a sink). Verified functionally; the GUI cannot be screenshotted in the sandbox. If `--ticks` is not honored, use `timeout 5 ./build/debug/app/mosaic editor; echo "exit=$?"`.

- [ ] **Step 7: Commit**

```bash
git add app/run/editor.cpp
git commit -m "studio 37e: editor drives RuntimeLoop from PlayState + captures logs (#37)"
```

---

## Task 5: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Mark 37e built and Studio #37 v0.1 complete**

Update `docs/project/next-steps.md`: change the "Last updated" line and the Studio status so **37e (play/stop + console) is built** and **the v0.1 Studio (#37) lean core is complete** (37a–37e all built). Update the Studio suggested-order lines (37a ✓ → 37b ✓ → 37c ✓ → 37d ✓ → 37e ✓). Note the remaining deferred Studio work (asset browser, Blender live-link) stays post-v0.1, and record the flagged follow-up: the app installs no logging sink, so `mosaic run` output is dropped until a stderr `ConsoleSink` is installed in the CLI entry path.

- [ ] **Step 2: Commit (rides on the feature branch, not its own PR)**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: Studio 37e built; v0.1 Studio (#37) lean core complete"
```

---

## Self-Review

**Spec coverage:**
- `PlayState` model (spec §PlayState) → Task 1. ✓
- Editor frame integration / loop drive (spec §Editor frame integration) → Task 4. ✓
- `ConsoleBuffer` (spec §ConsoleBuffer) → Task 2. ✓
- `ConsoleSink` (spec §ConsoleSink) → Task 2. ✓
- Registration & lifetime — add_sink at start, clear_sinks at exit (spec §Registration & lifetime) → Task 4. ✓
- Studio panels — accessors, menu-bar buttons, Console body (spec §Studio panels) → Task 3. ✓
- Testability boundary — PlayState + Console unit tests, functional editor (spec §Testability) → Tasks 1, 2, 4. ✓
- Follow-up (app installs no sink) → recorded in Task 5 next-steps + already flagged as a separate task. ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete code; commands have expected output. ✓

**Type consistency:** `PlayState` methods (`is_playing`/`play`/`stop`/`toggle`/`request_step`/`consume_step_request`) identical across Tasks 1/3/4; `ConsoleBuffer`/`ConsoleSink`/`ConsoleLine` signatures match across Tasks 2/3/4; `play_state()` / `console()` accessors consistent between Task 3 (definition) and Task 4 (use); `to_string(Level)` is the existing `foundation/logging/log.hpp` free function. ✓

**One open verification note (flagged, not blocking):** because the app installs no logging sink outside the editor, a "silent stderr" run does not prove the absence of log-worthy errors. The capture path is therefore covered by unit tests (Task 2); the functional editor run (Task 4) verifies launch/exit only. The broader CLI-sink gap is a separate, already-filed follow-up.
