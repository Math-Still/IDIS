#include "smart_factory/integration_factory.hpp"
#include "smart_factory/device_adapter_plugin.hpp"
#include "smart_factory/os_adapter_plugin.hpp"

#include <array>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace smart_factory {
namespace {
#if defined(_WIN32)
using LibraryHandle = HMODULE;
LibraryHandle openLibrary(const std::string& path) { return LoadLibraryA(path.c_str()); }
void closeLibrary(LibraryHandle handle) { if (handle) FreeLibrary(handle); }
void* resolveSymbol(LibraryHandle handle, const char* name) { return reinterpret_cast<void*>(GetProcAddress(handle, name)); }
std::string libraryError() { return "LoadLibrary/GetProcAddress failed with code " + std::to_string(GetLastError()); }
#else
using LibraryHandle = void*;
LibraryHandle openLibrary(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void closeLibrary(LibraryHandle handle) { if (handle) dlclose(handle); }
void* resolveSymbol(LibraryHandle handle, const char* name) { return dlsym(handle, name); }
std::string libraryError() { const char* e = dlerror(); return e ? e : "unknown dynamic-loader error"; }
#endif

class PluginDeviceAdapter final : public DeviceAdapter {
 public:
  PluginDeviceAdapter(LibraryHandle handle, DeviceAdapter* inner, SmartFactoryDestroyDeviceAdapterV1 destroy)
      : handle_(handle), inner_(inner), destroy_(destroy) {
    if (!handle_ || !inner_ || !destroy_) throw std::runtime_error("invalid DeviceAdapter plugin handle/object/destroy function");
  }
  ~PluginDeviceAdapter() override {
    if (inner_) destroy_(inner_);
    inner_ = nullptr;
    closeLibrary(handle_);
    handle_ = nullptr;
  }
  std::uint32_t apiVersion() const noexcept override { return inner_->apiVersion(); }
  std::string name() const override { return inner_->name(); }
  bool simulated() const override { return inner_->simulated(); }
  bool start(std::string& detail) override { return inner_->start(detail); }
  void stop() noexcept override { inner_->stop(); }
  DeviceAdapterHealth health() const override { return inner_->health(); }
  DeviceDataSnapshot readSnapshot() override { return inner_->readSnapshot(); }
  DeviceCommandResult execute(const DeviceCommand& c) override { return inner_->execute(c); }
  DeviceCommandFeedback readCommandFeedback(const DeviceCommand& c) override { return inner_->readCommandFeedback(c); }
 private:
  LibraryHandle handle_{};
  DeviceAdapter* inner_{nullptr};
  SmartFactoryDestroyDeviceAdapterV1 destroy_{nullptr};
};

class PluginOsAdapter final : public OsAdapter {
 public:
  PluginOsAdapter(LibraryHandle handle, OsAdapter* inner, SmartFactoryDestroyOsAdapterV1 destroy)
      : handle_(handle), inner_(inner), destroy_(destroy) {
    if (!handle_ || !inner_ || !destroy_) throw std::runtime_error("invalid OsAdapter plugin handle/object/destroy function");
  }
  ~PluginOsAdapter() override {
    if (inner_) destroy_(inner_);
    inner_ = nullptr;
    closeLibrary(handle_);
    handle_ = nullptr;
  }
  std::uint32_t apiVersion() const noexcept override { return inner_->apiVersion(); }
  std::string name() const override { return inner_->name(); }
  std::uint64_t monotonicNowNs() const override { return inner_->monotonicNowNs(); }
  OsCapabilities inspectCapabilities() const override { return inner_->inspectCapabilities(); }
  std::vector<std::string> enumerateDeviceNodes() const override { return inner_->enumerateDeviceNodes(); }
  RealtimeGrant configureCurrentThreadRealtime(const RealtimeThreadOptions& o) override { return inner_->configureCurrentThreadRealtime(o); }
 private:
  LibraryHandle handle_{};
  OsAdapter* inner_{nullptr};
  SmartFactoryDestroyOsAdapterV1 destroy_{nullptr};
};

std::unique_ptr<DeviceAdapter> loadDevicePlugin(const BackendConfig& config, const DeviceRegistry& registry) {
  auto handle = openLibrary(config.deviceAdapterLibrary);
  if (!handle) throw std::runtime_error("Cannot load DeviceAdapter plugin: " + config.deviceAdapterLibrary + ": " + libraryError());
  auto create = reinterpret_cast<SmartFactoryCreateDeviceAdapterV1>(resolveSymbol(handle, "smart_factory_create_device_adapter_v1"));
  auto destroy = reinterpret_cast<SmartFactoryDestroyDeviceAdapterV1>(resolveSymbol(handle, "smart_factory_destroy_device_adapter_v1"));
  if (!create || !destroy) {
    const auto detail = libraryError();
    closeLibrary(handle);
    throw std::runtime_error("DeviceAdapter plugin is missing v1 create/destroy symbols: " + detail);
  }
  const auto registryJson = boost::json::serialize(registry.toJson(true));
  std::array<char, 1024> error{};
  DeviceAdapter* inner = create(registryJson.c_str(), config.deviceAdapterOptions.c_str(), error.data(), error.size());
  if (!inner) {
    const std::string detail = error.data()[0] ? error.data() : "plugin create returned null";
    closeLibrary(handle);
    throw std::runtime_error("DeviceAdapter plugin creation failed: " + detail);
  }
  if (inner->apiVersion() != kDeviceAdapterApiVersion) {
    destroy(inner); closeLibrary(handle);
    throw std::runtime_error("DeviceAdapter plugin API version mismatch; host requires v" + std::to_string(kDeviceAdapterApiVersion));
  }
  return std::make_unique<PluginDeviceAdapter>(handle, inner, destroy);
}

std::unique_ptr<OsAdapter> loadOsPlugin(const BackendConfig& config) {
  auto handle = openLibrary(config.osAdapterLibrary);
  if (!handle) throw std::runtime_error("Cannot load OsAdapter plugin: " + config.osAdapterLibrary + ": " + libraryError());
  auto create = reinterpret_cast<SmartFactoryCreateOsAdapterV1>(resolveSymbol(handle, "smart_factory_create_os_adapter_v1"));
  auto destroy = reinterpret_cast<SmartFactoryDestroyOsAdapterV1>(resolveSymbol(handle, "smart_factory_destroy_os_adapter_v1"));
  if (!create || !destroy) {
    const auto detail = libraryError();
    closeLibrary(handle);
    throw std::runtime_error("OsAdapter plugin is missing v1 create/destroy symbols: " + detail);
  }
  std::array<char, 1024> error{};
  OsAdapter* inner = create(config.osAdapterOptions.c_str(), error.data(), error.size());
  if (!inner) {
    const std::string detail = error.data()[0] ? error.data() : "plugin create returned null";
    closeLibrary(handle);
    throw std::runtime_error("OsAdapter plugin creation failed: " + detail);
  }
  if (inner->apiVersion() != kOsAdapterApiVersion) {
    destroy(inner); closeLibrary(handle);
    throw std::runtime_error("OsAdapter plugin API version mismatch; host requires v" + std::to_string(kOsAdapterApiVersion));
  }
  return std::make_unique<PluginOsAdapter>(handle, inner, destroy);
}
}  // namespace

std::unique_ptr<DeviceAdapter> makeConfiguredDeviceAdapter(
    const BackendConfig& config, std::shared_ptr<const DeviceRegistry> registry) {
  if (!registry) throw std::runtime_error("DeviceRegistry is required by DeviceAdapter factory");
  if (config.deviceAdapter == "simulated") {
#if SMART_FACTORY_ENABLE_SIMULATED_ADAPTER
    return makeSimulatedDeviceAdapter(std::move(registry));
#else
    throw std::runtime_error("simulated DeviceAdapter is disabled in this build; use a target plugin/adapter");
#endif
  }
  if (config.deviceAdapter == "plugin") return loadDevicePlugin(config, *registry);
  throw std::runtime_error("Unknown device_adapter='" + config.deviceAdapter + "'. Supported core adapters: simulated (development only), plugin (target integration).");
}

std::unique_ptr<OsAdapter> makeConfiguredOsAdapter(const BackendConfig& config) {
  if (config.osAdapter == "posix") return makeDefaultOsAdapter();
  if (config.osAdapter == "plugin") return loadOsPlugin(config);
  throw std::runtime_error("Unknown os_adapter='" + config.osAdapter + "'. Supported core adapters: posix, plugin.");
}

}  // namespace smart_factory
