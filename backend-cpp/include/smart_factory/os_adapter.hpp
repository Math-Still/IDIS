#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace smart_factory {

inline constexpr std::uint32_t kOsAdapterApiVersion = 1;

struct RealtimeThreadOptions {
  int priority{80};
  int cpuAffinity{-1};
  bool lockMemory{false};
  std::string threadName{"sf-realtime"};
};

struct RealtimeGrant {
  bool granted{false};
  std::string detail;
};

struct OsCapabilities {
  bool monotonicClock{false};
  bool realtimeScheduling{false};
  bool cpuAffinity{false};
  bool memoryLock{false};
  bool deviceFilesystem{false};
  std::string schedulerApi;
  std::string detail;
  boost::json::object metadata;
};

// Frozen OS integration boundary (API v1). Target OS teams implement only
// this interface; business/runtime services must not call vendor OS APIs
// directly.
class OsAdapter {
 public:
  virtual ~OsAdapter() = default;
  virtual std::uint32_t apiVersion() const noexcept { return kOsAdapterApiVersion; }
  virtual std::string name() const = 0;
  virtual std::uint64_t monotonicNowNs() const = 0;
  virtual OsCapabilities inspectCapabilities() const = 0;
  virtual std::vector<std::string> enumerateDeviceNodes() const { return {}; }
  virtual RealtimeGrant configureCurrentThreadRealtime(const RealtimeThreadOptions& options) = 0;
};

std::unique_ptr<OsAdapter> makeDefaultOsAdapter();

}  // namespace smart_factory
