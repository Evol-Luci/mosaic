# Studio 37a — Editor Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `mosaic editor` command that opens a Dear ImGui docking window — dockspace, menu bar, and empty Hierarchy/Inspector/Console placeholder panels — over the existing GLFW+Vulkan setup.

**Architecture:** A new `studio/` tier (`mosaic_studio`) holds a unit-testable `StudioLayout` model and a `StudioApp::draw_ui()` ImGui glue; it depends only on Dear ImGui. The neutral `RenderDevice` grows three default-no-op UI hooks the Vulkan backend overrides to drive ImGui into its existing render pass. A `run_editor` glue function in the app tier reuses `FramePump` (advancing an empty idle world) and dispatches from `main.cpp` on a new `Command` enum.

**Tech Stack:** C++20, CMake + FetchContent, Dear ImGui (docking branch: core + `imgui_impl_glfw` + `imgui_impl_vulkan`), GLFW, Vulkan, CLI11, Catch2.

**Design spec:** `docs/superpowers/specs/2026-08-22-studio-37a-editor-shell-design.md`
**Decomposition:** `docs/superpowers/specs/2026-08-22-studio-decomposition.md`

## Global Constraints

- **Tier contract:** `engine/` and `foundation/` never depend on `studio/` or ImGui. `studio/` depends only on ImGui (and the C++ stdlib). The `run_editor` glue lives in the **app tier** (`mosaic_app`) and depends on `mosaic_studio`. Only `modules/render_vulkan` links the ImGui *backends* (`imgui_impl_glfw`/`imgui_impl_vulkan`).
- **Language / build:** C++20. Build with `cmake --build --preset debug` (configure once with `cmake --preset debug` if `build/debug` is absent). Full suite: `ctest --preset debug`. Unit binary: `build/debug/tests/mosaic_unit_tests "<tag>"`. App binary: `build/debug/app/mosaic`.
- **Dear ImGui version:** FetchContent `GIT_TAG v1.91.5-docking` (docking branch). The ImGui Vulkan-backend API is version-sensitive: the fetched tree ships `backends/imgui_impl_vulkan.h` and `examples/example_glfw_vulkan/main.cpp` — treat that header + example as authoritative for the exact `ImGui_ImplVulkan_Init` signature and font-texture handling at this tag, and reconcile the code below against them if they differ.
- **Tests:** Catch2 under `tests/unit/`, added to `mosaic_unit_tests` in `tests/CMakeLists.txt`. `TEST_CASE` names must not start with `--`.
- **Editor is windowed-only:** `mosaic editor` always opens a window; there is no headless editor. It accepts `--duration`/`--ticks` for bounded runs (the functional-test seam).
- **Sandbox:** the GUI window cannot be screenshotted; verify `mosaic editor` functionally (exit 0, silent stderr) via a bounded `--duration` run.
- **Commits:** one per task; end messages with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

---

### Task 1: Dear ImGui dependency + `mosaic_imgui` static lib

**Files:**
- Modify: `CMakeLists.txt` (add FetchContent for ImGui + a `mosaic_imgui` static lib target)

**Interfaces:**
- Produces: a CMake target **`mosaic_imgui`** that exposes ImGui core headers (`imgui.h`) and the GLFW/Vulkan backend headers (`imgui_impl_glfw.h`, `imgui_impl_vulkan.h`) on its include path, and compiles ImGui core + both backends.

- [ ] **Step 1: Declare and build the ImGui lib**

In `CMakeLists.txt`, after the existing `FetchContent_MakeAvailable(glfw)` block and before `add_subdirectory(foundation)`, add:

```cmake
# --- Dear ImGui (docking branch): core + GLFW/Vulkan backends -----------------
find_package(Vulkan REQUIRED)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.5-docking
)
FetchContent_MakeAvailable(imgui)

add_library(mosaic_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(mosaic_imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(mosaic_imgui PUBLIC glfw Vulkan::Vulkan)
target_compile_features(mosaic_imgui PUBLIC cxx_std_20)
```

