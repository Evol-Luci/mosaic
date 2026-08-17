add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
)
target_link_libraries(mosaic_app PUBLIC
  mosaic_engine
  mosaic_foundation
  mosaic_serialization
  CLI11::CLI11
  nlohmann_json::nlohmann_json
)
target_include_directories(mosaic_app PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mosaic_app PUBLIC cxx_std_20)
