add_executable(mosaic_unit_tests
  unit/ecs/bootstrap_test.cpp
  unit/ecs/entity_id_test.cpp
  unit/ecs/entity_registry_test.cpp
  unit/ecs/sparse_set_test.cpp
  unit/ecs/component_registry_test.cpp
  unit/ecs/component_registry_traits_test.cpp
  unit/ecs/system_test.cpp
  unit/ecs/sparse_set_iteration_test.cpp
  unit/ecs/component_registry_iteration_test.cpp
  unit/ecs/entity_registry_iteration_test.cpp
  unit/world/world_test.cpp
  unit/world/world_tick_test.cpp
  unit/world/world_destroy_test.cpp
  unit/world/world_integration_test.cpp
  unit/serialization/value_test.cpp
  unit/serialization/reference_test.cpp
  unit/serialization/reflect_value_test.cpp
  unit/serialization/reference_encoding_test.cpp
  unit/serialization/json_backend_test.cpp
  unit/serialization/toml_backend_test.cpp
  unit/serialization/cross_format_test.cpp
  unit/serialization/unsigned_roundtrip_test.cpp
  unit/events/event_bus_test.cpp
  unit/commands/command_stack_test.cpp
  unit/commands/command_event_integration_test.cpp
  unit/commands/set_component_command_test.cpp
  unit/commands/composite_command_test.cpp
  unit/reflection/type_id_test.cpp
  unit/reflection/type_registry_test.cpp
  unit/reflection/component_reflection_test.cpp
  unit/reflection/builtin_components_test.cpp
  unit/reflection/camera_reflection_test.cpp
  unit/reflection/field_visitor_test.cpp
  unit/reflection/component_inspect_test.cpp
  unit/reflection/field_asset_test.cpp
  unit/reflection/field_handle_test.cpp
  unit/scene/scene_save_test.cpp
  unit/scene/scene_load_test.cpp
  unit/assets/asset_id_hash_test.cpp
  unit/assets/asset_metadata_test.cpp
  unit/assets/asset_registry_test.cpp
  unit/assets/asset_resolver_test.cpp
  unit/assets/asset_enumeration_test.cpp
  unit/math/vec2_test.cpp
  unit/math/vec3_test.cpp
  unit/math/quat_test.cpp
  unit/math/mat4_test.cpp
  unit/math/inverse_affine_test.cpp
  unit/math/transform_direction_test.cpp
  unit/math/projection_test.cpp
  unit/transform/transform_reflection_test.cpp
  unit/transform/transform_system_test.cpp
  unit/runtime/runtime_loop_test.cpp
  unit/runtime/runtime_stability_test.cpp
  unit/logging/logger_test.cpp
  unit/logging/log_api_test.cpp
  unit/logging/log_thread_safety_test.cpp
  unit/app/options_test.cpp
  unit/app/window_pointer_test.cpp
  unit/app/editor_play_policy_test.cpp
  unit/app/component_reference_test.cpp
  unit/app/mcp_tools_test.cpp
  unit/app/mcp_protocol_test.cpp
  unit/app/scripting_globals_reference_test.cpp
  unit/app/status_reporter_test.cpp
  unit/app/frame_pump_test.cpp
  unit/app/demo_scene_test.cpp
  unit/app/click_nudge_test.cpp
  unit/app/windowed_systems_test.cpp
  unit/app/net_host_demo_test.cpp
  unit/app/script_runtime_test.cpp
  unit/app/headless_run_test.cpp
  unit/app/play_from_script_test.cpp
  unit/app/scene_io_test.cpp
  unit/app/populate_world_test.cpp
  unit/app/scene_save_roundtrip_test.cpp
  unit/app/project_manifest_test.cpp
  unit/app/project_create_test.cpp
  unit/app/project_resolve_test.cpp
  unit/app/bundle_test.cpp
  unit/app/image_cook_test.cpp
  unit/app/cubemap_cook_test.cpp
  unit/app/ibl_bake_test.cpp
  unit/app/asset_import_test.cpp
  unit/app/import_plugins_test.cpp
  unit/app/package_source_test.cpp
  unit/app/package_install_test.cpp
  unit/app/package_sources_registry_test.cpp
  unit/app/studio_prefs_test.cpp
  unit/app/git_test.cpp
  unit/jobs/thread_pool_test.cpp
  unit/render/swapchain_support_test.cpp
  unit/render/bloom_levels_test.cpp
  unit/render/vulkan_validation_contract_test.cpp
  unit/render/bloom_shader_contract_test.cpp
  unit/render/bloom_backend_contract_test.cpp
  unit/render/render_system_test.cpp
  unit/render/texture_cache_test.cpp
  unit/render/mesh_cache_test.cpp
  unit/render/builtin_meshes_test.cpp
  unit/render/mesh_renderer_reflection_test.cpp
  unit/render/shape_meshes_test.cpp
  unit/render/render_device_hooks_test.cpp
  unit/render/shader_specular_contract_test.cpp
  unit/render/texture_colorspace_contract_test.cpp
  unit/render/ui_shader_contract_test.cpp
  unit/render/mip_levels_test.cpp
  unit/render/shader_tonemap_contract_test.cpp
  unit/render/shadow_test.cpp
  unit/reflection/lighting_reflection_test.cpp
  unit/reflection/component_value_test.cpp
  unit/studio/studio_layout_test.cpp
  unit/studio/selection_test.cpp
  unit/studio/hierarchy_test.cpp
  unit/studio/live_link_test.cpp
  unit/studio/live_link_server_test.cpp
  unit/studio/live_link_label_test.cpp
  unit/studio/viewport_test.cpp
  unit/studio/play_state_test.cpp
  unit/studio/scene_file_test.cpp
  unit/studio/order_warnings_test.cpp
  unit/studio/console_test.cpp
  unit/studio/script_console_test.cpp
  unit/studio/script_editor_test.cpp
  unit/studio/pick_test.cpp
  unit/studio/transform_controls_test.cpp
  unit/studio/demo_physics_test.cpp
  unit/studio/asset_browser_test.cpp
  unit/studio/plugin_host_test.cpp
  unit/studio/plugin_file_watcher_test.cpp
  unit/physics/physics_backend_test.cpp
  unit/physics/physics_destroy_body_test.cpp
  unit/physics/falling_box_test.cpp
  unit/physics/physics_components_test.cpp
  unit/physics/physics_system_test.cpp
  unit/physics/physics_concurrency_test.cpp
  unit/physics/physics_contact_reentry_test.cpp
  unit/physics/physics_raycast_test.cpp
  unit/physics/physics_contacts_test.cpp
  unit/script/script_engine_test.cpp
  unit/script/script_gil_test.cpp
  unit/script/world_globals_test.cpp
  unit/script/script_world_test.cpp
  unit/script/value_marshal_test.cpp
  unit/script/script_entities_test.cpp
  unit/script/script_commands_test.cpp
  unit/script/script_system_test.cpp
  unit/script/script_hotreload_test.cpp
  unit/script/script_host_test.cpp
  unit/script/script_asset_test.cpp
  unit/script/script_context_test.cpp
  unit/script/script_context_engine_test.cpp
  unit/script/script_worker_test.cpp
  unit/script/script_stdlib_test.cpp
  unit/script/script_authoring_test.cpp
  unit/script/script_raycast_test.cpp
  unit/script/script_apply_impulse_test.cpp
  unit/script/script_events_test.cpp
  unit/script/script_profiler_test.cpp
  unit/script/script_debugger_test.cpp
  unit/script/script_debug_engine_test.cpp
  unit/script/python_script_engine_test.cpp
  unit/script/studio_plugin_script_engine_test.cpp
  unit/script/imgui_globals_test.cpp
  unit/animation/animation_clip_test.cpp
  unit/animation/animation_player_test.cpp
  unit/animation/animation_system_test.cpp
  unit/sequencer/sequence_test.cpp
  unit/sequencer/sequence_system_test.cpp
  unit/navigation/nav_grid_test.cpp
  unit/navigation/navigation_system_test.cpp
  unit/particles/particle_test.cpp
  unit/particles/particle_system_test.cpp
  unit/audio/audio_components_test.cpp
  unit/audio/audio_system_test.cpp
  unit/audio/miniaudio_backend_test.cpp
  unit/app/audio_cook_test.cpp
  unit/app/audio_asset_test.cpp
  unit/ui/ui_rect_test.cpp
  unit/ui/ui_fit_test.cpp
  unit/ui/ui_hit_test.cpp
  unit/ui/ui_order_check_test.cpp
  unit/ui/ui_button_rects_test.cpp
  unit/ui/ui_order_conflict_test.cpp
  unit/ui/ui_hover_style_test.cpp
  unit/ui/ui_hover_style_reflection_test.cpp
  unit/ui/ui_hierarchy_test.cpp
  unit/ui/ui_stack_test.cpp
  unit/ui/ui_layout_test.cpp
  unit/ui/ui_parent_reflection_test.cpp
  unit/ui/ui_panel_reflection_test.cpp
  unit/ui/ui_image_reflection_test.cpp
  unit/ui/ui_button_reflection_test.cpp
  unit/ui/ui_system_test.cpp
  unit/ui/text_layout_test.cpp
  unit/ui/ui_text_reflection_test.cpp
  unit/ui/world_label_reflection_test.cpp
  unit/ui/world_text_test.cpp
  unit/app/font_cook_test.cpp
  unit/app/font_asset_test.cpp
  unit/net/loopback_transport_test.cpp
  unit/net/enet_transport_test.cpp
  unit/net/message_test.cpp
  unit/net/network_system_test.cpp
  unit/net/input_system_test.cpp
  unit/net/net_replicated_test.cpp
  unit/net/replication_test.cpp
  unit/net/replication_system_test.cpp
  unit/vfx/trail_system_test.cpp
  unit/vfx/ribbon_test.cpp
  unit/vfx/decal_test.cpp
  unit/vfx/screen_effect_test.cpp
  unit/vfx/bloom_test.cpp
)
target_link_libraries(mosaic_unit_tests PRIVATE
  mosaic_app
  mosaic_engine
  mosaic_foundation
  mosaic_serialization
  mosaic_jobs
  mosaic_render_vulkan
  mosaic_physics_box3d
  mosaic_script_python
  mosaic_script_python_studio
  mosaic_audio_miniaudio
  mosaic_net_enet
  pybind11::embed
  mosaic_studio
  Vulkan::Vulkan
  Catch2::Catch2WithMain
)
target_compile_features(mosaic_unit_tests PRIVATE cxx_std_20)
target_include_directories(mosaic_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit/serialization)
target_compile_definitions(mosaic_unit_tests PRIVATE
  MOSAIC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(mosaic_unit_tests PRIVATE ${MOSAIC_PCH_HEADERS})

include(Catch)
# TIMEOUT 60 is a BACKSTOP, not a performance budget, and the number is chosen against
# measurement rather than taste.
#
# Without it ctest's default of 1500s applies, so any test that hangs costs 25 minutes of
# CI before failing, and says only "Timeout" when it does. This session's predecessors
# produced two hang-shaped bugs -- a watchdog use-after-free and a GIL deadlock -- so the
# scenario is observed, not hypothetical.
#
# Measured 2026-10-07 over a full run the slowest case was 1.11s and the suite took 24s, so 60s
# was ~54x the slowest. A regression in #241 later took `the default scene's HUD underlay image
# draws beneath the checker` to 16.5s locally and 23.4s on CI (2026-10-21, image 20261012.334.1,
# ~2.6x headroom), and that was fixed in #274 (it now runs in 0.18s). Re-measured 2026-10-24,
# the slowest case in the suite is `PluginFileWatcher.poll detects a modified file's mtime` at
# 1.13s, so headroom against 60s is back to ~53x. The per-case performance budget added in
# `tools/ci/check_ctest_run.py` (`MAX_CASE_SECONDS = 10.0`, observation O3) is what now catches
# that regression class early instead of relying on someone noticing. A comment whose job is
# "nothing is wrong here" must not predict the wrong margin. Do NOT raise TIMEOUT to make room:
# under `ctest -j` a case nearing 60s is the regression to see. The slice that made ctest
# parallel (docs/superpowers/specs/2026-10-21-ci-parallel-ctest-design.md) treats a longest
# case above 40s as a ship-blocker.
#
# This does NOT replace the in-test watchdogs. A watchdog gives a named assertion in
# seconds and works when running the test binary directly; a ctest property does neither.
# It catches the next hang nobody anticipated, which is the case no watchdog exists for.
#
# `catch_discover_tests` applies PROPERTIES to the discovered tests as a whole, and gives
# each case its own process, so one case timing out cannot take another down.
catch_discover_tests(mosaic_unit_tests PROPERTIES TIMEOUT 60)

option(MOSAIC_ENABLE_GPU_TESTS "Build and register GPU tests that require a Vulkan physical device" OFF)
if(MOSAIC_ENABLE_GPU_TESTS)
  add_executable(mosaic_gpu_tests
    gpu/headless_device_test.cpp
  )
  target_link_libraries(mosaic_gpu_tests PRIVATE
    mosaic_render_vulkan
    mosaic_foundation
    Vulkan::Vulkan
    Catch2::Catch2WithMain
  )
  target_compile_features(mosaic_gpu_tests PRIVATE cxx_std_20)
  target_include_directories(mosaic_gpu_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_precompile_headers(mosaic_gpu_tests PRIVATE ${MOSAIC_PCH_HEADERS})
  catch_discover_tests(mosaic_gpu_tests PROPERTIES TIMEOUT 60)
endif()