- [ ] **Step 2: Configure and build the target**

Run: `cmake --preset debug && cmake --build --preset debug --target mosaic_imgui`
Expected: ImGui downloads and `mosaic_imgui` compiles and archives with no errors.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "$(printf 'studio 37a: fetch Dear ImGui (docking) + mosaic_imgui static lib (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 2: `Command` enum + `mosaic editor` subcommand

**Files:**
- Modify: `app/cli/options.hpp` (add `Command` enum + field)
- Modify: `app/cli/options.cpp` (add the `editor` subcommand + selection)
- Test: `tests/unit/app/cli_options_test.cpp` (add cases)

**Interfaces:**
- Produces: `mosaic::app::Command { Run, Editor }`; `Options::command` (default `Command::Run`). `parse()` sets `command = Command::Editor` when the `editor` subcommand is given, and the `editor` subcommand accepts `--duration` and `--ticks`.

> Find the existing CLI test file first: `grep -rl "parse(" tests/unit/app` — it is the file that includes `app/cli/options.hpp` and builds an `argv`. The snippets below assume its existing `argv`-builder helper; match its local style.

- [ ] **Step 1: Write the failing tests**

Add to the existing CLI options test file (the one covering `parse`), following its existing helper for building `argv`:

```cpp
TEST_CASE("parse selects the run command by default subcommand", "[cli]")
{
    const char* argv[] = {"mosaic", "run"};
    auto pr = mosaic::app::parse(2, const_cast<char**>(argv));
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Run);
}

TEST_CASE("parse selects the editor command", "[cli]")
{
    const char* argv[] = {"mosaic", "editor"};
    auto pr = mosaic::app::parse(2, const_cast<char**>(argv));
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
}

TEST_CASE("editor accepts a bounded --duration", "[cli]")
{
    const char* argv[] = {"mosaic", "editor", "--duration", "1"};
    auto pr = mosaic::app::parse(4, const_cast<char**>(argv));
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.duration_s.has_value());
    REQUIRE(pr.options.duration_s.value() == 1.0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `build/debug/tests/mosaic_unit_tests "[cli]"`
Expected: FAIL — `Command` undefined / `editor` unknown subcommand.

- [ ] **Step 3: Add the `Command` enum to Options**

In `app/cli/options.hpp`, add above `struct Options`:

```cpp
// Which subcommand the user invoked.
enum class Command { Run, Editor };
```

and add as the first member of `struct Options`:

```cpp
    Command                      command = Command::Run;
```

- [ ] **Step 4: Add the editor subcommand to parse()**

In `app/cli/options.cpp`, inside `parse`, after the `run` subcommand's options are registered (after the `--fixed-hz` line) and before the `try`/`app.parse`, add the editor subcommand sharing the duration/ticks vars:

```cpp
    CLI::App* editor = app.add_subcommand("editor", "Open the Studio editor window");
    editor->add_option("--duration", duration_s, "Stop after N seconds");
    editor->add_option("--ticks", ticks, "Stop after N fixed ticks");
```

Note: `duration_s` and `ticks` are the variables the `run` subcommand already declared; reuse them (either subcommand writes them). Then, after `app.parse(...)` succeeds and before the existing `if (!scene.empty())` block, add the command selection:

```cpp
    if (editor->parsed())
    {
        result.options.command = Command::Editor;
    }
```

The existing `if (*duration_opt)` / `if (*ticks_opt)` blocks already copy the shared vars into the options; leave them. (When `editor` is used, the `run` subcommand's `duration_opt`/`ticks_opt` are not parsed, so replace those two guards to also accept the editor's options — change them to read the raw vars when either subcommand parsed them:)

```cpp
    if (run->parsed() ? static_cast<bool>(*duration_opt) : editor->count("--duration") > 0)
    {
        result.options.duration_s = duration_s;
    }
    if (run->parsed() ? static_cast<bool>(*ticks_opt) : editor->count("--ticks") > 0)
    {
        result.options.ticks = ticks;
    }
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[cli]"`
Expected: PASS (new cases + all pre-existing `[cli]` cases).

