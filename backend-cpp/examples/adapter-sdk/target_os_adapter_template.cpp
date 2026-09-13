// TEMPLATE ONLY — not compiled by the platform build.
#include "smart_factory/os_adapter.hpp"
#include "smart_factory/os_adapter_plugin.hpp"
#include <cstring>

namespace {
class TargetOsAdapter final : public smart_factory::OsAdapter {
 public:
  std::string name() const override { return "REPLACE-target-os"; }
  std::uint64_t monotonicNowNs() const override { return 0; /* TODO vendor monotonic clock */ }
  smart_factory::OsCapabilities inspectCapabilities() const override {
    return {false,false,false,false,false,"REPLACE_VENDOR_API","NOT IMPLEMENTED",{}};
  }
  std::vector<std::string> enumerateDeviceNodes() const override { return {}; }
  smart_factory::RealtimeGrant configureCurrentThreadRealtime(const smart_factory::RealtimeThreadOptions&) override {
    return {false,"NOT IMPLEMENTED: bind target realtime scheduling API"};
  }
};
}
extern "C" smart_factory::OsAdapter* smart_factory_create_os_adapter_v1(const char*, char*, std::size_t) {
  return new TargetOsAdapter();
}
extern "C" void smart_factory_destroy_os_adapter_v1(smart_factory::OsAdapter* adapter) { delete adapter; }
