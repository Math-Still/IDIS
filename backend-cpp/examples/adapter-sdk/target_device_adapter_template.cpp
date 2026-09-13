// TEMPLATE ONLY — not compiled by the platform build.
// Copy this file into the target team's own shared-library project.
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_adapter_plugin.hpp"

#include <cstring>
#include <memory>
#include <string>

namespace {
// Host may call readSnapshot/execute/readCommandFeedback concurrently.
// Protect non-reentrant SDK state and use bounded I/O; do not rely on an
// unbounded mutex/driver call for safety-critical emergency preemption.
class TargetDeviceAdapter final : public smart_factory::DeviceAdapter {
 public:
  explicit TargetDeviceAdapter(std::string registryJson) : registryJson_(std::move(registryJson)) {}
  std::string name() const override { return "REPLACE-target-device-adapter"; }
  bool simulated() const override { return false; }
  bool start(std::string& detail) override {
    // TODO: parse registryJson_, open SDK/driver/protocol sessions, validate required devices.
    detail = "NOT IMPLEMENTED: target DeviceAdapter start()";
    return false; // fail closed until real field integration is implemented.
  }
  void stop() noexcept override { /* TODO release SDK/driver resources */ }
  smart_factory::DeviceAdapterHealth health() const override { return {false,"OFFLINE","NOT IMPLEMENTED",0,0}; }
  smart_factory::DeviceDataSnapshot readSnapshot() override { return {}; }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override {
    return {false,"NOT IMPLEMENTED"};
  }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override {
    return {smart_factory::DeviceFeedbackState::Unsupported,"NOT IMPLEMENTED","null"};
  }
 private:
  std::string registryJson_;
};
}

extern "C" smart_factory::DeviceAdapter* smart_factory_create_device_adapter_v1(
    const char* registryJson, const char*, char* errorBuffer, std::size_t errorBufferSize) {
  try {
    return new TargetDeviceAdapter(registryJson ? registryJson : "{}");
  } catch (const std::exception& e) {
    if (errorBuffer && errorBufferSize) {
      std::strncpy(errorBuffer, e.what(), errorBufferSize - 1);
      errorBuffer[errorBufferSize - 1] = '\0';
    }
    return nullptr;
  }
}

extern "C" void smart_factory_destroy_device_adapter_v1(smart_factory::DeviceAdapter* adapter) { delete adapter; }
