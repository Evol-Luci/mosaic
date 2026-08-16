add_library(mosaic_engine STATIC
  ecs/entity_registry.cpp
  world/world.cpp
)
target_include_directories(mosaic_engine PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_engine PUBLIC mosaic_foundation)
target_compile_features(mosaic_engine PUBLIC cxx_std_20)
