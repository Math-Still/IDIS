#include "smart_factory/os_adapter.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <time.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace smart_factory {
namespace {
class PosixOsAdapter final : public OsAdapter {
 public:
  std::string name() const override { return "posix"; }

  std::uint64_t monotonicNowNs() const override {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(ts.tv_nsec);
  }

  OsCapabilities inspectCapabilities() const override {
    OsCapabilities caps;
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    caps.monotonicClock = true;
    caps.realtimeScheduling = true;
    caps.memoryLock = true;
    caps.schedulerApi = "POSIX pthread_setschedparam/SCHED_FIFO";
#if defined(__linux__)
    caps.cpuAffinity = true;
    caps.deviceFilesystem = std::filesystem::exists("/dev");
#else
    caps.cpuAffinity = false;
    caps.deviceFilesystem = std::filesystem::exists("/dev");
#endif
    utsname info{};
    if (uname(&info) == 0) {
      caps.metadata["sysname"] = info.sysname;
      caps.metadata["release"] = info.release;
      caps.metadata["machine"] = info.machine;
    }
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    caps.metadata["uid"] = static_cast<std::int64_t>(getuid());
    caps.metadata["euid"] = static_cast<std::int64_t>(geteuid());
#endif
    caps.detail = "Portable POSIX capability surface; grant/permission must be verified on the target OS.";
#else
    caps.detail = "POSIX capability surface unavailable on this build target.";
#endif
    return caps;
  }

  std::vector<std::string> enumerateDeviceNodes() const override {
    std::vector<std::string> nodes;
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    std::error_code ec;
    if (!std::filesystem::exists("/dev", ec)) return nodes;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
      if (ec) break;
      const auto name = entry.path().filename().string();
      if (name.rfind("tty", 0) == 0 || name.rfind("can", 0) == 0 || name.rfind("video", 0) == 0 ||
          name.rfind("i2c-", 0) == 0 || name.rfind("spidev", 0) == 0) {
        nodes.push_back(entry.path().string());
        if (nodes.size() >= 128) break;
      }
    }
#endif
    return nodes;
  }

  RealtimeGrant configureCurrentThreadRealtime(const RealtimeThreadOptions& options) override {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#if defined(__linux__)
    if (!options.threadName.empty()) {
      pthread_setname_np(pthread_self(), options.threadName.substr(0, 15).c_str());
    }
#endif

    std::ostringstream details;
    bool ok = true;

#if defined(__linux__)
    if (options.cpuAffinity >= 0) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(options.cpuAffinity, &cpuset);
      const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      if (rc != 0) {
        ok = false;
        details << "cpu-affinity failed: " << std::strerror(rc) << "; ";
      } else {
        details << "cpu-affinity CPU " << options.cpuAffinity << " granted; ";
      }
    }
#endif

    if (options.lockMemory) {
      if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        ok = false;
        details << "mlockall failed: " << std::strerror(errno) << "; ";
      } else {
        details << "mlockall granted; ";
      }
    }

    sched_param param{};
    param.sched_priority = options.priority;
    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (rc != 0) {
      ok = false;
      details << "SCHED_FIFO priority " << options.priority << " failed: " << std::strerror(rc)
              << " (CAP_SYS_NICE/root or target RT policy may be required); ";
    } else {
      details << "SCHED_FIFO priority " << options.priority << " granted; ";
    }

    return {ok, details.str()};
#else
    return {false, "POSIX realtime APIs unavailable on this build target"};
#endif
  }
};
}  // namespace

std::unique_ptr<OsAdapter> makeDefaultOsAdapter() { return std::make_unique<PosixOsAdapter>(); }

}  // namespace smart_factory
