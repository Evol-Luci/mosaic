add_library(mosaic_net_enet STATIC enet_transport.cpp)
target_include_directories(mosaic_net_enet
  PUBLIC  ${CMAKE_SOURCE_DIR}
  PRIVATE ${enet_SOURCE_DIR}/include)
target_link_libraries(mosaic_net_enet
  PUBLIC  mosaic_engine
  PRIVATE enet)
target_compile_features(mosaic_net_enet PUBLIC cxx_std_20)
