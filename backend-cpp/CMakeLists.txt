cmake_minimum_required(VERSION 3.20)
project(smart_factory_backend VERSION 0.10.0 LANGUAGES CXX)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SMART_FACTORY_BUILD_TESTS "Build backend smoke tests" ON)
option(SMART_FACTORY_ENABLE_PYTHON_BRIDGE "Build optional pybind11 bridge" OFF)
option(SMART_FACTORY_REQUIRE_POSIX "Require POSIX runtime adapter" ON)
option(SMART_FACTORY_ENABLE_SIMULATED_ADAPTER "Compile development-only simulated DeviceAdapter" ON)
option(SMART_FACTORY_BUILD_TARGET_DIAGNOSTIC "Build target-environment diagnostic CLI" ON)

find_package(Threads REQUIRED)
find_package(Boost 1.75 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

set(SMART_FACTORY_CORE_SOURCES
  src/config.cpp
  src/runtime/executor.cpp
  src/os/posix_os_adapter.cpp
  src/integration/device_registry.cpp
  src/integration/integration_factory.cpp
  src/historian.cpp
  src/command_ledger.cpp
  src/security.cpp
  src/platform/config_service.cpp
  src/platform/quality_service.cpp
  src/platform/acquisition_service.cpp
  src/platform/rule_service.cpp
  src/platform/alarm_service.cpp
  src/platform/incident_service.cpp
  src/platform/command_service.cpp
  src/platform/query_service.cpp
  src/platform/replay_service.cpp
  src/platform/report_service.cpp
  src/platform/agent_client.cpp
  src/application.cpp
  src/json_impl.cpp
)
if(SMART_FACTORY_ENABLE_SIMULATED_ADAPTER)
  list(APPEND SMART_FACTORY_CORE_SOURCES src/device/simulated_device_adapter.cpp)
endif()

add_library(smart_factory_core ${SMART_FACTORY_CORE_SOURCES})
target_include_directories(smart_factory_core PUBLIC include ${Boost_INCLUDE_DIRS})
target_compile_definitions(smart_factory_core PUBLIC
  BOOST_ERROR_CODE_HEADER_ONLY
  BOOST_SYSTEM_NO_LIB
  SMART_FACTORY_ENABLE_SIMULATED_ADAPTER=$<BOOL:${SMART_FACTORY_ENABLE_SIMULATED_ADAPTER}>
)
target_link_libraries(smart_factory_core PUBLIC Threads::Threads SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})

add_library(smart_factory_transport
  src/transport/http_server.cpp
)
target_include_directories(smart_factory_transport PUBLIC include ${Boost_INCLUDE_DIRS})
target_compile_definitions(smart_factory_transport PUBLIC BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_LIB)
target_link_libraries(smart_factory_transport PUBLIC smart_factory_core Threads::Threads)

add_executable(smart-factory-backend src/main.cpp)
target_link_libraries(smart-factory-backend PRIVATE smart_factory_transport)

add_executable(smart-factory-storage-maintenance src/storage_maintenance.cpp)
target_link_libraries(smart-factory-storage-maintenance PRIVATE smart_factory_core)

if(SMART_FACTORY_BUILD_TARGET_DIAGNOSTIC)
  add_executable(smart-factory-target-diagnostic src/target_diagnostic.cpp)
  target_link_libraries(smart-factory-target-diagnostic PRIVATE smart_factory_core)
endif()

if(SMART_FACTORY_BUILD_TESTS)
  enable_testing()
  add_executable(smart-factory-backend-smoke tests/backend_smoke.cpp)
  target_link_libraries(smart-factory-backend-smoke PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-backend-smoke PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME backend_smoke COMMAND smart-factory-backend-smoke)

  add_executable(smart-factory-command-runtime-alpha2 tests/command_runtime_alpha2.cpp)
  target_link_libraries(smart-factory-command-runtime-alpha2 PRIVATE smart_factory_core)
  add_test(NAME command_runtime_alpha2 COMMAND smart-factory-command-runtime-alpha2)

  add_executable(smart-factory-alarm-storage-beta1 tests/alarm_storage_beta1.cpp)
  target_link_libraries(smart-factory-alarm-storage-beta1 PRIVATE smart_factory_core)
  add_test(NAME alarm_storage_beta1 COMMAND smart-factory-alarm-storage-beta1)

  add_executable(smart-factory-transport-sampling-beta2 tests/transport_sampling_beta2.cpp)
  target_link_libraries(smart-factory-transport-sampling-beta2 PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-transport-sampling-beta2 PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME transport_sampling_beta2 COMMAND smart-factory-transport-sampling-beta2)

  add_executable(smart-factory-platform-config-b1 tests/platform_config_b1.cpp)
  target_link_libraries(smart-factory-platform-config-b1 PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-platform-config-b1 PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME platform_config_b1 COMMAND smart-factory-platform-config-b1)

  add_executable(smart-factory-platform-acquisition-b2 tests/platform_acquisition_b2.cpp)
  target_link_libraries(smart-factory-platform-acquisition-b2 PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-platform-acquisition-b2 PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME platform_acquisition_b2 COMMAND smart-factory-platform-acquisition-b2)

  add_executable(smart-factory-platform-rule-alarm-incident-b3 tests/platform_rule_alarm_incident_b3.cpp)
  target_link_libraries(smart-factory-platform-rule-alarm-incident-b3 PRIVATE smart_factory_core)
  add_test(NAME platform_rule_alarm_incident_b3 COMMAND smart-factory-platform-rule-alarm-incident-b3)

  add_executable(smart-factory-platform-vertical-b3 tests/platform_vertical_b3.cpp)
  target_link_libraries(smart-factory-platform-vertical-b3 PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-platform-vertical-b3 PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME platform_vertical_b3 COMMAND smart-factory-platform-vertical-b3)

  add_executable(smart-factory-platform-command-b4 tests/platform_command_b4.cpp)
  target_link_libraries(smart-factory-platform-command-b4 PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-platform-command-b4 PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME platform_command_b4 COMMAND smart-factory-platform-command-b4)

  add_executable(smart-factory-platform-application-b5 tests/platform_application_b5.cpp)
  target_link_libraries(smart-factory-platform-application-b5 PRIVATE smart_factory_core)
  target_compile_definitions(smart-factory-platform-application-b5 PRIVATE SMART_FACTORY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME platform_application_b5 COMMAND smart-factory-platform-application-b5)
endif()

if(SMART_FACTORY_ENABLE_PYTHON_BRIDGE)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(smart_factory_native bindings/python/module.cpp)
  target_link_libraries(smart_factory_native PRIVATE smart_factory_core)
endif()
