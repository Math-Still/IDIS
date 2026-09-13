#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace smart_factory {

inline constexpr std::uint32_t kProtocolAdapterApiVersion = 1;

enum class AdapterHealthState { Ready, Degraded, Offline, Fault };

struct AdapterHealth {
  AdapterHealthState state{AdapterHealthState::Offline};
  std::string detail;
  std::int64_t lastSuccessTs{0};
  std::uint64_t errorCount{0};
};

struct ProtocolReadRequest {
  std::string deviceId;
  std::string pointId;
  boost::json::object source;
};

struct ProtocolReadResult {
  bool success{false};
  boost::json::value value{nullptr};
  std::int64_t sampleTs{0};
  std::string quality{"BAD"};
  std::string detail;
};

struct ProtocolWriteRequest {
  std::string deviceId;
  std::string action;
  boost::json::value requestedValue{nullptr};
  boost::json::object mapping;
};

struct ProtocolWriteResult {
  bool success{false};
  std::string detail;
};

// Integration boundary for concrete industrial protocol implementations.
// The platform core does not assume Modbus/CAN/OPC UA/etc. Concrete protocol
// teams implement this interface and compose it inside a DeviceAdapter.
class ProtocolAdapter {
 public:
  virtual ~ProtocolAdapter() = default;
  virtual std::uint32_t apiVersion() const noexcept { return kProtocolAdapterApiVersion; }
  virtual std::string name() const = 0;
  virtual bool start(std::string& detail) = 0;
  virtual void stop() noexcept = 0;
  virtual AdapterHealth health() const = 0;
  virtual ProtocolReadResult read(const ProtocolReadRequest& request) = 0;
  virtual ProtocolWriteResult write(const ProtocolWriteRequest& request) = 0;
};

}  // namespace smart_factory