- [ ] **Step 6: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp tests/unit/app/cli_options_test.cpp
git commit -m "$(printf 'studio 37a: Command enum + mosaic editor subcommand (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 3: Neutral UI hooks on `RenderDevice` + Vulkan overrides

**Files:**
- Modify: `engine/render/render_device.hpp` (three default-no-op virtuals)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (store window; override hooks; ImGui init/shutdown; ImGui descriptor pool)
- Modify: `modules/render_vulkan/CMakeLists.txt` (link `mosaic_imgui`)

**Interfaces:**
- Consumes: `mosaic_imgui` (Task 1).
- Produces: `RenderDevice::init_ui()`, `begin_ui_frame()`, `end_ui_frame()` (default no-op; Vulkan overrides drive ImGui). `end_ui_frame()` must be called between `begin_frame()` and `end_frame()`.

> No unit test — GPU/ImGui code. Verified by a clean build and a green full suite (`FakeRenderDevice` inherits the no-ops, so it and headless `mosaic run` are unaffected). The functional launch is exercised in Task 5.

- [ ] **Step 1: Add the default-no-op hooks to the interface**

In `engine/render/render_device.hpp`, add to `class RenderDevice`'s public section (after `end_frame()`):

```cpp
    // Editor UI overlay hooks. Default no-ops so headless devices and test fakes
    // need not implement them. On a UI-capable backend, call init_ui() once after
    // construction, then per frame: begin_ui_frame() before building the UI and
    // end_ui_frame() between begin_frame() and end_frame() to record the UI.
    virtual void init_ui() {}
    virtual void begin_ui_frame() {}
    virtual void end_ui_frame() {}
```

- [ ] **Step 2: Link the backend against ImGui**

In `modules/render_vulkan/CMakeLists.txt`, add `mosaic_imgui` to the target's link libraries (find the existing `target_link_libraries(mosaic_render_vulkan ...)` and append `mosaic_imgui`).

- [ ] **Step 3: Store the window; add an ImGui descriptor pool member**

In `modules/render_vulkan/vulkan_render_device.cpp`: include the ImGui headers near the top (after the existing includes):

```cpp
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
```

Add two members alongside the other `VkDescriptorPool`/handle members:

```cpp
    GLFWwindow*      window_ = nullptr;
    VkDescriptorPool imgui_pool_ = VK_NULL_HANDLE;
    bool             ui_ready_ = false;
```

