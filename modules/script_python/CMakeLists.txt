add_library(mosaic_script_python STATIC python_script_engine.cpp)
target_include_directories(mosaic_script_python PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_script_python
  PUBLIC  mosaic_engine
  PRIVATE pybind11::embed mosaic_foundation)
target_compile_features(mosaic_script_python PUBLIC cxx_std_20)
