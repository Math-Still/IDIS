#include "smart_factory/platform/command_service.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>

namespace smart_factory {
namespace {
std::atomic<std::uint64_t> previewCounter{0};
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}
}

CommandService::CommandService(DeviceAdapter* adapter) : adapter_(adapter) {
  if (!adapter_) throw std::runtime_error("CommandService requires DeviceAdapter");
}

std::string CommandService::nextPreviewId(std::int64_t now) {
  return "preview-" + std::to_string(now) + "-" + std::to_string(++previewCounter);
}

void CommandService::prunePreviewsLocked(std::int64_t now) const {
  for (auto it = previews_.begin(); it != previews_.end();) {
    if (it->second.expiresAt <= now) it = previews_.erase(it);
    else ++it;
  }
}

CommandPreviewRecord CommandService::createPreview(const std::string& actorId,
                                                   const std::string& deviceId,
                                                   const std::string& action,
                                                   boost::json::value requestedValue,
                                                   const std::string& reason,
                                                   const std::string& configRevision,
                                                   const std::string& deviceStatus,
                                                   std::int64_t now,
                                                   std::int64_t ttlMs) {
  if (actorId.empty() || deviceId.empty() || action.empty()) throw std::runtime_error("preview identity is incomplete");
  if (ttlMs < 1000 || ttlMs > 5 * 60 * 1000) throw std::runtime_error("preview ttl must be 1000..300000 ms");
  CommandPreviewRecord record;
  record.previewId = nextPreviewId(now);
  record.actorId = actorId;
  record.deviceId = deviceId;
  record.action = action;
  record.requestedValue = std::move(requestedValue);
  record.reason = reason;
  record.configRevision = configRevision;
  record.deviceStatus = deviceStatus;
  record.createdAt = now;
  record.expiresAt = now + ttlMs;
  std::lock_guard lock(mutex_);
  prunePreviewsLocked(now);
  previews_[record.previewId] = record;
  return record;
}

std::optional<CommandPreviewRecord> CommandService::preview(const std::string& previewId,
                                                            const std::string& actorId,
                                                            std::int64_t now) const {
  std::lock_guard lock(mutex_);
  prunePreviewsLocked(now);
  const auto it = previews_.find(previewId);
  if (it == previews_.end() || it->second.actorId != actorId) return std::nullopt;
  return it->second;
}

void CommandService::consumePreview(const std::string& previewId) {
  std::lock_guard lock(mutex_);
  previews_.erase(previewId);
}

bool CommandService::adapterWriteReady() const {
  std::lock_guard lock(mutex_);
  if (quarantined_) return false;
  if (dynamic_cast<DeviceCommandExecutionExtension*>(adapter_)) return true;
  return adapter_->simulated();
}

bool CommandService::adapterQuarantined() const {
  std::lock_guard lock(mutex_);
  return quarantined_;
}

std::string CommandService::adapterWriteReadinessDetail() const {
  std::lock_guard lock(mutex_);
  if (quarantined_) return quarantineDetail_;
  if (auto* extension = dynamic_cast<DeviceCommandExecutionExtension*>(adapter_)) {
    return "write contract available: " + isolationName(extension->executionIsolation());
  }
  if (adapter_->simulated()) return "legacy simulated adapter allowed for development-only write tests";
  return "real adapter lacks bounded or isolated B4 write contract; write control is fail-closed";
}

std::string CommandService::certaintyName(DeviceCommandDeliveryCertainty certainty) {
  switch (certainty) {
    case DeviceCommandDeliveryCertainty::NotSent: return "NOT_SENT";
    case DeviceCommandDeliveryCertainty::Rejected: return "REJECTED";
    case DeviceCommandDeliveryCertainty::PossiblyApplied: return "POSSIBLY_APPLIED";
    case DeviceCommandDeliveryCertainty::Applied: return "APPLIED";
  }
  return "POSSIBLY_APPLIED";
}

