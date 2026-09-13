#pragma once

#include "smart_factory/os_adapter.hpp"
#include <cstddef>

extern "C" {
using SmartFactoryCreateOsAdapterV1 = smart_factory::OsAdapter* (*)(
    const char* optionsJson, char* errorBuffer, std::size_t errorBufferSize);
using SmartFactoryDestroyOsAdapterV1 = void (*)(smart_factory::OsAdapter*);
}
