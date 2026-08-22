add_library(mosaic_render_vulkan STATIC
  vulkan_swapchain_support.cpp
  vulkan_render_device.cpp
)
target_include_directories(mosaic_render_vulkan PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_render_vulkan
  PUBLIC  mosaic_engine
  PRIVATE Vulkan::Vulkan glfw mosaic_foundation mosaic_imgui
)
target_compile_features(mosaic_render_vulkan PUBLIC cxx_std_20)

# --- SPIR-V shader compilation ------------------------------------------------
find_program(GLSLC glslc REQUIRED
  DOC "glslc shader compiler (ships with the Vulkan SDK)")

set(_shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${_shader_dir})
foreach(_stage cube.vert cube.frag)
  add_custom_command(
    OUTPUT  ${_shader_dir}/${_stage}.spv
    COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_stage} -o ${_shader_dir}/${_stage}.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_stage}
    COMMENT "Compiling ${_stage} -> SPIR-V")
  list(APPEND _spv ${_shader_dir}/${_stage}.spv)
endforeach()
add_custom_target(mosaic_render_vulkan_shaders DEPENDS ${_spv})
add_dependencies(mosaic_render_vulkan mosaic_render_vulkan_shaders)
target_compile_definitions(mosaic_render_vulkan PRIVATE
  MOSAIC_SHADER_DIR="${_shader_dir}")
