#pragma once

#include "smart_factory/device_adapter.hpp"

#include <boost/json.hpp>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace smart_factory {

struct CommandPreviewRecord {
  std::string previewId;
  std::string actorId;
  std::string deviceId;
  std::string action;
  boost::json::value requestedValue{nullptr};
  std::string reason;
  std::string configRevision;
  std::string deviceStatus;
  std::int64_t createdAt{0};
  std::int64_t expiresAt{0};
};

struct SupervisedDeliveryResult {
  DeviceCommandDeliveryCertainty certainty{DeviceCommandDeliveryCertainty::PossiblyApplied};
  std::string detail;
  std::string supervisionMode{"UNKNOWN"};
  bool adapterQuarantined{false};
};

// B4 command-control helper. It intentionally does not own the adapter or the
// backend lifecycle state machine. ApplicationService remains the compatibility
// facade while command preview/session state and write-safety gating are moved
// behind this testable boundary.
class CommandService {
 public:
  explicit CommandService(DeviceAdapter* adapter);

  CommandPreviewRecord createPreview(const std::string& actorId,
                                     const std::string& deviceId,
                                     const std::string& action,
                                     boost::json::value requestedValue,
                                     const std::string& reason,
                                     const std::string& configRevision,
                                     const std::string& deviceStatus,
                                     std::int64_t now,
                                     std::int64_t ttlMs = 60000);
  std::optional<CommandPreviewRecord> preview(const std::string& previewId,
                                              const std::string& actorId,
                                              std::int64_t now) const;
  void consumePreview(const std::string& previewId);

  SupervisedDeliveryResult execute(const DeviceCommand& command,
                                   std::int64_t deadlineTs);
  bool adapterWriteReady() const;
  bool adapterQuarantined() const;
  std::string adapterWriteReadinessDetail() const;

  static std::string certaintyName(DeviceCommandDeliveryCertainty certainty);
  static std::string isolationName(DeviceCommandExecutionIsolation isolation);

 private:
  static std::string nextPreviewId(std::int64_t now);
  void prunePreviewsLocked(std::int64_t now) const;

  DeviceAdapter* adapter_{nullptr};
  mutable std::mutex mutex_;
  mutable std::unordered_map<std::string, CommandPreviewRecord> previews_;
  bool quarantined_{false};
  std::string quarantineDetail_;
};

}  // namespace smart_factory
