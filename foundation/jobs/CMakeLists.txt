add_library(mosaic_jobs STATIC
  thread_pool.cpp
)
target_include_directories(mosaic_jobs PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_jobs
  PUBLIC mosaic_foundation
  PRIVATE Threads::Threads
)
target_compile_features(mosaic_jobs PUBLIC cxx_std_20)
