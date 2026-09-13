#include "smart_factory/application.hpp"
#include "smart_factory/command_ledger.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"

#include <atomic>
#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

smart_factory::SecurityContext actor() {
  smart_factory::SecurityContext ctx;
  ctx.principal = {"alpha2-admin", "Alpha2 Admin", smart_factory::UserRole::Administrator, true};
  ctx.remoteAddress = "127.0.0.1";
  return ctx;
}

std::shared_ptr<smart_factory::DeviceRegistry> makeRegistry(const std::filesystem::path& path) {
  std::ofstream out(path);
  out << R"JSON({
    "schemaVersion":"1.1",
    "site":"alpha2-test",
    "devices":[{
      "id":"ctrl-01","name":"Control Device","scenario":"temperature_humidity","location":"Lab",
      "connection":{"interfaceType":"TEST","protocol":"Test","driver":"test"},
      "points":[{"pointId":"pv","label":"PV","valueType":"number","samplePeriodMs":1000}],
      "commands":{
        "device.selfTest":{"displayName":"Self test","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false}},
        "device.other":{"displayName":"Other","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false}},
        "device.slowRealtime":{"displayName":"Slow RT","requiredPermission":"command.issue","executionClass":"REALTIME","reasonRequired":false,"parameterSchema":{"type":"none","required":false}},
        "device.emergencyStop":{"displayName":"Emergency stop","requiredPermission":"command.emergency","executionClass":"EMERGENCY","reasonRequired":true,"parameterSchema":{"type":"none","required":false}}
      }
    }]
  })JSON";
  out.close();
  return smart_factory::DeviceRegistry::load(path.string());
}

struct SharedState {
  std::atomic<int> executeCount{0};
  std::atomic<bool> slowStarted{false};
  std::atomic<bool> slowFinished{false};
  std::atomic<bool> emergencyBeforeSlowFinished{false};
};

