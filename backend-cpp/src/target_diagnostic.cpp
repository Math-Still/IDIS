#include "smart_factory/config.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/integration_factory.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <thread>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace {
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

boost::json::array strings(const std::vector<std::string>& values) {
  boost::json::array a;
  for (const auto& v : values) a.emplace_back(v);
  return a;
}

boost::json::array networkInterfaces() {
  boost::json::array out;
#if defined(__linux__)
  std::error_code ec;
  const std::filesystem::path root{"/sys/class/net"};
  if (std::filesystem::exists(root, ec)) {
    for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
      if (ec) break;
      out.emplace_back(e.path().filename().string());
    }
  }
#endif
  return out;
}
}

int main(int argc, char** argv) {
  try {
    std::string configPath = "backend-cpp/config/backend.development.conf";
    bool probeRealtime = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--config" && i + 1 < argc) configPath = argv[++i];
      else if (arg == "--probe-realtime") probeRealtime = true;
      else if (arg == "--help") {
        std::cout << "usage: smart-factory-target-diagnostic [--config PATH] [--probe-realtime]\n";
        return 0;
      }
    }

    auto config = smart_factory::loadBackendConfig(configPath);
    auto registry = smart_factory::DeviceRegistry::load(config.deviceRegistryFile);
    auto os = smart_factory::makeConfiguredOsAdapter(config);
    const auto caps = os->inspectCapabilities();

    boost::json::object root;
    root["schema"] = "smart-factory-target-environment/v1";
    root["timestamp"] = nowMs();
    root["configPath"] = configPath;
    root["compiler"] = std::string(__VERSION__);
    root["cxxStandard"] = static_cast<std::int64_t>(__cplusplus);
    root["hardwareConcurrency"] = static_cast<std::int64_t>(std::thread::hardware_concurrency());
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    root["uid"] = static_cast<std::int64_t>(getuid());
    root["euid"] = static_cast<std::int64_t>(geteuid());
    utsname u{};
    if (uname(&u) == 0) root["uname"] = boost::json::object{{"sysname",u.sysname},{"release",u.release},{"version",u.version},{"machine",u.machine}};
#endif

    boost::json::object osInfo{{"adapter",os->name()},{"apiVersion",os->apiVersion()},{"monotonicClock",caps.monotonicClock},
      {"realtimeScheduling",caps.realtimeScheduling},{"cpuAffinity",caps.cpuAffinity},{"memoryLock",caps.memoryLock},
      {"deviceFilesystem",caps.deviceFilesystem},{"schedulerApi",caps.schedulerApi},{"detail",caps.detail},{"metadata",caps.metadata}};
    osInfo["deviceNodes"] = strings(os->enumerateDeviceNodes());
    root["os"] = std::move(osInfo);
    root["networkInterfaces"] = networkInterfaces();

    std::set<std::string> interfaces, protocols, drivers;
    for (const auto& d : registry->devices()) {
      interfaces.insert(d.connection.interfaceType);
      protocols.insert(d.connection.protocol);
      drivers.insert(d.connection.driver);
    }
    boost::json::array ifs, ps, ds;
    for (const auto& v : interfaces) ifs.emplace_back(v);
    for (const auto& v : protocols) ps.emplace_back(v);
    for (const auto& v : drivers) ds.emplace_back(v);
    root["registry"] = boost::json::object{{"path",registry->sourcePath()},{"schemaVersion",registry->schemaVersion()},{"site",registry->site()},
      {"deviceCount",static_cast<std::int64_t>(registry->size())},{"interfaces",std::move(ifs)},{"protocols",std::move(ps)},{"drivers",std::move(ds)}};

    if (probeRealtime) {
      smart_factory::RealtimeGrant grant;
      std::thread probe([&] {
        smart_factory::RealtimeThreadOptions options;
        options.priority = config.realtimePriority;
        options.cpuAffinity = config.realtimeCpu;
        options.lockMemory = config.realtimeLockMemory;
        options.threadName = "sf-rt-probe";
        grant = os->configureCurrentThreadRealtime(options);
      });
      probe.join();
      root["realtimeProbe"] = boost::json::object{{"requested",true},{"granted",grant.granted},{"detail",grant.detail},
        {"priority",config.realtimePriority},{"cpu",config.realtimeCpu},{"lockMemory",config.realtimeLockMemory}};
    } else {
      root["realtimeProbe"] = boost::json::object{{"requested",false},{"note","Use --probe-realtime to attempt actual scheduling/affinity/memory-lock grant."}};
    }

    std::cout << boost::json::serialize(root) << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "target diagnostic failed: " << e.what() << '\n';
    return 1;
  }
}
