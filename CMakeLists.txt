cmake_minimum_required(VERSION 3.20)
project(mosaic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(tomlplusplus nlohmann_json CLI11)

find_package(Threads REQUIRED)

find_package(Vulkan REQUIRED)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- Box3D (physics backend; alpha, pinned to a commit) -----------------------
FetchContent_Declare(
  box3d
  GIT_REPOSITORY https://github.com/erincatto/box3d.git
  GIT_TAG        30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c
)
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box3d)

# --- miniaudio (audio backend; single header, pinned) -------------------------
FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.21
)
FetchContent_MakeAvailable(miniaudio)

# --- ENet (real UDP transport backend; pinned) --------------------------------
FetchContent_Declare(
  enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG        v1.3.18
)
FetchContent_MakeAvailable(enet)

# --- Dear ImGui (docking branch): core + GLFW/Vulkan backends -----------------
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

# --- Python scripting (CPython embedded via pybind11) -------------------------
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.13.6
)
# Force pybind11 to reuse the Python3 already found above (avoids a second
# Python detection via FindPythonLibsNew which can pick a different version).
set(PYBIND11_FINDPYTHON ON)
FetchContent_MakeAvailable(pybind11)

add_subdirectory(foundation)
add_subdirectory(engine)
add_subdirectory(modules/render_vulkan)
add_subdirectory(modules/physics_box3d)
add_subdirectory(modules/script_python)
add_subdirectory(modules/script_python_studio)
add_subdirectory(modules/audio_miniaudio)
add_subdirectory(modules/net_enet)
add_subdirectory(app)
add_subdirectory(studio)
add_subdirectory(tests)