std::string CommandService::isolationName(DeviceCommandExecutionIsolation isolation) {
  switch (isolation) {
    case DeviceCommandExecutionIsolation::BoundedInProcess: return "BOUNDED_IN_PROCESS";
    case DeviceCommandExecutionIsolation::IsolatedDriverHost: return "ISOLATED_DRIVER_HOST";
  }
  return "UNKNOWN";
}

SupervisedDeliveryResult CommandService::execute(const DeviceCommand& command,
                                                 std::int64_t deadlineTs) {
  {
    std::lock_guard lock(mutex_);
    if (quarantined_) {
      return {DeviceCommandDeliveryCertainty::NotSent,
              "ADAPTER_QUARANTINED: " + quarantineDetail_, "QUARANTINED", true};
    }
  }

  if (auto* extension = dynamic_cast<DeviceCommandExecutionExtension*>(adapter_)) {
    const auto mode = extension->executionIsolation();
    const auto startedAt = nowMs();
    DeviceCommandDeliveryResult result;
    try {
      result = extension->executeWithDelivery(command, deadlineTs);
    } catch (const std::exception& e) {
      result = {DeviceCommandDeliveryCertainty::PossiblyApplied,
                std::string("EXECUTION_EXCEPTION_AFTER_UNKNOWN_DELIVERY: ") + e.what()};
    } catch (...) {
      result = {DeviceCommandDeliveryCertainty::PossiblyApplied,
                "EXECUTION_EXCEPTION_AFTER_UNKNOWN_DELIVERY: unknown exception"};
    }
    const auto finishedAt = nowMs();
    bool quarantined = false;
    if (mode == DeviceCommandExecutionIsolation::BoundedInProcess && finishedAt > deadlineTs) {
      std::lock_guard lock(mutex_);
      quarantined_ = true;
      quarantined = true;
      quarantineDetail_ = "bounded in-process execution contract exceeded deadline; adapter requires investigation or process isolation";
      result.certainty = DeviceCommandDeliveryCertainty::PossiblyApplied;
      result.detail = "DRIVER_BOUNDED_CONTRACT_VIOLATION: " + result.detail;
    }
    return {result.certainty, result.detail, isolationName(mode), quarantined};
  }

  // Compatibility only for development simulation. A real adapter without a
  // bounded/isolated execution extension is not allowed to cross the write
  // boundary because a stuck vendor call cannot be safely killed by a thread.
  if (!adapter_->simulated()) {
    return {DeviceCommandDeliveryCertainty::NotSent,
            "DRIVER_ISOLATION_REQUIRED: real adapter does not advertise bounded execution or an isolated driver host",
            "UNSAFE_LEGACY_DRIVER", false};
  }

  try {
    const auto legacy = adapter_->execute(command);
    if (nowMs() > deadlineTs) {
      return {DeviceCommandDeliveryCertainty::PossiblyApplied,
              "LEGACY_SIMULATION_DEADLINE_OVERRUN: adapter returned after the execution deadline; physical outcome is unknown",
              "LEGACY_SIMULATION", false};
    }
    if (legacy.success) {
      return {DeviceCommandDeliveryCertainty::Applied,
              "LEGACY_SIMULATION_ONLY: " + legacy.detail, "LEGACY_SIMULATION", false};
    }
    return {DeviceCommandDeliveryCertainty::PossiblyApplied,
            "LEGACY_SIMULATION_AMBIGUOUS_FAILURE: " + legacy.detail, "LEGACY_SIMULATION", false};
  } catch (const std::exception& e) {
    return {DeviceCommandDeliveryCertainty::PossiblyApplied,
            std::string("LEGACY_SIMULATION_EXCEPTION: ") + e.what(), "LEGACY_SIMULATION", false};
  } catch (...) {
    return {DeviceCommandDeliveryCertainty::PossiblyApplied,
            "LEGACY_SIMULATION_EXCEPTION: unknown exception", "LEGACY_SIMULATION", false};
  }
}

}  // namespace smart_factory