In the device constructor (the one taking the `GLFWwindow*`), store it: `window_ = window;` (match the constructor's parameter name).

- [ ] **Step 4: Implement the three overrides**

Add these methods to the Vulkan device class (public, near `end_frame`):

```cpp
    void init_ui() override
    {
        // Dedicated descriptor pool for ImGui (combined-image-samplers for its textures).
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = 64;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &imgui_pool_),
                 "vkCreateDescriptorPool(imgui)");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window_, true);

        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = instance_;
        info.PhysicalDevice = physical_;
        info.Device = device_;
        info.QueueFamily = graphics_family_;
        info.Queue = graphics_queue_;
        info.DescriptorPool = imgui_pool_;
        info.RenderPass = render_pass_;
        info.MinImageCount = kFramesInFlight;
        info.ImageCount = kFramesInFlight;
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        // NOTE: at v1.91.5-docking, ImGui_ImplVulkan_Init takes only &info (RenderPass is a
        // field) and uploads fonts lazily. If the pinned header differs, follow
        // backends/imgui_impl_vulkan.h + examples/example_glfw_vulkan/main.cpp at this tag.
        ImGui_ImplVulkan_Init(&info);

        ui_ready_ = true;
    }

    void begin_ui_frame() override
    {
        if (!ui_ready_) { return; }
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void end_ui_frame() override
    {
        if (!ui_ready_ || frame_skipped_) { return; }
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_[frame_]);
    }
```

(Use the class's actual member names for the instance/physical-device/graphics-queue handles — confirm via `grep -n "instance_\|physical_\|graphics_queue_\|graphics_family_\|frame_skipped_" modules/render_vulkan/vulkan_render_device.cpp`. If the graphics queue member has a different name, substitute it.)

- [ ] **Step 5: Shut ImGui down in the destructor**

In the device destructor / cleanup path (where other Vulkan objects are destroyed, after `vkDeviceWaitIdle` and before the device/pool teardown), add:

```cpp
        if (ui_ready_)
        {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            ui_ready_ = false;
        }
        if (imgui_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_, imgui_pool_, nullptr);
            imgui_pool_ = VK_NULL_HANDLE;
        }
```

- [ ] **Step 6: Build and run the full suite**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: build succeeds (backend links ImGui); full suite green — `FakeRenderDevice` compiles unchanged (it inherits the no-op hooks), and `mosaic run` is unaffected.

- [ ] **Step 7: Commit**

```bash
git add engine/render/render_device.hpp modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/CMakeLists.txt
git commit -m "$(printf 'studio 37a: RenderDevice UI hooks + Vulkan ImGui backend (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 4: `StudioLayout` model + `studio/` tier

**Files:**
- Create: `studio/studio_layout.hpp`
- Create: `studio/studio_layout.cpp`
- Create: `studio/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add `add_subdirectory(studio)` before `add_subdirectory(tests)`)
- Modify: `tests/CMakeLists.txt` (add the new test source + link `mosaic_studio`)
- Test: `tests/unit/studio/studio_layout_test.cpp`

**Interfaces:**
- Produces: `mosaic::studio::Panel { std::string name; bool open; }` and `mosaic::studio::StudioLayout` — `panels()`, `is_open(std::string_view)`, `toggle(std::string_view)`, `exit_requested()`, `request_exit()`. Default construction contains `Hierarchy`, `Inspector`, `Console`, all open, `exit_requested()==false`.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/studio/studio_layout_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "studio/studio_layout.hpp"

using mosaic::studio::StudioLayout;

TEST_CASE("StudioLayout default has the three panels, all open", "[studio]")
{
    StudioLayout layout;
    REQUIRE(layout.panels().size() == 3);
    REQUIRE(layout.is_open("Hierarchy"));
    REQUIRE(layout.is_open("Inspector"));
    REQUIRE(layout.is_open("Console"));
}

TEST_CASE("StudioLayout toggle flips a panel's open state", "[studio]")
{
    StudioLayout layout;
    layout.toggle("Inspector");
    REQUIRE_FALSE(layout.is_open("Inspector"));
    layout.toggle("Inspector");
    REQUIRE(layout.is_open("Inspector"));
}

TEST_CASE("StudioLayout exit request defaults false and can be set", "[studio]")
{
    StudioLayout layout;
    REQUIRE_FALSE(layout.exit_requested());
    layout.request_exit();
    REQUIRE(layout.exit_requested());
}

TEST_CASE("StudioLayout is_open is false for an unknown panel", "[studio]")
{
    StudioLayout layout;
    REQUIRE_FALSE(layout.is_open("Nope"));
}
```

- [ ] **Step 2: Create the header**

Create `studio/studio_layout.hpp`:

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mosaic::studio
{

struct Panel
{
    std::string name;
    bool        open;
};

// The editor shell's window state: which panels exist / are open, and whether the
// user asked to quit (File > Exit). Pure data + small mutators; no ImGui, no window.
class StudioLayout
{
public:
    StudioLayout();

    const std::vector<Panel>& panels() const { return panels_; }
    bool                      is_open(std::string_view name) const;
    void                      toggle(std::string_view name);

    bool exit_requested() const { return exit_; }
    void request_exit() { exit_ = true; }

private:
    std::vector<Panel> panels_;
    bool               exit_ = false;
};

} // namespace mosaic::studio
```

- [ ] **Step 3: Create the implementation**

Create `studio/studio_layout.cpp`:

```cpp
#include "studio/studio_layout.hpp"

namespace mosaic::studio
{

StudioLayout::StudioLayout()
    : panels_{{"Hierarchy", true}, {"Inspector", true}, {"Console", true}}
{
}

bool StudioLayout::is_open(std::string_view name) const
{
    for (const Panel& p : panels_)
    {
        if (p.name == name)
        {
            return p.open;
        }
    }
    return false;
}

void StudioLayout::toggle(std::string_view name)
{
    for (Panel& p : panels_)
    {
        if (p.name == name)
        {
            p.open = !p.open;
            return;
        }
    }
}

} // namespace mosaic::studio
```

- [ ] **Step 4: Create the studio CMake target**

Create `studio/CMakeLists.txt`:

```cmake
add_library(mosaic_studio STATIC
  studio_layout.cpp
)
target_link_libraries(mosaic_studio PUBLIC mosaic_imgui)
target_include_directories(mosaic_studio PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mosaic_studio PUBLIC cxx_std_20)
```

In the top-level `CMakeLists.txt`, add before `add_subdirectory(tests)`:

```cmake
add_subdirectory(studio)
```

- [ ] **Step 5: Register the test**

In `tests/CMakeLists.txt`, add `unit/studio/studio_layout_test.cpp` to the `mosaic_unit_tests` source list, and add `mosaic_studio` to its `target_link_libraries`.

- [ ] **Step 6: Run tests to verify pass**

Run: `cmake --preset debug && cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: PASS (4 cases).

- [ ] **Step 7: Commit**

```bash
git add studio/studio_layout.hpp studio/studio_layout.cpp studio/CMakeLists.txt CMakeLists.txt tests/CMakeLists.txt tests/unit/studio/studio_layout_test.cpp
git commit -m "$(printf 'studio 37a: StudioLayout model + studio tier (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 5: `StudioApp::draw_ui()` + `run_editor()` + main dispatch

**Files:**
- Create: `studio/studio_app.hpp`
- Create: `studio/studio_app.cpp`
- Modify: `studio/CMakeLists.txt` (add `studio_app.cpp`)
- Create: `app/run/editor.hpp`
- Create: `app/run/editor.cpp`
- Modify: `app/CMakeLists.txt` (add `run/editor.cpp`; link `mosaic_studio`)
- Modify: `app/main.cpp` (dispatch `Command::Editor` → `run_editor`)

**Interfaces:**
- Consumes: `StudioLayout` (Task 4); `RenderDevice::init_ui/begin_ui_frame/end_ui_frame` (Task 3); `Options`/`Command` (Task 2); existing `FramePump`/`PumpDeps` (`app/run/frame_pump.hpp`), `StatusReporter` (`app/run/status_reporter.hpp`), `create_vulkan_render_device` (`modules/render_vulkan/vulkan_render_device.hpp`), `RuntimeLoop`, `World`.
- Produces: `mosaic::studio::StudioApp` — `draw_ui()`, `exit_requested()`, `layout()`. `mosaic::app::run_editor(const Options&) -> int` (exit code).

> Functional deliverable — no unit test. Verified by a clean build and a bounded windowed launch (`mosaic editor --duration 1` exits 0, silent stderr).

- [ ] **Step 1: Create `StudioApp`**

Create `studio/studio_app.hpp`:

```cpp
#pragma once

#include "studio/studio_layout.hpp"

namespace mosaic::studio
{

// Draws the editor shell each frame from a StudioLayout: a full-window dockspace, a
// menu bar (File > Exit), and an empty window per open panel. ImGui glue only; the
// caller wraps draw_ui() between the device's begin_ui_frame()/end_ui_frame().
class StudioApp
{
public:
    void draw_ui();

    StudioLayout&       layout() { return layout_; }
    bool                exit_requested() const { return layout_.exit_requested(); }

private:
    StudioLayout layout_;
};

} // namespace mosaic::studio
```

Create `studio/studio_app.cpp`:

```cpp
#include "studio/studio_app.hpp"

#include <imgui.h>

namespace mosaic::studio
{

void StudioApp::draw_ui()
{
    // Full-window dockspace host so panels can be docked/tabbed/floated.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

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

    for (const Panel& panel : layout_.panels())
    {
        if (!panel.open)
        {
            continue;
        }
        if (ImGui::Begin(panel.name.c_str()))
        {
            ImGui::TextUnformatted("(empty)");
        }
        ImGui::End();
    }
}

} // namespace mosaic::studio
```

Add `studio_app.cpp` to `studio/CMakeLists.txt`'s `mosaic_studio` source list.

- [ ] **Step 2: Build the studio lib**

Run: `cmake --build --preset debug --target mosaic_studio`
Expected: compiles (ImGui `DockSpaceOverViewport` requires the docking branch from Task 1).

- [ ] **Step 3: Create `run_editor`**

Create `app/run/editor.hpp`:

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{

// Opens the Studio editor window and runs its frame loop until the window closes,
// File > Exit, --duration/--ticks, or SIGINT. Returns a process exit code.
int run_editor(const Options& options);

} // namespace mosaic::app
```

Create `app/run/editor.cpp`:

```cpp
#include "app/run/editor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

#include <GLFW/glfw3.h>

#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/render/render_device.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "studio/studio_app.hpp"

namespace mosaic::app
{
namespace
{
std::atomic<bool> g_editor_stop{false};
void on_editor_sigint(int) { g_editor_stop.store(true); }
} // namespace

int run_editor(const Options& options)
{
    std::unique_ptr<StatusReporter> reporter =
        options.json ? std::unique_ptr<StatusReporter>(std::make_unique<JsonStatusReporter>())
                     : std::unique_ptr<StatusReporter>(std::make_unique<TextStatusReporter>());

    if (glfwInit() != GLFW_TRUE)
    {
        reporter->error("glfwInit failed");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Mosaic Studio", nullptr, nullptr);
    if (window == nullptr)
    {
        reporter->error("glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }

    std::unique_ptr<engine::render::RenderDevice> device;
    try
    {
        device = mosaic::render::vk::create_vulkan_render_device(window);
    }
    catch (const std::exception& e)
    {
        reporter->error(std::string("render init failed: ") + e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    device->init_ui();

    std::signal(SIGINT, on_editor_sigint);
    g_editor_stop.store(false);

    mosaic::studio::StudioApp studio;

    engine::World              world;      // empty, idle: no systems registered
    engine::runtime::RuntimeLoop loop(static_cast<float>(1.0 / options.fixed_hz));

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    PumpDeps deps;
    deps.now = [start] {
        return std::chrono::duration<double>(clock::now() - start).count();
    };
    deps.sleep = [](double seconds) {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    };
    deps.stop_requested = [window, &studio] {
        return g_editor_stop.load() || studio.exit_requested()
               || glfwWindowShouldClose(window) == GLFW_TRUE;
    };
    deps.on_frame = [window, &device, &studio] {
        glfwPollEvents();
        device->begin_frame();
        device->clear(engine::render::Color{0.10f, 0.10f, 0.12f, 1.0f});
        device->begin_ui_frame();
        studio.draw_ui();
        device->end_ui_frame();
        device->end_frame();
    };

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);

    device.reset(); // device (and ImGui) destroyed before the window
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace mosaic::app
```

> The names above are verified against the codebase: `JsonStatusReporter`/`TextStatusReporter` (constructed exactly as `app/main.cpp` does), `PumpDeps{ now, sleep, stop_requested, on_frame }`, and `RuntimeLoop(float fixed_dt, ...)`. `FramePump(loop, std::move(deps), *reporter, options)` + `pump.run(world)` mirror `app/main.cpp`. If any signature has drifted since, mirror `app/main.cpp`'s current usage — it is the reference.

- [ ] **Step 4: Wire the app CMake + dispatch**

In `app/CMakeLists.txt`: add `run/editor.cpp` to the `mosaic_app` source list, and add `mosaic_studio` to `mosaic_app`'s `target_link_libraries`.

In `app/main.cpp`: add the include near the other app includes:

```cpp
#include "app/run/editor.hpp"
```

and immediately after the parse + `exit_code` early return (right after the `if (pr.exit_code) return *pr.exit_code;` block), add the dispatch:

```cpp
    if (pr.options.command == Command::Editor)
    {
        return mosaic::app::run_editor(pr.options);
    }
```

- [ ] **Step 5: Build and launch the editor (bounded)**

Run: `cmake --build --preset debug && build/debug/app/mosaic editor --duration 1; echo "exit=$?"`
Expected: build succeeds; the editor window opens, then exits after ~1s with `exit=0` and no error output on stderr.

- [ ] **Step 6: Confirm `mosaic run` still works and the suite is green**

Run: `build/debug/app/mosaic run --window --duration 1; echo "exit=$?" && ctest --preset debug`
Expected: `run --window` still exits 0 (no regression); full suite green.

- [ ] **Step 7: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp studio/CMakeLists.txt app/run/editor.hpp app/run/editor.cpp app/CMakeLists.txt app/main.cpp
git commit -m "$(printf 'studio 37a: StudioApp draw + run_editor + mosaic editor dispatch (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Self-Review

**Spec coverage:**
- `studio/` tier + `mosaic_studio` (ImGui-only deps) → Tasks 4, 5. ✓
- `StudioLayout` model (unit-testable) / `StudioApp::draw_ui` (glue) split → Tasks 4, 5. ✓
- `mosaic editor` subcommand + `Command` enum, `--duration`/`--ticks` → Task 2. ✓
- Neutral default-no-op UI hooks on `RenderDevice` + Vulkan overrides (ImGui init into existing render pass, dedicated descriptor pool, stored window, shutdown) → Task 3. ✓
- `FramePump` reuse, empty idle world, dockspace + menu bar + placeholder panels → Task 5. ✓
- Dear ImGui docking via FetchContent as `mosaic_imgui` (core + backends); backends only in the render module; studio links core → Tasks 1, 3, 4. ✓
- Error handling (glfw/window/device/init_ui failures → non-zero exit) → Task 5 `run_editor`. ✓
- Tests: `editor` parse (unit), `StudioLayout` (unit), `mosaic editor --duration 1` functional → Tasks 2, 4, 5. ✓
- Non-goals (hierarchy/inspector/viewport/play-stop content, scene loading, curated dock layout, theming) — nothing implemented. ✓

**Placeholder scan:** No TBD/TODO. The two "reconcile names against the codebase" notes (Tasks 3, 5) name the exact `grep` to run and give the real code structure — they resolve backend/util member names the plan cannot see from outside, not deferred logic.

**Type consistency:** `Command`/`Options::command` used identically in Task 2 (definition) and Task 5 (`pr.options.command == Command::Editor`). `StudioLayout` API (`panels/is_open/toggle/exit_requested/request_exit`) matches between Task 4 (definition + tests) and Task 5 (`StudioApp` uses `panels()`, `request_exit()`, `exit_requested()`). `RenderDevice` hook names (`init_ui/begin_ui_frame/end_ui_frame`) match between Task 3 (definition) and Task 5 (`run_editor` calls). `run_editor(const Options&) -> int` matches between Task 5 header and `main.cpp` dispatch.
