add_library(mosaic_serialization STATIC
  json_backend.cpp
)
target_include_directories(mosaic_serialization PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_serialization
  PUBLIC mosaic_foundation
  PRIVATE tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json
)
target_compile_features(mosaic_serialization PUBLIC cxx_std_20)
