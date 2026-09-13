#pragma once

#include "smart_factory/device_registry.hpp"

#include <boost/json.hpp>
#include <cstdint>
#include <string>

namespace smart_factory {

class QualityService {
 public:
  explicit QualityService(std::shared_ptr<const DeviceRegistry> registry);

  boost::json::object normalizePoint(const boost::json::object& raw,
                                     std::int64_t now,
                                     const std::string& sourceEpoch,
                                     const std::string& originKind,
                                     const std::string& sourceId,
                                     const std::string& configRevision,
                                     const std::string& contextId) const;

 private:
  static std::int64_t intField(const boost::json::object& obj, const char* key, std::int64_t fallback = 0);
  static std::string stringField(const boost::json::object& obj, const char* key, const std::string& fallback = {});
  static void appendReason(boost::json::array& reasons, const std::string& reason);
  static int qualityRank(const std::string& quality);
  static std::string worseQuality(const std::string& current, const std::string& candidate);
  static bool valueMatchesType(const boost::json::value& value, const std::string& valueType);

  std::shared_ptr<const DeviceRegistry> registry_;
};

}  // namespace smart_factory
