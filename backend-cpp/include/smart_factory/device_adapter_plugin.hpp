#pragma once

#include "smart_factory/device_adapter.hpp"
#include <cstddef>

// C ABI entry points for a target DeviceAdapter shared library. The plugin
// and host must be built with ABI-compatible C++ toolchains because the
// returned object implements the C++ DeviceAdapter interface.
extern "C" {
using SmartFactoryCreateDeviceAdapterV1 = smart_factory::DeviceAdapter* (*)(
    const char* registryJson, const char* optionsJson, char* errorBuffer, std::size_t errorBufferSize);
using SmartFactoryDestroyDeviceAdapterV1 = void (*)(smart_factory::DeviceAdapter*);
}
