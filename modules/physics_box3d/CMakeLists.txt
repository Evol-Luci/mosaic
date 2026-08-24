add_library(mosaic_physics_box3d STATIC box3d_backend.cpp)
target_include_directories(mosaic_physics_box3d PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_physics_box3d
  PUBLIC  mosaic_engine
  PRIVATE box3d::box3d mosaic_foundation)
target_compile_features(mosaic_physics_box3d PUBLIC cxx_std_20)
