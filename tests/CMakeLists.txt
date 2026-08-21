add_executable(solar_edge_tests
  test_config_loader.cpp
  test_config_malleability.cpp
  test_event_queue.cpp
  test_fsm_boot.cpp
  test_fsm_cleaning_cycle.cpp
  test_fsm_estop.cpp
  test_fsm_network_resilience.cpp
  test_mock_hardware_driver.cpp
  test_mock_vision_engine.cpp
  test_serial_driver.cpp
  test_telemetry_cache.cpp
  test_application_boot.cpp
)

target_link_libraries(solar_edge_tests PRIVATE daemon_core Catch2::Catch2WithMain)
target_compile_definitions(solar_edge_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(solar_edge_tests)
