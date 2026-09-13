#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace smart_factory {

class DeviceRegistry;
inline constexpr std::uint32_t kDeviceAdapterApiVersion = 1;

struct DeviceCommand {
  std::string commandId;
  std::string deviceId;
  std::string action;
  std::string requestedValueJson{"null"};
};

struct DeviceCommandResult {
  bool success{false};
  std::string detail;
};

// B4 command-delivery semantics are deliberately separate from the backend
// command lifecycle. A driver can prove a request was not sent, prove the
// device rejected it, report that it may already have crossed the physical
// boundary, or prove delivery at the adapter boundary. None of these values
// alone means the requested physical state was independently confirmed.
enum class DeviceCommandDeliveryCertainty { NotSent, Rejected, PossiblyApplied, Applied };

enum class DeviceCommandExecutionIsolation {
  // The driver contract guarantees executeWithDelivery() returns within the
  // supplied deadline. The host cannot forcibly cancel a lying/stuck SDK.
  BoundedInProcess,
  // The adapter owns a killable driver host/process or equivalent isolation.
  IsolatedDriverHost
};

struct DeviceCommandDeliveryResult {
  DeviceCommandDeliveryCertainty certainty{DeviceCommandDeliveryCertainty::PossiblyApplied};
  std::string detail;
};

// Optional B4 extension. Frozen DeviceAdapter API-v1 is intentionally left
// unchanged. Real write-capable adapters that cannot provide this contract are
// fail-closed by the platform; development simulation keeps a compatibility
// fallback so existing test adapters are not presented as production drivers.
class DeviceCommandExecutionExtension {
 public:
  virtual ~DeviceCommandExecutionExtension() = default;
  virtual DeviceCommandExecutionIsolation executionIsolation() const noexcept = 0;
  virtual DeviceCommandDeliveryResult executeWithDelivery(const DeviceCommand& command,
                                                           std::int64_t deadlineTs) = 0;
};

enum class DeviceFeedbackState { Pending, Confirmed, Failed, Unsupported };

struct DeviceCommandFeedback {
  DeviceFeedbackState state{DeviceFeedbackState::Pending};
  std::string detail;
  std::string observedValueJson{"null"};
};

struct DeviceDataSnapshot {
  boost::json::array devices;
  boost::json::array telemetry;
  boost::json::array alarms;
  boost::json::array communication;
  std::int64_t generatedAt{0};
};

struct DeviceAdapterHealth {
  bool ready{false};
  std::string state{"OFFLINE"};
  std::string detail;
  std::int64_t lastSuccessTs{0};
  std::uint64_t errorCount{0};
};

// Frozen integration boundary (API v1). Target teams implement this class;
// the platform owns data quality, historian, alarm lifecycle, command state,
// RBAC/audit, REST/WebSocket and HMI behavior above this boundary.
class DeviceAdapter {
 public:
  virtual ~DeviceAdapter() = default;
  virtual std::uint32_t apiVersion() const noexcept { return kDeviceAdapterApiVersion; }
  virtual std::string name() const = 0;
  virtual bool simulated() const = 0;

  // Lifecycle hooks are intentionally explicit for hardware/SDK ownership.
  // A target adapter should open drivers/sessions in start() and release them
  // in stop(). Default implementations preserve compatibility for tests.
  virtual bool start(std::string& detail) { detail = "adapter ready"; return true; }
  virtual void stop() noexcept {}
  virtual DeviceAdapterHealth health() const { return {true, "READY", "adapter did not provide extended health", 0, 0}; }

  // API-v1 completeness contract: a successful return MUST represent a complete
  // acquisition cycle for every alarm/device domain owned by this adapter. If
  // any part is missing, stale because of a read failure, or otherwise partial,
  // throw instead of returning a partial snapshot. The host treats absence of a
  // FIELD alarm as return-to-normal only after a successful complete cycle.
  virtual DeviceDataSnapshot readSnapshot() = 0;

  // execute() only means the command was delivered/applied at the adapter
  // boundary. It MUST NOT be interpreted as physical confirmation.
  virtual DeviceCommandResult execute(const DeviceCommand& command) = 0;

  // readCommandFeedback() observes independent field/device feedback after
  // APPLIED. Only Confirmed may advance the command to CONFIRMED.
  virtual DeviceCommandFeedback readCommandFeedback(const DeviceCommand& command) = 0;
};

// Development/demo implementation. Production builds can disable it with
// SMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF.
std::unique_ptr<DeviceAdapter> makeSimulatedDeviceAdapter(std::shared_ptr<const DeviceRegistry> registry);

}  // namespace smart_factory
