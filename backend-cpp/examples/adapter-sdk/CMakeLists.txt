cmake_minimum_required(VERSION 3.20)
project(smart_factory_adapter_sdk_templates LANGUAGES CXX)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.74 REQUIRED)

set(SMART_FACTORY_HOST_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../include")

function(sf_adapter_target target source)
  add_library(${target} SHARED ${source} boost_json_plugin_impl.cpp)
  target_include_directories(${target} PRIVATE "${SMART_FACTORY_HOST_INCLUDE}" ${Boost_INCLUDE_DIRS})
  target_compile_definitions(${target} PRIVATE BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_LIB)
  if(UNIX AND NOT APPLE)
    target_link_options(${target} PRIVATE -Wl,-z,defs)
  endif()
endfunction()

sf_adapter_target(sf_target_device_adapter_template target_device_adapter_template.cpp)
sf_adapter_target(sf_target_os_adapter_template target_os_adapter_template.cpp)

add_library(sf_protocol_adapter_template OBJECT protocol_adapter_template.cpp)
target_include_directories(sf_protocol_adapter_template PRIVATE "${SMART_FACTORY_HOST_INCLUDE}" ${Boost_INCLUDE_DIRS})
target_compile_definitions(sf_protocol_adapter_template PRIVATE BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_LIB)
