add_library(mosaic_audio_miniaudio STATIC miniaudio_backend.cpp)
target_include_directories(mosaic_audio_miniaudio
  PUBLIC  ${CMAKE_SOURCE_DIR}
  PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(mosaic_audio_miniaudio
  PUBLIC  mosaic_engine
  PRIVATE mosaic_foundation Threads::Threads ${CMAKE_DL_LIBS} m)
target_compile_features(mosaic_audio_miniaudio PUBLIC cxx_std_20)
