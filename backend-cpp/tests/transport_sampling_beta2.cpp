#include "smart_factory/application.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/historian.hpp"

#include <boost/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}
}

int main() {
#ifndef SMART_FACTORY_SOURCE_DIR
  std::cerr << "SMART_FACTORY_SOURCE_DIR is not defined\n";
  return 1;
#endif
  const auto root = std::filesystem::path(SMART_FACTORY_SOURCE_DIR);
  auto registry = smart_factory::DeviceRegistry::load((root / "backend-cpp/config/devices.development.json").string());
  if (!registry || registry->minimumSamplePeriodMs() != 250) {
    std::cerr << "registry minimum sample period is not 250 ms\n";
    return 2;
  }

  const auto base = std::filesystem::temp_directory_path() / ("smart-factory-beta2-sampling-" + std::to_string(nowMs()));
  std::filesystem::create_directories(base / "historian");
  std::atomic<int> telemetryEvents{0};
  std::atomic<int> deviceEvents{0};

  smart_factory::BackendConfig cfg;
  cfg.enableRealtime = false;
  cfg.authEnabled = false;
  cfg.auditEnabled = false;
  cfg.commandLedgerEnabled = false;
  cfg.historianEnabled = true;
  cfg.historianDirectory = (base / "historian").string();
  cfg.historianBackupEnabled = false;
  cfg.historianMinFreeSpaceMb = 1;
  cfg.acquisitionPollMs = 100;       // deliberately faster than every registry point
  cfg.telemetryPublishMs = 600;      // HMI delivery intentionally slower than acquisition
  cfg.acquisitionStaleAfterMs = 1000;
  cfg.acquisitionOfflineAfterMs = 3000;

  {
    auto adapter = smart_factory::makeSimulatedDeviceAdapter(registry);
    smart_factory::ApplicationService app(
        cfg, std::move(adapter),
        [&](const std::string& message) {
          try {
            const auto value = boost::json::parse(message);
            if (!value.is_object()) return;
            const auto& object = value.as_object();
            const auto* type = object.if_contains("type");
            if (!type || !type->is_string()) return;
            const std::string name(type->as_string());
            if (name == "telemetry.updated") ++telemetryEvents;
            if (name == "device.updated") ++deviceEvents;
          } catch (...) {
          }
        },
        nullptr, registry);

    const auto runtime = app.runtimeStatus();
    if (runtime.acquisitionPollMs != 100 || runtime.hmiPublishMs != 600) {
      std::cerr << "runtime sampling/publish cadence mismatch: poll=" << runtime.acquisitionPollMs
                << " publish=" << runtime.hmiPublishMs << "\n";
      return 3;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1450));

    smart_factory::TelemetryHistoryQuery fast;
    fast.deviceId = "motion-01";
    fast.pointId = "distance";
    fast.limit = 100;
    const auto fastResult = app.telemetryHistory(fast);
    const auto fastCount = fastResult.at("items").as_array().size();

    smart_factory::TelemetryHistoryQuery slow;
    slow.deviceId = "env-01";
    slow.pointId = "temperature";
    slow.limit = 100;
    const auto slowResult = app.telemetryHistory(slow);
    const auto slowCount = slowResult.at("items").as_array().size();

    // Initial acquisition is persisted synchronously; publisher loop then runs
    // at 100 ms. 250 ms points therefore persist materially more often than
    // 1000 ms points, while the HMI only receives a few 600 ms bursts.
    if (fastCount < 4 || slowCount < 2 || fastCount <= slowCount) {
      std::cerr << "registry samplePeriodMs is not governing Historian cadence: fast=" << fastCount
                << " slow=" << slowCount << "\n";
      return 4;
    }

    const auto registryPointCount = [&] {
      std::size_t count = 0;
      for (const auto& device : registry->devices()) count += device.points.size();
      return count;
    }();
    const auto hmiBurstsApprox = registryPointCount == 0 ? 0 : telemetryEvents.load() / static_cast<int>(registryPointCount);
    if (telemetryEvents.load() <= 0 || hmiBurstsApprox > 4) {
      std::cerr << "HMI telemetry publication was not throttled independently: events=" << telemetryEvents.load()
                << " points=" << registryPointCount << "\n";
      return 5;
    }
    if (deviceEvents.load() <= 0) {
      std::cerr << "no HMI device publication observed\n";
      return 6;
    }
  }

  std::filesystem::remove_all(base);
  std::cout << "transport/sampling beta2 test passed; acquisition, Historian cadence and HMI publication are decoupled\n";
  return 0;
}
