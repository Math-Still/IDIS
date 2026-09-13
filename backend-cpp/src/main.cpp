#include "smart_factory/application.hpp"
#include "smart_factory/config.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/http_server.hpp"
#include "smart_factory/integration_factory.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    std::string configPath = "backend-cpp/config/backend.development.conf";
    if (argc >= 3 && std::string(argv[1]) == "--config") configPath = argv[2];
    else if (argc == 2) configPath = argv[1];

    auto config = smart_factory::loadBackendConfig(configPath);
    auto registry = smart_factory::DeviceRegistry::load(config.deviceRegistryFile);
    auto device = smart_factory::makeConfiguredDeviceAdapter(config, registry);
    auto os = config.enableRealtime ? smart_factory::makeConfiguredOsAdapter(config) : nullptr;

    smart_factory::HttpServer* serverPtr = nullptr;
    smart_factory::ApplicationService app(
        config, std::move(device),
        [&serverPtr](const std::string& payload) {
          if (serverPtr) serverPtr->broadcast(payload);
        },
        std::move(os), registry);
    smart_factory::HttpServer server(config, app);
    serverPtr = &server;

    const auto status = app.runtimeStatus();
    std::cout << "runtime: osAdapter=" << status.osAdapter
              << " deviceAdapter=" << status.deviceAdapter
              << " simulated=" << status.deviceAdapterSimulated
              << " registryDevices=" << status.configuredDeviceCount
              << " realtimeEnabled=" << status.realtimeEnabled
              << " realtimeGranted=" << status.realtimeGranted
              << " detail=\"" << status.realtimeDetail << "\"\n";
    server.run();
    // Stop publishing into HttpServer before its destructor runs. ApplicationService
    // outlives server in this scope and owns the publisher thread.
    serverPtr = nullptr;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