class Alpha2Adapter final : public smart_factory::DeviceAdapter {
 public:
  explicit Alpha2Adapter(std::shared_ptr<SharedState> state, int slowMs = 0)
      : state_(std::move(state)), slowMs_(slowMs) {}
  std::string name() const override { return "alpha2-test-adapter"; }
  bool simulated() const override { return true; }
  smart_factory::DeviceDataSnapshot readSnapshot() override {
    const auto now = nowMs();
    smart_factory::DeviceDataSnapshot snapshot;
    snapshot.generatedAt = now;
    snapshot.devices.emplace_back(boost::json::object{{"id","ctrl-01"},{"name","Control Device"},{"scenario","temperature_humidity"},
      {"status","ONLINE"},{"location","Lab"},{"protocol","Test"},{"osNode","test"},{"lastSeen",now},{"tags",boost::json::array{}}});
    snapshot.telemetry.emplace_back(boost::json::object{{"deviceId","ctrl-01"},{"pointId","pv"},{"label","PV"},{"value",1.0},
      {"unit",""},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",1}});
    snapshot.communication.emplace_back(boost::json::object{{"deviceId","ctrl-01"},{"online",true},{"quality","GOOD"},
      {"latencyMsP50",1},{"latencyMsP95",1},{"latencyMsP99",1},{"jitterMs",0},{"messageRatePerMin",60},
      {"reconnectCount",0},{"errorCount",0},{"lastSeen",now}});
    return snapshot;
  }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand& command) override {
    ++state_->executeCount;
    if (command.action == "device.slowRealtime") {
      state_->slowStarted.store(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(slowMs_));
      state_->slowFinished.store(true);
    } else if (command.action == "device.emergencyStop") {
      if (state_->slowStarted.load() && !state_->slowFinished.load()) {
        state_->emergencyBeforeSlowFinished.store(true);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {true, "adapter applied"};
  }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override {
    return {smart_factory::DeviceFeedbackState::Confirmed, "field confirmed", "true"};
  }
 private:
  std::shared_ptr<SharedState> state_;
  int slowMs_{0};
};

std::string commandState(const smart_factory::ApplicationService& app, const std::string& id) {
  const auto snapshot = app.dashboardSnapshot();
  for (const auto& value : snapshot.at("commands").as_array()) {
    const auto& command = value.as_object();
    if (std::string(command.at("id").as_string()) == id) return std::string(command.at("state").as_string());
  }
  return {};
}

bool waitState(const smart_factory::ApplicationService& app, const std::string& id,
               const std::string& expected, int timeoutMs = 2000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (commandState(app, id) == expected) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return commandState(app, id) == expected;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / ("smart-factory-alpha2-" + std::to_string(nowMs()));
  std::filesystem::create_directories(root);
  const auto registryPath = root / "devices.json";
  auto registry = makeRegistry(registryPath);

  // 1) Durable idempotency: a confirmed physical command must never execute a
  // second time solely because the backend process restarted.
  auto persistedState = std::make_shared<SharedState>();
  smart_factory::BackendConfig persistConfig;
  persistConfig.enableRealtime = false;
  persistConfig.historianEnabled = false;
  persistConfig.auditEnabled = false;
  persistConfig.commandLedgerEnabled = true;
  persistConfig.commandLedgerDirectory = (root / "ledger").string();
  persistConfig.commandLedgerMaxRecords = 5000;
  persistConfig.commandDashboardLimit = 100;
  persistConfig.commandFeedbackTimeoutMs = 500;
  persistConfig.commandFeedbackPollMs = 10;

  boost::json::object durableRequest{{"commandId","durable-1"},{"deviceId","ctrl-01"},{"action","device.selfTest"},{"ttlMs",5000}};
  {
    smart_factory::ApplicationService app(persistConfig, std::make_unique<Alpha2Adapter>(persistedState), {}, nullptr, registry);
    if (app.issueCommand(durableRequest, actor()).httpStatus != 202 || !waitState(app, "durable-1", "CONFIRMED")) {
      std::cerr << "durable command first execution failed\n"; return 1;
    }
  }
  if (persistedState->executeCount.load() != 1) { std::cerr << "durable first execution count invalid\n"; return 2; }
  {
    smart_factory::ApplicationService app(persistConfig, std::make_unique<Alpha2Adapter>(persistedState), {}, nullptr, registry);
    const auto replay = app.issueCommand(durableRequest, actor());
    if (replay.httpStatus != 200 || std::string(replay.body.at("state").as_string()) != "CONFIRMED") {
      std::cerr << "durable replay did not return persisted terminal command\n"; return 3;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (persistedState->executeCount.load() != 1) { std::cerr << "backend restart replayed physical command\n"; return 4; }
    auto conflicting = durableRequest;
    conflicting["action"] = "device.other";
    if (app.issueCommand(conflicting, actor()).httpStatus != 409) {
      std::cerr << "durable commandId fingerprint conflict not enforced\n"; return 5;
    }
  }

  // 2) Interrupted commands are recovered as OUTCOME_UNKNOWN and are never
  // automatically replayed at startup.
  {
    smart_factory::CommandLedger ledger((root / "ledger").string(), 5000);
    const auto t = nowMs();
    boost::json::object interrupted{{"id","interrupted-1"},{"deviceId","ctrl-01"},{"action","device.selfTest"},
      {"state","APPLIED"},{"issuedAt",t-100},{"updatedAt",t-50},{"expiresAt",t+5000},{"lastEventSeq",4},
      {"operator","alpha2-admin"},{"operatorRole","ADMINISTRATOR"}};
    if (!ledger.insert("interrupted-1", "manual-interrupted-fingerprint", interrupted)) {
      std::cerr << "failed to seed interrupted command\n"; return 6;
    }
  }
  {
    auto noReplayState = std::make_shared<SharedState>();
    smart_factory::ApplicationService app(persistConfig, std::make_unique<Alpha2Adapter>(noReplayState), {}, nullptr, registry);
    if (!waitState(app, "interrupted-1", "OUTCOME_UNKNOWN", 100) || noReplayState->executeCount.load() != 0) {
      std::cerr << "interrupted command recovery was not fail-safe\n"; return 7;
    }
    smart_factory::CommandLedger ledger((root / "ledger").string(), 5000);
    auto row = ledger.find("interrupted-1");
    if (!row || std::string(row->command.at("state").as_string()) != "OUTCOME_UNKNOWN") {
      std::cerr << "OUTCOME_UNKNOWN recovery was not durable\n"; return 8;
    }
  }

  // 3) Emergency work has an independent realtime worker. It must not wait
  // behind a currently blocking ordinary realtime action.
  {
    auto state = std::make_shared<SharedState>();
    smart_factory::BackendConfig cfg;
    cfg.enableRealtime = true;
    cfg.enableEmergencyControl = true;
    cfg.requireRealtime = false;
    cfg.realtimePriority = 70;
    cfg.emergencyRealtimePriority = 80;
    cfg.realtimeQueueCapacity = 4;
    cfg.emergencyQueueCapacity = 2;
    cfg.commandExecutionTimeoutMs = 1000;
    cfg.commandFeedbackTimeoutMs = 500;
    cfg.commandFeedbackPollMs = 10;
    cfg.historianEnabled = false;
    cfg.auditEnabled = false;
    smart_factory::ApplicationService app(cfg, std::make_unique<Alpha2Adapter>(state, 350), {}, nullptr, registry);
    boost::json::object slow{{"commandId","rt-slow"},{"deviceId","ctrl-01"},{"action","device.slowRealtime"},{"ttlMs",3000}};
    if (app.issueCommand(slow, actor()).httpStatus != 202) { std::cerr << "slow realtime command rejected\n"; return 9; }
    for (int i = 0; i < 100 && !state->slowStarted.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!state->slowStarted.load()) { std::cerr << "slow realtime command did not start\n"; return 10; }
    boost::json::object emergency{{"commandId","emergency-1"},{"deviceId","ctrl-01"},{"action","device.emergencyStop"},{"ttlMs",1000},{"reason","alpha2 isolation test"}};
    if (app.issueCommand(emergency, actor()).httpStatus != 202 || !waitState(app, "emergency-1", "CONFIRMED", 250)) {
      std::cerr << "emergency command was blocked by ordinary realtime work\n"; return 11;
    }
    if (!state->emergencyBeforeSlowFinished.load()) {
      std::cerr << "emergency action did not execute independently of realtime worker\n"; return 12;
    }
  }

  // 4) Realtime queues are bounded. Once the only worker is busy and the
  // configured queue slot is occupied, admission must fail instead of growing
  // memory without bound. Slow adapter execution also becomes OUTCOME_UNKNOWN
  // after the execution watchdog deadline rather than a false success.
  {
    auto state = std::make_shared<SharedState>();
    smart_factory::BackendConfig cfg;
    cfg.enableRealtime = true;
    cfg.enableEmergencyControl = false;
    cfg.requireRealtime = false;
    cfg.realtimeQueueCapacity = 1;
    cfg.commandExecutionTimeoutMs = 100;
    cfg.commandFeedbackTimeoutMs = 300;
    cfg.commandFeedbackPollMs = 10;
    cfg.historianEnabled = false;
    cfg.auditEnabled = false;
    smart_factory::ApplicationService app(cfg, std::make_unique<Alpha2Adapter>(state, 300), {}, nullptr, registry);
    boost::json::object first{{"commandId","queue-1"},{"deviceId","ctrl-01"},{"action","device.slowRealtime"},{"ttlMs",2000}};
    boost::json::object second{{"commandId","queue-2"},{"deviceId","ctrl-01"},{"action","device.slowRealtime"},{"ttlMs",2000}};
    boost::json::object third{{"commandId","queue-3"},{"deviceId","ctrl-01"},{"action","device.slowRealtime"},{"ttlMs",2000}};
    if (app.issueCommand(first, actor()).httpStatus != 202) { std::cerr << "queue first rejected\n"; return 13; }
    for (int i = 0; i < 100 && !state->slowStarted.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (app.issueCommand(second, actor()).httpStatus != 202) { std::cerr << "queue second was not admitted\n"; return 14; }
    const auto overflow = app.issueCommand(third, actor());
    if (overflow.httpStatus != 503 || !overflow.body.if_contains("code") ||
        std::string(overflow.body.at("code").as_string()) != "COMMAND_QUEUE_FULL") {
      std::cerr << "bounded realtime queue did not reject overflow\n"; return 15;
    }
    if (!waitState(app, "queue-1", "OUTCOME_UNKNOWN", 1000)) {
      std::cerr << "execution deadline overrun did not surface as OUTCOME_UNKNOWN\n"; return 16;
    }
    const auto status = app.runtimeStatusJson();
    const auto& rt = status.at("executors").as_object().at("realtime").as_object();
    if (rt.at("queueCapacity").as_int64() != 1 || rt.at("rejected").as_int64() < 1) {
      std::cerr << "executor queue metrics are inconsistent\n"; return 17;
    }
  }

  std::filesystem::remove_all(root);
  std::cout << "command runtime alpha2 PASS\n";
  return 0;
}
