add_library(mosaic_render_vulkan STATIC
  vulkan_swapchain_support.cpp
  vulkan_render_device.cpp
)
target_include_directories(mosaic_render_vulkan PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_render_vulkan
  PUBLIC  mosaic_engine
  PRIVATE Vulkan::Vulkan glfw mosaic_foundation
)
target_compile_features(mosaic_render_vulkan PUBLIC cxx_std_20)
