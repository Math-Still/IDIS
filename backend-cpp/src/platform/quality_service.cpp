#include "smart_factory/platform/quality_service.hpp"

#include <algorithm>
#include <limits>

namespace smart_factory {

QualityService::QualityService(std::shared_ptr<const DeviceRegistry> registry) : registry_(std::move(registry)) {}

std::int64_t QualityService::intField(const boost::json::object& obj, const char* key, std::int64_t fallback) {
  const auto* v = obj.if_contains(key);
  if (!v) return fallback;
  if (v->is_int64()) return v->as_int64();
  if (v->is_uint64() && v->as_uint64() <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return static_cast<std::int64_t>(v->as_uint64());
  }
  return fallback;
}

std::string QualityService::stringField(const boost::json::object& obj, const char* key, const std::string& fallback) {
  if (const auto* v = obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return fallback;
}

void QualityService::appendReason(boost::json::array& reasons, const std::string& reason) {
  for (const auto& v : reasons) if (v.is_string() && v.as_string() == reason) return;
  reasons.emplace_back(reason);
}

int QualityService::qualityRank(const std::string& quality) {
  if (quality == "GOOD") return 0;
  if (quality == "UNCERTAIN") return 1;
  if (quality == "STALE") return 2;
  return 3; // BAD and unknown values fail safe.
}

std::string QualityService::worseQuality(const std::string& current, const std::string& candidate) {
  return qualityRank(candidate) > qualityRank(current) ? candidate : current;
}

bool QualityService::valueMatchesType(const boost::json::value& value, const std::string& valueType) {
  if (value.is_null()) return false;
  if (valueType == "boolean") return value.is_bool();
  if (valueType == "string") return value.is_string();
  if (valueType == "integer") return value.is_int64() || value.is_uint64();
  if (valueType == "number") return value.is_double() || value.is_int64() || value.is_uint64();
  return true; // Unknown types are carried but flagged separately by the caller if needed.
}

boost::json::object QualityService::normalizePoint(const boost::json::object& raw,
                                                   std::int64_t now,
                                                   const std::string& sourceEpoch,
                                                   const std::string& originKind,
                                                   const std::string& sourceId,
                                                   const std::string& configRevision,
                                                   const std::string& contextId) const {
  auto point = raw;
  const auto deviceId = stringField(point, "deviceId");
  const auto pointId = stringField(point, "pointId");
  const auto* definition = registry_ ? registry_->findPoint(deviceId, pointId) : nullptr;

  boost::json::array reasons;
  if (const auto* existing = point.if_contains("qualityReasons"); existing && existing->is_array()) reasons = existing->as_array();
  std::string quality = stringField(point, "quality", "UNCERTAIN");
  if (quality != "GOOD" && quality != "UNCERTAIN" && quality != "BAD" && quality != "STALE") {
    quality = "UNCERTAIN";
    appendReason(reasons, "QUALITY_CODE_UNRECOGNIZED");
  }

  auto receiveTs = intField(point, "receiveTs", now);
  if (receiveTs <= 0) {
    receiveTs = now;
    quality = worseQuality(quality, "UNCERTAIN");
    appendReason(reasons, "RECEIVE_TIMESTAMP_MISSING");
  }

  auto sampleTs = intField(point, "sampleTs", 0);
  std::string sourceTimeBasis = "DEVICE";
  if (sampleTs <= 0) {
    // Historian v1 requires a positive ordering timestamp. Use host receive
    // time transparently, never pretending that a device timestamp existed.
    sampleTs = receiveTs;
    sourceTimeBasis = "HOST";
    quality = worseQuality(quality, "UNCERTAIN");
    appendReason(reasons, "SOURCE_TIMESTAMP_MISSING");
  } else if (originKind == "SIMULATION") {
    sourceTimeBasis = "HOST";
  }

  if (sampleTs > receiveTs + 5000) {
    quality = worseQuality(quality, "UNCERTAIN");
    appendReason(reasons, "SOURCE_CLOCK_AHEAD");
  }

  if (definition) {
    if (!point.if_contains("label") || !point.at("label").is_string() || point.at("label").as_string().empty()) point["label"] = definition->label;
    if (const auto* value = point.if_contains("value"); !value || !valueMatchesType(*value, definition->valueType)) {
      quality = "BAD";
      appendReason(reasons, value ? "VALUE_TYPE_MISMATCH" : "VALUE_MISSING");
      if (!value) point["value"] = nullptr;
    }
    if (!definition->unit.empty()) point["unit"] = definition->unit;
    const auto maxGap = std::max<std::int64_t>(5000, definition->samplePeriodMs * 3);
    if (receiveTs - sampleTs > maxGap) {
      quality = worseQuality(quality, "STALE");
      appendReason(reasons, "SAMPLE_AGE_EXCEEDED");
    }
  } else {
    quality = "BAD";
    appendReason(reasons, "POINT_NOT_REGISTERED");
  }

  if (intField(point, "seq", -1) < 0) {
    quality = worseQuality(quality, "UNCERTAIN");
    appendReason(reasons, "SEQUENCE_MISSING");
  }

  point["sampleTs"] = sampleTs;
  point["receiveTs"] = receiveTs;
  point["quality"] = quality;
  point["qualityReasons"] = std::move(reasons);
  point["sourceEpoch"] = sourceEpoch;
  point["configRevision"] = configRevision;
  point["contextId"] = contextId;
  point["provenance"] = boost::json::object{
      {"originKind", originKind}, {"sourceId", sourceId}, {"deliveryMode", "LIVE"}, {"sourceTimeBasis", sourceTimeBasis}};
  return point;
}

}  // namespace smart_factory
