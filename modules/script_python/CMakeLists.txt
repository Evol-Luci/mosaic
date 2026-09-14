add_library(mosaic_script_python STATIC python_script_engine.cpp world_globals.cpp)
target_include_directories(mosaic_script_python PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_script_python
  PUBLIC  mosaic_engine
  PRIVATE pybind11::embed mosaic_foundation)
target_compile_features(mosaic_script_python PUBLIC cxx_std_20)

# The embedded interpreter can't auto-locate its standard library relative to the
# host binary, so bake the linked Python's install prefix in as its home (a
# PYTHONHOME env var overrides it at runtime). Without this, CPython prints
# "Could not find platform dependent libraries <exec_prefix>" and pure-Python
# stdlib imports (json, os, ...) fail; built-in modules (math, sys) still work.
execute_process(
  COMMAND ${Python3_EXECUTABLE} -c "import sys; print(sys.prefix)"
  OUTPUT_VARIABLE MOSAIC_PYTHON_HOME
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(MOSAIC_PYTHON_HOME)
  target_compile_definitions(mosaic_script_python
    PRIVATE MOSAIC_PYTHON_HOME="${MOSAIC_PYTHON_HOME}")
endif()
