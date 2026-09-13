#pragma once

#include "smart_factory/config.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/os_adapter.hpp"

#include <memory>

namespace smart_factory {

std::unique_ptr<DeviceAdapter> makeConfiguredDeviceAdapter(
    const BackendConfig& config, std::shared_ptr<const DeviceRegistry> registry);
std::unique_ptr<OsAdapter> makeConfiguredOsAdapter(const BackendConfig& config);

}  // namespace smart_factory
