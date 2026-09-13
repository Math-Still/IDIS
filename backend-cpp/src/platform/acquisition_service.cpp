#include "smart_factory/platform/acquisition_service.hpp"
#include "smart_factory/platform/quality_service.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace smart_factory {
namespace {
std::string failureFor(const boost::json::object& details, const std::string& deviceId) {
  if (const auto* v = details.if_contains(deviceId); v && v->is_string()) return std::string(v->as_string());
  return "device acquisition failed";
}

void addReason(boost::json::object& point, const std::string& reason) {
  boost::json::array reasons;
  if (const auto* v = point.if_contains("qualityReasons"); v && v->is_array()) reasons = v->as_array();
  for (const auto& r : reasons) if (r.is_string() && r.as_string() == reason) { point["qualityReasons"] = std::move(reasons); return; }
  reasons.emplace_back(reason);
  point["qualityReasons"] = std::move(reasons);
}
}

std::int64_t AcquisitionService::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string AcquisitionService::stringField(const boost::json::object& obj, const char* key, const std::string& fallback) {
  if (const auto* v = obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return fallback;
}

AcquisitionService::AcquisitionService(std::shared_ptr<const DeviceRegistry> registry,
                                       std::string siteId,
                                       std::string sourceId,
                                       bool simulated)
    : registry_(std::move(registry)), siteId_(std::move(siteId)), sourceId_(std::move(sourceId)),
      originKind_(simulated ? "SIMULATION" : "REAL_DEVICE") {
  sourceEpoch_ = sourceId_ + ":" + std::to_string(nowMs());
}

DeviceAcquisitionBatch AcquisitionService::wrapV1(DeviceDataSnapshot snapshot) const {
  DeviceAcquisitionBatch batch;
  batch.snapshot = std::move(snapshot);
  batch.sourceEpoch = sourceEpoch_;
  if (registry_) {
    for (const auto& d : registry_->devices()) {
      batch.ownedDeviceIds.insert(d.id);
      batch.completeDeviceIds.insert(d.id);
    }
  } else {
    for (const auto& v : batch.snapshot.devices) {
      if (!v.is_object()) continue;
      const auto id = stringField(v.as_object(), "id");
      if (!id.empty()) { batch.ownedDeviceIds.insert(id); batch.completeDeviceIds.insert(id); }
    }
  }
  return batch;
}

void AcquisitionService::replaceDeviceValues(boost::json::array& target, const boost::json::array& incoming,
                                              const std::set<std::string>& completeDeviceIds,
                                              const std::set<std::string>& failedDeviceIds,
                                              std::int64_t now, const boost::json::object& failureDetails) {
  std::unordered_map<std::string, boost::json::object> merged;
  for (const auto& v : target) if (v.is_object()) {
    const auto id = stringField(v.as_object(), "id"); if (!id.empty()) merged[id] = v.as_object();
  }
  for (const auto& v : incoming) if (v.is_object()) {
    const auto id = stringField(v.as_object(), "id"); if (!id.empty() && completeDeviceIds.contains(id)) merged[id] = v.as_object();
  }
  for (const auto& id : failedDeviceIds) {
    auto& device = merged[id];
    if (!device.if_contains("id")) device["id"] = id;
    device["status"] = "DEGRADED";
    device["acquisitionState"] = "FAILED";
    device["acquisitionError"] = failureFor(failureDetails, id);
    device["acquisitionFailedAt"] = now;
  }
  target.clear();
  for (auto& [_, value] : merged) target.emplace_back(std::move(value));
}

void AcquisitionService::replaceTelemetryValues(boost::json::array& target, const boost::json::array& incoming,
                                                 const std::set<std::string>& completeDeviceIds,
                                                 const std::set<std::string>& failedDeviceIds,
                                                 std::int64_t now, const boost::json::object& failureDetails) {
  std::unordered_map<std::string, boost::json::object> merged;
  for (const auto& v : target) if (v.is_object()) {
    const auto d = stringField(v.as_object(), "deviceId"); const auto p = stringField(v.as_object(), "pointId");
    if (!d.empty() && !p.empty()) merged[d + ":" + p] = v.as_object();
  }
  for (const auto& v : incoming) if (v.is_object()) {
    const auto d = stringField(v.as_object(), "deviceId"); const auto p = stringField(v.as_object(), "pointId");
    if (!d.empty() && !p.empty() && completeDeviceIds.contains(d)) merged[d + ":" + p] = v.as_object();
  }
  for (auto& [_, point] : merged) {
    const auto d = stringField(point, "deviceId");
    if (!failedDeviceIds.contains(d)) continue;
    point["quality"] = "STALE";
    point["staleSince"] = now;
    point["acquisitionError"] = failureFor(failureDetails, d);
    addReason(point, "DEVICE_ACQUISITION_FAILED");
  }
  target.clear();
  for (auto& [_, value] : merged) target.emplace_back(std::move(value));
}

void AcquisitionService::replaceCommunicationValues(boost::json::array& target, const boost::json::array& incoming,
                                                     const std::set<std::string>& completeDeviceIds,
                                                     const std::set<std::string>& failedDeviceIds,
                                                     std::int64_t now, const boost::json::object& failureDetails) {
  std::unordered_map<std::string, boost::json::object> merged;
  for (const auto& v : target) if (v.is_object()) {
    const auto id = stringField(v.as_object(), "deviceId"); if (!id.empty()) merged[id] = v.as_object();
  }
  for (const auto& v : incoming) if (v.is_object()) {
    const auto id = stringField(v.as_object(), "deviceId"); if (!id.empty() && completeDeviceIds.contains(id)) merged[id] = v.as_object();
  }
  for (const auto& id : failedDeviceIds) {
    auto& comm = merged[id];
    comm["deviceId"] = id;
    comm["online"] = false;
    comm["quality"] = "BAD";
    comm["acquisitionState"] = "FAILED";
    comm["acquisitionError"] = failureFor(failureDetails, id);
    comm["failedAt"] = now;
  }
  target.clear();
  for (auto& [_, value] : merged) target.emplace_back(std::move(value));
}

DeviceAcquisitionBatch AcquisitionService::normalize(DeviceAcquisitionBatch batch,
                                                      const DeviceDataSnapshot& previous,
                                                      const std::string& configRevision,
                                                      QualityService& quality) const {
  const auto now = batch.snapshot.generatedAt > 0 ? batch.snapshot.generatedAt : nowMs();
  batch.snapshot.generatedAt = now;
  if (batch.sourceEpoch.empty()) batch.sourceEpoch = sourceEpoch_;

  const auto contextId = "live:" + siteId_;
  boost::json::array normalizedTelemetry;
  for (const auto& v : batch.snapshot.telemetry) {
    if (!v.is_object()) continue;
    const auto deviceId = stringField(v.as_object(), "deviceId");
    if (!batch.completeDeviceIds.contains(deviceId)) continue;
    normalizedTelemetry.emplace_back(quality.normalizePoint(v.as_object(), now, batch.sourceEpoch, originKind_, sourceId_, configRevision, contextId));
  }
  batch.snapshot.telemetry = std::move(normalizedTelemetry);

  for (auto& v : batch.snapshot.devices) if (v.is_object()) {
    auto& d = v.as_object();
    d["provenance"] = boost::json::object{{"originKind",originKind_},{"sourceId",sourceId_},{"deliveryMode","LIVE"}};
    d["configRevision"] = configRevision;
  }
  for (auto& v : batch.snapshot.communication) if (v.is_object()) {
    auto& c = v.as_object();
    c["provenance"] = boost::json::object{{"originKind",originKind_},{"sourceId",sourceId_},{"deliveryMode","LIVE"}};
    c["configRevision"] = configRevision;
  }

  // Partial batches update only complete devices. Failed devices keep their
  // last known facts but are explicitly marked degraded/stale; absence never
  // becomes an implicit zero or healthy return-to-normal.
  auto merged = previous;
  if (batch.completeDeviceIds.size() == batch.ownedDeviceIds.size()) {
    merged = batch.snapshot;
  } else {
    if (merged.generatedAt <= 0) merged = batch.snapshot;
    merged.generatedAt = now;
    replaceDeviceValues(merged.devices, batch.snapshot.devices, batch.completeDeviceIds, batch.failedDeviceIds, now, batch.failureDetails);
    replaceTelemetryValues(merged.telemetry, batch.snapshot.telemetry, batch.completeDeviceIds, batch.failedDeviceIds, now, batch.failureDetails);
    replaceCommunicationValues(merged.communication, batch.snapshot.communication, batch.completeDeviceIds, batch.failedDeviceIds, now, batch.failureDetails);
    merged.alarms = batch.snapshot.alarms; // lifecycle merge remains ApplicationService responsibility.
  }
  batch.snapshot = std::move(merged);
  return batch;
}

}  // namespace smart_factory
