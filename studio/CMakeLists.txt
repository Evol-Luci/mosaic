add_library(mosaic_studio STATIC
  studio_layout.cpp
  studio_app.cpp
)
target_link_libraries(mosaic_studio PRIVATE mosaic_imgui)
target_include_directories(mosaic_studio PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mosaic_studio PUBLIC cxx_std_20)
