#include "smart_factory/device_registry.hpp"
#include "smart_factory/platform/config_service.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }

std::filesystem::path tempDir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto p = std::filesystem::temp_directory_path() / ("smart-factory-b1-" + std::to_string(stamp));
  std::filesystem::create_directories(p); return p;
}

boost::json::object ref(const char* device, const char* point) {
  return {{"deviceId",device},{"pointId",point}};
}
}

int main() {
  const auto root = tempDir();
  try {
    const auto registry = smart_factory::DeviceRegistry::load(std::string(SMART_FACTORY_SOURCE_DIR) + "/backend-cpp/config/devices.development.json");
    require(registry->find("env-01") && registry->find("env-02"), "B1 fixture requires two temperature devices");

    std::string firstPublished;
    {
      smart_factory::ConfigService service(root.string(), "B1 Test Site", registry);
      auto status = service.status();
      require(status.at("state").as_string() == "RUNNING", "bootstrap config must be running");
      firstPublished = std::string(status.at("publishedRevision").as_string());
      require(firstPublished == std::string(status.at("runningRevision").as_string()), "bootstrap published/running mismatch");

      const auto instances = service.applicationInstances();
      int tempInstances = 0; bool env1 = false, env2 = false;
      for (const auto& v : instances) {
        const auto& i = v.as_object();
        if (i.at("templateId").as_string() != "temperature_humidity") continue;
        ++tempInstances;
        const auto& t = i.at("bindings").as_object().at("ambientTemperature").as_object();
        const auto device = std::string(t.at("deviceId").as_string());
        env1 = env1 || device == "env-01"; env2 = env2 || device == "env-02";
      }
      require(tempInstances == 2 && env1 && env2, "temperature instances must use explicit env-01/env-02 PointRef bindings");

      // Removing a point that is still referenced by an application instance must be rejected.
      {
        auto broken = service.activeSnapshot();
        auto& brokenDevices = broken.at("devices").as_array();
        bool removed = false;
        for (auto& deviceValue : brokenDevices) {
          auto& device = deviceValue.as_object();
          if (device.at("id").as_string() != "env-01") continue;
          auto& points = device.at("points").as_array();
          for (auto it = points.begin(); it != points.end(); ++it) {
            if (it->as_object().at("pointId").as_string() == "temperature") {
              points.erase(it);
              removed = true;
              break;
            }
          }
        }
        require(removed, "test fixture must remove env-01.temperature");
        auto brokenDraft = service.createDraft(firstPublished, broken, "engineer");
        auto brokenValidation = service.validateDraft(std::string(brokenDraft.at("draftId").as_string()));
        require(!brokenValidation.at("valid").as_bool(), "referenced point deletion must fail validation");
        bool hasMissingBinding = false;
        for (const auto& errorValue : brokenValidation.at("errors").as_array()) {
          const auto& error = errorValue.as_object();
          hasMissingBinding = hasMissingBinding || (error.if_contains("code") && error.at("code").as_string() == "BINDING_POINT_MISSING");
        }
        require(hasMissingBinding, "referenced point deletion must report BINDING_POINT_MISSING");
      }

      auto snapshot = service.activeSnapshot();
      auto& appInstances = snapshot.at("applicationInstances").as_array();
      appInstances.emplace_back(boost::json::object{
        {"instanceId","temperature_humidity-env-03-config-only"},{"templateId","temperature_humidity"},{"templateVersion","1"},
        {"assetId",appInstances.front().as_object().at("assetId")},{"displayName","配置新增温湿度实例"},
        {"bindings",boost::json::object{{"ambientTemperature",ref("env-01","temperature")},{"ambientHumidity",ref("env-01","humidity")}}},
        {"ruleIds",boost::json::array{}},{"allowedActions",boost::json::array{}}
      });
      auto draft = service.createDraft(firstPublished, snapshot, "engineer");
      const auto draftId = std::string(draft.at("draftId").as_string());
      auto validation = service.validateDraft(draftId);
      require(validation.at("valid").as_bool(), "application-only draft must validate");
      require(!validation.at("requiresRestart").as_bool(), "application-only draft must not require restart");
      auto published = service.publishDraft(draftId,"engineer","add third configured instance");
      require(published.at("ok").as_bool() && !published.at("requiresRestart").as_bool(), "application-only publish failed");
      require(service.applicationInstances().size() == instances.size() + 1, "third instance must become running without code changes");

      const auto afterAppRevision = std::string(published.at("revision").as_string());
      auto staleSnapshot = service.activeSnapshot();
      auto staleDraft = service.createDraft(firstPublished, staleSnapshot, "engineer");
      auto staleResult = service.publishDraft(std::string(staleDraft.at("draftId").as_string()),"engineer","stale base should conflict");
      require(!staleResult.at("ok").as_bool() && staleResult.at("code").as_string() == "CONFIG_REVISION_CONFLICT", "stale base must conflict");

      auto rollback = service.rollback(firstPublished,"engineer","return to bootstrap");
      require(rollback.at("ok").as_bool() && !rollback.at("requiresRestart").as_bool(), "application rollback must activate without restart");
      require(service.applicationInstances().size() == instances.size(), "rollback must restore application instances");

      auto deviceChange = service.activeSnapshot();
      auto& devices = deviceChange.at("devices").as_array();
      auto& d0 = devices.front().as_object();
      d0.at("connection").as_object()["endpoint"] = "SIMULATED://changed-endpoint";
      const auto base = std::string(service.status().at("publishedRevision").as_string());
      auto deviceDraft = service.createDraft(base, deviceChange, "engineer");
      auto deviceValidation = service.validateDraft(std::string(deviceDraft.at("draftId").as_string()));
      require(deviceValidation.at("valid").as_bool() && deviceValidation.at("requiresRestart").as_bool(), "device change must require restart");
      auto pending = service.publishDraft(std::string(deviceDraft.at("draftId").as_string()),"engineer","change adapter endpoint");
      require(pending.at("ok").as_bool() && pending.at("requiresRestart").as_bool(), "device change publish must be pending restart");
      const auto pendingStatus = service.status();
      require(pendingStatus.at("state").as_string() == "PUBLISHED_REQUIRES_RESTART", "pending device config must not report RUNNING");
      require(pendingStatus.at("publishedRevision").as_string() != pendingStatus.at("runningRevision").as_string(), "pending device config must preserve running revision");
      const auto runningSnapshot = service.activeSnapshot();
      require(runningSnapshot.at("devices").as_array().front().as_object().at("connection").as_object().at("endpoint").as_string() != "SIMULATED://changed-endpoint", "active snapshot must remain actual running device config");
    }

    // Restart must recover the same published/running distinction from SQLite.
    {
      smart_factory::ConfigService recovered(root.string(), "B1 Test Site", registry);
      auto status = recovered.status();
      require(status.at("state").as_string() == "PUBLISHED_REQUIRES_RESTART", "restart must recover pending-restart state");
      require(status.at("publishedRevision").as_string() != status.at("runningRevision").as_string(), "restart must not fake activation");
    }

    std::filesystem::remove_all(root);
    std::cout << "platform_config_b1 PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "platform_config_b1 FAIL: " << e.what() << "\n";
    std::filesystem::remove_all(root);
    return 1;
  }
}
