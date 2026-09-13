#pragma once

#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"

#include <boost/json.hpp>
#include <cstdint>
#include <memory>
#include <set>
#include <string>

namespace smart_factory {

// Optional extension layered on top of frozen DeviceAdapter API v1. Existing
// adapters keep the conservative all-or-throw readSnapshot() contract. An
// adapter that can isolate per-device acquisition failures may additionally
// implement this extension without changing the v1 virtual table.
struct DeviceAcquisitionBatch {
  DeviceDataSnapshot snapshot;
  std::set<std::string> ownedDeviceIds;
  std::set<std::string> completeDeviceIds;
  std::set<std::string> failedDeviceIds;
  boost::json::object failureDetails;
  std::string sourceEpoch;
};

class DeviceAdapterBatchExtension {
 public:
  virtual ~DeviceAdapterBatchExtension() = default;
  virtual DeviceAcquisitionBatch readBatch() = 0;
};

class QualityService;

class AcquisitionService {
 public:
  AcquisitionService(std::shared_ptr<const DeviceRegistry> registry,
                     std::string siteId,
                     std::string sourceId,
                     bool simulated);

  DeviceAcquisitionBatch wrapV1(DeviceDataSnapshot snapshot) const;
  DeviceAcquisitionBatch normalize(DeviceAcquisitionBatch batch,
                                   const DeviceDataSnapshot& previous,
                                   const std::string& configRevision,
                                   QualityService& quality) const;

  const std::string& sourceEpoch() const noexcept { return sourceEpoch_; }
  const std::string& originKind() const noexcept { return originKind_; }
  const std::string& sourceId() const noexcept { return sourceId_; }

 private:
  static std::int64_t nowMs();
  static std::string stringField(const boost::json::object& obj, const char* key, const std::string& fallback = {});
  static void replaceDeviceValues(boost::json::array& target, const boost::json::array& incoming,
                                  const std::set<std::string>& completeDeviceIds,
                                  const std::set<std::string>& failedDeviceIds,
                                  std::int64_t now, const boost::json::object& failureDetails);
  static void replaceTelemetryValues(boost::json::array& target, const boost::json::array& incoming,
                                     const std::set<std::string>& completeDeviceIds,
                                     const std::set<std::string>& failedDeviceIds,
                                     std::int64_t now, const boost::json::object& failureDetails);
  static void replaceCommunicationValues(boost::json::array& target, const boost::json::array& incoming,
                                         const std::set<std::string>& completeDeviceIds,
                                         const std::set<std::string>& failedDeviceIds,
                                         std::int64_t now, const boost::json::object& failureDetails);

  std::shared_ptr<const DeviceRegistry> registry_;
  std::string siteId_;
  std::string sourceId_;
  std::string originKind_;
  std::string sourceEpoch_;
};

}  // namespace smart_factory
