add_executable(mosaic_unit_tests
  unit/ecs/bootstrap_test.cpp
  unit/ecs/entity_id_test.cpp
  unit/ecs/entity_registry_test.cpp
  unit/ecs/sparse_set_test.cpp
  unit/ecs/component_registry_test.cpp
  unit/ecs/system_test.cpp
  unit/serialization/value_test.cpp
)
target_link_libraries(mosaic_unit_tests PRIVATE
  mosaic_engine
  mosaic_foundation
  Catch2::Catch2WithMain
)
target_compile_features(mosaic_unit_tests PRIVATE cxx_std_20)

include(Catch)
catch_discover_tests(mosaic_unit_tests)
