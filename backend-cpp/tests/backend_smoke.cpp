#include "smart_factory/application.hpp"
#include "smart_factory/config.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"

#include <boost/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>


namespace {

std::shared_ptr<smart_factory::DeviceRegistry> developmentRegistry() {
  return smart_factory::DeviceRegistry::load(std::string(SMART_FACTORY_SOURCE_DIR) + "/backend-cpp/config/devices.development.json");
}

std::int64_t testNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}


class CommandCountingAdapter final : public smart_factory::DeviceAdapter {
 public:
  std::string name() const override { return "command-counting-test"; }
  bool simulated() const override { return true; }
  smart_factory::DeviceDataSnapshot readSnapshot() override {
    const auto now = testNowMs();
    smart_factory::DeviceDataSnapshot snapshot;
    snapshot.generatedAt = now;
    snapshot.devices.emplace_back(boost::json::object{{"id","env-01"},{"name","Env"},{"scenario","temperature_humidity"},{"status","ONLINE"},{"location","A"},{"protocol","test"},{"osNode","edge"},{"lastSeen",now},{"tags",boost::json::array{}}});
    snapshot.telemetry.emplace_back(boost::json::object{{"deviceId","env-01"},{"pointId","temperature"},{"label","Temperature"},{"value",25.0},{"unit","C"},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",1}});
    snapshot.communication.emplace_back(boost::json::object{{"deviceId","env-01"},{"online",true},{"quality","GOOD"},{"latencyMsP50",1},{"latencyMsP95",2},{"latencyMsP99",3},{"jitterMs",0},{"messageRatePerMin",60},{"reconnectCount",0},{"errorCount",0},{"lastSeen",now}});
    return snapshot;
  }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override {
    ++executeCount_;
    return {true,"accepted"};
  }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override {
    return {smart_factory::DeviceFeedbackState::Confirmed,"confirmed","true"};
  }
  int executeCount() const { return executeCount_.load(); }
 private:
  std::atomic<int> executeCount_{0};
};

class AlarmLifecycleTestAdapter final : public smart_factory::DeviceAdapter {
 public:
  void setAlarmActive(bool active) { active_ = active; }
  std::string name() const override { return "alarm-lifecycle-test"; }
  bool simulated() const override { return true; }
  smart_factory::DeviceDataSnapshot readSnapshot() override {
    const auto now = testNowMs();
    smart_factory::DeviceDataSnapshot s;
    s.generatedAt = now;
    s.devices.emplace_back(boost::json::object{{"id","gas-01"},{"name","Gas 01"},{"scenario","hazardous_gas"},{"status","ONLINE"},{"location","A"},{"protocol","Modbus"},{"osNode","edge"},{"lastSeen",now},{"tags",boost::json::array{}}});
    s.telemetry.emplace_back(boost::json::object{{"deviceId","gas-01"},{"pointId","gas"},{"label","Gas"},{"value",12.3},{"unit","ppm"},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",++seq_}});
    if (active_) s.alarms.emplace_back(boost::json::object{{"id","gas-high-01"},{"deviceId","gas-01"},{"title","Gas high"},{"message","Threshold exceeded"},{"severity","CRITICAL"},{"state","ACTIVE"},{"raisedAt",raisedAt_}});
    s.communication.emplace_back(boost::json::object{{"deviceId","gas-01"},{"online",true},{"quality","GOOD"},{"latencyMsP50",1},{"latencyMsP95",2},{"latencyMsP99",3},{"jitterMs",0},{"messageRatePerMin",60},{"reconnectCount",0},{"errorCount",0},{"lastSeen",now}});
    return s;
  }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override { return {true,"ok"}; }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override {
    return {smart_factory::DeviceFeedbackState::Confirmed,"test field feedback confirmed","true"};
  }
 private:
  const std::int64_t raisedAt_{testNowMs() - 1000};
  mutable std::int64_t seq_{0};
  std::atomic<bool> active_{true};
};

smart_factory::SecurityContext actor(const std::string& id="operator-01", smart_factory::UserRole role=smart_factory::UserRole::Administrator) {
  smart_factory::SecurityContext ctx; ctx.principal = {id,id,role,true}; ctx.remoteAddress="127.0.0.1"; return ctx;
}
}

int main() {
  // Handoff contract: the registry JSON passed to a target DeviceAdapter must
  // preserve site, command mappings and metadata. These fields are integration
  // inputs and must not disappear at the plugin boundary.
  const auto registryContractPath = std::filesystem::temp_directory_path() /
      ("smart-factory-registry-contract-" + std::to_string(testNowMs()) + ".json");
  {
    std::ofstream out(registryContractPath);
    out << R"JSON({
      "schemaVersion":"1.1",
      "site":"handoff-test-site",
      "devices":[{
        "id":"handoff-01","name":"Handoff Device","scenario":"temperature_humidity","location":"Area A",
        "osNode":"edge-01","adapter":"target",
        "connection":{"interfaceType":"RS-485","protocol":"Example Protocol","endpoint":"/dev/example","driver":"example.driver","parameters":{"address":7}},
        "points":[{"pointId":"pv","label":"PV","unit":"","valueType":"number","samplePeriodMs":1000,"source":{"register":1}}],
        "commands":{"start":{"displayName":"Start","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false},"mapping":{"coil":2}}},
        "metadata":{"vendor":"example"}
      }]
    })JSON";
  }
  auto registryContract = smart_factory::DeviceRegistry::load(registryContractPath.string());
  const auto registryJson = registryContract->toJson(true);
  std::filesystem::remove(registryContractPath);
  if (registryContract->site() != "handoff-test-site" || !registryJson.if_contains("site")) {
    std::cerr << "registry site was not preserved across plugin serialization\n";
    return 1;
  }
  const auto& registryDevice = registryJson.at("devices").as_array().front().as_object();
  if (!registryDevice.if_contains("commands") || !registryDevice.if_contains("metadata") ||
      registryDevice.at("commands").as_object().empty() || registryDevice.at("metadata").as_object().empty()) {
    std::cerr << "registry command/metadata integration fields were lost\n";
    return 11;
  }

  // Registry definitions are security policy. Malformed emergency definitions
  // must fail at startup instead of silently falling back to string heuristics.
  const auto malformedRegistryPath = std::filesystem::temp_directory_path() /
      ("smart-factory-registry-malformed-" + std::to_string(testNowMs()) + ".json");
  {
    std::ofstream out(malformedRegistryPath);
    out << R"JSON({"schemaVersion":"1.1","site":"bad","devices":[{"id":"bad-01","name":"Bad","scenario":"hazardous_gas","location":"A","connection":{"interfaceType":"DI","protocol":"Test","driver":"test"},"points":[{"pointId":"pv","label":"PV","valueType":"number","samplePeriodMs":1000}],"commands":{"device.emergencyStop":{"displayName":"Emergency stop","requiredPermission":"command.issue","executionClass":"EMERGENCY","reasonRequired":false,"parameterSchema":{"type":"none","required":false}}}}]})JSON";
  }
  bool malformedRejected = false;
  try { (void)smart_factory::DeviceRegistry::load(malformedRegistryPath.string()); }
  catch (const std::exception&) { malformedRejected = true; }
  std::filesystem::remove(malformedRegistryPath);
  if (!malformedRejected) { std::cerr << "malformed emergency command definition was accepted\n"; return 12; }

  // A denied command must never cross the DeviceAdapter boundary.
  {
    smart_factory::BackendConfig guardConfig;
    guardConfig.enableRealtime = false;
    guardConfig.historianEnabled = false;
    guardConfig.auditEnabled = false;
    guardConfig.commandFeedbackTimeoutMs = 300;
    guardConfig.commandFeedbackPollMs = 10;
    auto guardRegistry = developmentRegistry();
    auto counting = std::make_unique<CommandCountingAdapter>();
    auto* countingPtr = counting.get();
    smart_factory::ApplicationService guardApp(guardConfig, std::move(counting), {}, nullptr, guardRegistry);
    boost::json::object denied{{"commandId","guard-denied"},{"deviceId","env-01"},{"action","device.arbitraryUnsafeAction"},{"ttlMs",5000}};
    if (guardApp.issueCommand(denied, actor()).httpStatus != 404 || countingPtr->executeCount() != 0) {
      std::cerr << "denied command crossed adapter boundary\n"; return 13;
    }
    boost::json::object allowed{{"commandId","guard-allowed"},{"deviceId","env-01"},{"action","device.selfTest"},{"ttlMs",5000}};
    if (guardApp.issueCommand(allowed, actor()).httpStatus != 202) { std::cerr << "registered command was not accepted\n"; return 14; }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (countingPtr->executeCount() != 1) { std::cerr << "registered command did not cross adapter boundary exactly once\n"; return 15; }
  }
  smart_factory::BackendConfig config;
  config.enableRealtime = true;
  config.requireRealtime = false;
  config.enableEmergencyControl = false;
  config.workerThreads = 2;
  config.telemetryPublishMs = 100;
  config.commandFeedbackTimeoutMs = 600;
  config.commandFeedbackPollMs = 20;
  auto devRegistry = developmentRegistry();
  auto device = smart_factory::makeSimulatedDeviceAdapter(devRegistry);
  std::mutex eventMutex;
  std::set<std::string> eventTypes;
  smart_factory::ApplicationService app(config, std::move(device), [&](const std::string& payload) {
    try {
      const auto value = boost::json::parse(payload);
      if (value.is_object()) {
        const auto& obj = value.as_object();
        if (const auto* type = obj.if_contains("type"); type && type->is_string()) {
          std::lock_guard lock(eventMutex);
          eventTypes.insert(std::string(type->as_string()));
        }
      }
    } catch (...) {}
  }, nullptr, devRegistry);

  const auto dashboard = app.dashboardSnapshot();
  if (!dashboard.if_contains("devices") || dashboard.at("devices").as_array().size() < 5) {
    std::cerr << "dashboard device model failed\n";
    return 2;
  }
  const auto& telemetry = dashboard.at("telemetry").as_array();
  if (telemetry.empty() || !telemetry.front().as_object().if_contains("seq")) {
    std::cerr << "telemetry ordering metadata missing\n";
    return 3;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(260));
  {
    std::lock_guard lock(eventMutex);
    for (const auto* required : {"telemetry.updated", "device.updated", "alarm.updated", "communication.updated"}) {
      if (!eventTypes.contains(required)) {
        std::cerr << "missing realtime event: " << required << "\n";
        return 4;
      }
    }
  }

  boost::json::object request{{"commandId", "test-1"}, {"deviceId", "env-01"}, {"action", "device.selfTest"}, {"ttlMs", 5000}};
  auto first = app.issueCommand(request, actor());
  auto duplicate = app.issueCommand(request, actor());
  if (first.httpStatus != 202 || duplicate.httpStatus != 200) {
    std::cerr << "command idempotency status failed\n";
    return 5;
  }

  boost::json::object conflict = request;
  conflict["action"] = "device.feedback-fail";
  if (app.issueCommand(conflict, actor()).httpStatus != 409) {
    std::cerr << "command idempotency conflict failed\n";
    return 6;
  }

  boost::json::object unknownCommand{{"commandId","unknown-command"},{"deviceId","env-01"},{"action","device.arbitraryUnsafeAction"},{"ttlMs",5000}};
  const auto unknownResult = app.issueCommand(unknownCommand, actor());
  if (unknownResult.httpStatus != 404 || !unknownResult.body.if_contains("code") || unknownResult.body.at("code").as_string() != "COMMAND_NOT_REGISTERED") {
    std::cerr << "unknown command default-deny failed\n";
    return 64;
  }
  boost::json::object emergencyBogus{{"commandId","emergency-bogus"},{"deviceId","safety-01"},{"action","device.emergencyBogus"},{"ttlMs",5000},{"reason","must reject"}};
  if (app.issueCommand(emergencyBogus, actor()).httpStatus != 404) {
    std::cerr << "unregistered emergency-like action was not rejected\n";
    return 65;
  }
  boost::json::object unsupportedForDevice{{"commandId","cross-device"},{"deviceId","light-01"},{"action","device.feedback-fail"},{"ttlMs",5000}};
  if (app.issueCommand(unsupportedForDevice, actor()).httpStatus != 404) {
    std::cerr << "cross-device command capability enforcement failed\n";
    return 66;
  }
  boost::json::object invalidParameter{{"commandId","invalid-parameter"},{"deviceId","env-01"},{"action","device.setLevel"},{"requestedValue",150},{"ttlMs",5000}};
  const auto invalidParameterResult = app.issueCommand(invalidParameter, actor());
  if (invalidParameterResult.httpStatus != 400 || invalidParameterResult.body.at("code").as_string() != "COMMAND_PARAMETER_INVALID") {
    std::cerr << "command parameter maximum enforcement failed\n";
    return 67;
  }
  boost::json::object missingParameter{{"commandId","missing-parameter"},{"deviceId","env-01"},{"action","device.setLevel"},{"ttlMs",5000}};
  if (app.issueCommand(missingParameter, actor()).httpStatus != 400) {
    std::cerr << "required command parameter enforcement failed\n";
    return 68;
  }
  boost::json::object validParameter{{"commandId","valid-parameter"},{"deviceId","env-01"},{"action","device.setLevel"},{"requestedValue",50.0},{"ttlMs",5000}};
  if (app.issueCommand(validParameter, actor()).httpStatus != 202) {
    std::cerr << "valid registered command parameter rejected\n";
    return 69;
  }

  boost::json::object feedbackOk{{"commandId","feedback-ok"},{"deviceId","env-01"},{"action","device.selfTest"},{"ttlMs",5000}};
  if (app.issueCommand(feedbackOk, actor()).httpStatus != 202) { std::cerr << "feedback command issue failed\n"; return 61; }
  std::this_thread::sleep_for(std::chrono::milliseconds(260));
  bool sawConfirmed=false, sawAppliedSemantic=false;
  const auto feedbackOkSnapshot = app.dashboardSnapshot();
  for (const auto& value : feedbackOkSnapshot.at("commands").as_array()) {
    const auto& c=value.as_object();
    if (c.at("id").as_string()=="feedback-ok") {
      sawConfirmed=c.at("state").as_string()=="CONFIRMED";
      sawAppliedSemantic=c.if_contains("feedbackStatus") && c.at("feedbackStatus").as_string()=="CONFIRMED";
    }
  }
  if(!sawConfirmed||!sawAppliedSemantic){std::cerr<<"field feedback confirmation failed\n";return 62;}

  boost::json::object feedbackFail{{"commandId","feedback-fail"},{"deviceId","env-01"},{"action","device.feedback-fail"},{"ttlMs",5000}};
  app.issueCommand(feedbackFail, actor()); std::this_thread::sleep_for(std::chrono::milliseconds(100));
  bool sawFieldFail=false;
  const auto feedbackFailSnapshot = app.dashboardSnapshot();
  for(const auto& value:feedbackFailSnapshot.at("commands").as_array()){const auto& c=value.as_object(); if(c.at("id").as_string()=="feedback-fail"&&c.at("state").as_string()=="FAILED"&&std::string(c.at("detail").as_string()).find("FIELD_FAILED")!=std::string::npos)sawFieldFail=true;}
  if(!sawFieldFail){std::cerr<<"field feedback failure path failed\n";return 63;}

  boost::json::object emergency{{"commandId", "test-rt"}, {"deviceId", "safety-01"}, {"action", "device.emergencyStop"}, {"ttlMs", 5000}, {"reason", "smoke test"}};
  const auto disabledEmergency = app.issueCommand(emergency, actor());
  if (disabledEmergency.httpStatus != 409 || !disabledEmergency.body.if_contains("code") ||
      disabledEmergency.body.at("code").as_string() != "EMERGENCY_CONTROL_DISABLED") {
    std::cerr << "emergency disabled boundary failed\n";
    return 7;
  }

  const auto historianDir = (std::filesystem::temp_directory_path() / ("smart-factory-historian-smoke-" + std::to_string(testNowMs()))).string();
  {
    smart_factory::BackendConfig alarmConfig;
    alarmConfig.enableRealtime = false;
    alarmConfig.historianEnabled = true;
    alarmConfig.historianDirectory = historianDir;
    alarmConfig.historianMaxRecords = 5000;
    auto alarmDevice = std::make_unique<AlarmLifecycleTestAdapter>();
    smart_factory::ApplicationService alarmApp(alarmConfig, std::move(alarmDevice), [](const std::string&){});
    const auto beforeAck = alarmApp.alarms();
    if (beforeAck.size() != 1 || beforeAck.front().as_object().at("state").as_string() != "ACTIVE") {
      std::cerr << "alarm lifecycle initial state failed\n";
      return 9;
    }
    const auto ack = alarmApp.acknowledgeAlarm("gas-high-01", boost::json::object{{"comment","checked"}}, actor("operator-01", smart_factory::UserRole::Operator));
    if (ack.httpStatus != 200 || ack.body.at("state").as_string() != "ACKNOWLEDGED" || !ack.body.if_contains("acknowledgedBy")) {
      std::cerr << "alarm ACK failed\n";
      return 10;
    }
    smart_factory::AlarmHistoryQuery alarmQuery; alarmQuery.alarmId = "gas-high-01"; alarmQuery.limit = 100;
    const auto alarmHistory = alarmApp.alarmHistory(alarmQuery);
    if (alarmHistory.at("items").as_array().size() < 2) {
      std::cerr << "alarm historian did not record lifecycle\n";
      return 11;
    }
    smart_factory::TelemetryHistoryQuery telemetryQuery; telemetryQuery.deviceId = "gas-01"; telemetryQuery.pointId = "gas"; telemetryQuery.limit = 100;
    const auto telemetryHistory = alarmApp.telemetryHistory(telemetryQuery);
    if (telemetryHistory.at("items").as_array().empty()) {
      std::cerr << "telemetry historian did not persist sample\n";
      return 12;
    }
  }
  {
    smart_factory::BackendConfig alarmConfig;
    alarmConfig.enableRealtime = false;
    alarmConfig.historianEnabled = true;
    alarmConfig.historianDirectory = historianDir;
    alarmConfig.historianMaxRecords = 5000;
    auto alarmDevice = std::make_unique<AlarmLifecycleTestAdapter>();
    auto* alarmDevicePtr = alarmDevice.get();
    alarmConfig.telemetryPublishMs = 100;
    smart_factory::ApplicationService recoveredApp(alarmConfig, std::move(alarmDevice), [](const std::string&){});
    const auto recovered = recoveredApp.alarms();
    if (recovered.size() != 1 || recovered.front().as_object().at("state").as_string() != "ACKNOWLEDGED" ||
        recovered.front().as_object().at("acknowledgedBy").as_string() != "operator-01") {
      std::cerr << "alarm ACK recovery across restart failed\n";
      return 13;
    }
    if (recoveredApp.clearAlarm("gas-high-01", boost::json::object{}, actor("operator-01", smart_factory::UserRole::Operator)).httpStatus != 409) {
      std::cerr << "manual alarm clear was not rejected\n";
      return 14;
    }
    alarmDevicePtr->setAlarmActive(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    const auto cleared = recoveredApp.alarms();
    if (cleared.size() != 1 || cleared.front().as_object().at("state").as_string() != "CLEARED") {
      std::cerr << "field return-to-normal did not clear alarm\n";
      return 15;
    }
    smart_factory::AlarmHistoryQuery clearedQuery; clearedQuery.alarmId = "gas-high-01"; clearedQuery.limit = 100;
    const auto events = recoveredApp.alarmHistory(clearedQuery).at("items").as_array();
    bool sawCleared = false;
    for (const auto& value : events) if (value.as_object().at("eventType").as_string() == "CLEARED") sawCleared = true;
    if (!sawCleared) {
      std::cerr << "field clear lifecycle was not persisted\n";
      return 16;
    }
  }
  std::filesystem::remove_all(historianDir);

  const auto securityRoot = std::filesystem::temp_directory_path() / ("smart-factory-security-smoke-" + std::to_string(testNowMs()));
  std::filesystem::create_directories(securityRoot);
  const auto authFile = (securityRoot / "users.json").string();
  {
    std::ofstream out(authFile);
    out << boost::json::serialize(boost::json::object{{"users",boost::json::array{
      boost::json::object{{"id","observer"},{"displayName","Observer"},{"role","OBSERVER"},{"tokenSha256",smart_factory::AuthService::sha256Hex("observer-token")},{"enabled",true}},
      boost::json::object{{"id","operator"},{"displayName","Operator"},{"role","OPERATOR"},{"tokenSha256",smart_factory::AuthService::sha256Hex("operator-token")},{"enabled",true}},
      boost::json::object{{"id","admin"},{"displayName","Admin"},{"role","ADMINISTRATOR"},{"tokenSha256",smart_factory::AuthService::sha256Hex("admin-token")},{"enabled",true}}
    }}});
  }
  {
    smart_factory::BackendConfig securityConfig; securityConfig.enableRealtime=false; securityConfig.authEnabled=true; securityConfig.authUsersFile=authFile;
    securityConfig.auditEnabled=true; securityConfig.auditDirectory=(securityRoot/"audit").string(); securityConfig.auditMaxRecords=5000;
    securityConfig.historianEnabled=false; securityConfig.commandFeedbackTimeoutMs=500; securityConfig.commandFeedbackPollMs=20;
    auto securityRegistry = developmentRegistry();
    smart_factory::ApplicationService secured(securityConfig, smart_factory::makeSimulatedDeviceAdapter(securityRegistry), [](const std::string&){}, nullptr, securityRegistry);
    const auto observer=secured.authenticate("observer","observer-token"); const auto oper=secured.authenticate("operator","operator-token"); const auto admin=secured.authenticate("admin","admin-token");
    if(!observer||!oper||!admin||secured.authenticate("operator","bad-token")){std::cerr<<"authentication failed\n";return 17;}
    smart_factory::SecurityContext observerCtx{*observer,"127.0.0.1"}; smart_factory::SecurityContext operatorCtx{*oper,"127.0.0.1"}; smart_factory::SecurityContext adminCtx{*admin,"127.0.0.1"};
    boost::json::object deniedCmd{{"commandId","rbac-denied"},{"deviceId","env-01"},{"action","device.selfTest"},{"ttlMs",5000}};
    if(secured.issueCommand(deniedCmd,observerCtx).httpStatus!=403){std::cerr<<"observer command RBAC failed\n";return 18;}
    boost::json::object allowedCmd{{"commandId","rbac-ok"},{"deviceId","env-01"},{"action","device.selfTest"},{"ttlMs",5000}};
    if(secured.issueCommand(allowedCmd,operatorCtx).httpStatus!=202){std::cerr<<"operator command RBAC failed\n";return 19;}
    boost::json::object emergencyDenied{{"commandId","rbac-emergency-denied"},{"deviceId","safety-01"},{"action","device.emergencyStop"},{"ttlMs",5000},{"reason","test"}};
    if(secured.issueCommand(emergencyDenied,operatorCtx).httpStatus!=403){std::cerr<<"emergency highest privilege RBAC failed\n";return 20;}
    smart_factory::AuditQuery q; q.limit=100;
    if(secured.auditHistory(q,observerCtx).if_contains("error")==nullptr){std::cerr<<"audit view RBAC failed\n";return 21;}
    auto audit=secured.auditHistory(q,adminCtx); if(!audit.at("enabled").as_bool()||audit.at("items").as_array().empty()){std::cerr<<"audit persistence/query failed\n";return 22;}
  }
  std::filesystem::remove_all(securityRoot);

  std::cout << "backend smoke PASS\n";
  return 0;
}
