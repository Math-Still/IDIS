#include "smart_factory/application.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"

#include <atomic>
#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace {
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

smart_factory::SecurityContext actor(std::string id="operator-1",
                                     smart_factory::UserRole role=smart_factory::UserRole::Administrator) {
  smart_factory::SecurityContext ctx;
  ctx.principal = {std::move(id), "B4 User", role, true};
  ctx.remoteAddress = "127.0.0.1";
  return ctx;
}

std::shared_ptr<smart_factory::DeviceRegistry> registry(const std::filesystem::path& file) {
  std::ofstream out(file);
  out << R"JSON({
    "schemaVersion":"1.1","site":"b4-test","devices":[{
      "id":"ctrl-01","name":"Control Device","scenario":"temperature_humidity","location":"Lab",
      "connection":{"interfaceType":"TEST","protocol":"Test","driver":"b4.test"},
      "points":[{"pointId":"pv","label":"PV","valueType":"number","samplePeriodMs":50}],
      "commands":{
        "device.setValue":{"displayName":"Set value","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":true,"parameterSchema":{"type":"number","required":true,"minimum":0,"maximum":100}},
        "device.deliveryNotSent":{"displayName":"Not sent","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false}},
        "device.deliveryUnknown":{"displayName":"Unknown delivery","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false}},
        "device.feedbackTimeout":{"displayName":"Feedback timeout","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false}},
        "device.slow":{"displayName":"Slow","requiredPermission":"command.issue","executionClass":"NORMAL","reasonRequired":false,"parameterSchema":{"type":"none","required":false}}
      }
    }]})JSON";
  out.close();
  return smart_factory::DeviceRegistry::load(file.string());
}

struct SharedState {
  std::atomic<int> executeCount{0};
  std::atomic<bool> online{true};
};

class B4Adapter final : public smart_factory::DeviceAdapter,
                        public smart_factory::DeviceCommandExecutionExtension {
 public:
  explicit B4Adapter(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}
  std::string name() const override { return "b4-bounded-sim"; }
  bool simulated() const override { return true; }
  smart_factory::DeviceCommandExecutionIsolation executionIsolation() const noexcept override {
    return smart_factory::DeviceCommandExecutionIsolation::BoundedInProcess;
  }
  smart_factory::DeviceDataSnapshot readSnapshot() override {
    const auto now=nowMs();
    smart_factory::DeviceDataSnapshot s; s.generatedAt=now;
    const bool online=state_->online.load();
    s.devices.emplace_back(boost::json::object{{"id","ctrl-01"},{"name","Control Device"},{"scenario","temperature_humidity"},
      {"status",online?"ONLINE":"OFFLINE"},{"location","Lab"},{"protocol","Test"},{"osNode","edge"},{"lastSeen",now},{"tags",boost::json::array{}}});
    if (online) s.telemetry.emplace_back(boost::json::object{{"deviceId","ctrl-01"},{"pointId","pv"},{"label","PV"},{"value",1.0},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",++seq_}});
    s.communication.emplace_back(boost::json::object{{"deviceId","ctrl-01"},{"online",online},{"quality",online?"GOOD":"BAD"},{"lastSeen",now}});
    return s;
  }
  smart_factory::DeviceCommandDeliveryResult executeWithDelivery(const smart_factory::DeviceCommand& command,
                                                                  std::int64_t) override {
    ++state_->executeCount;
    if (command.action=="device.deliveryNotSent")
      return {smart_factory::DeviceCommandDeliveryCertainty::NotSent,"test proved request did not leave transport"};
    if (command.action=="device.deliveryUnknown")
      return {smart_factory::DeviceCommandDeliveryCertainty::PossiblyApplied,"test lost acknowledgement after write"};
    if (command.action=="device.slow") {
      std::this_thread::sleep_for(std::chrono::milliseconds(140));
      return {smart_factory::DeviceCommandDeliveryCertainty::Applied,"late bounded-contract return"};
    }
    std::lock_guard lock(mutex_);
    feedback_[command.commandId]=command.action;
    return {smart_factory::DeviceCommandDeliveryCertainty::Applied,"adapter boundary accepted"};
  }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override {
    ++state_->executeCount; return {true,"legacy execute should not be used by B4 extension"};
  }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand& command) override {
    if (command.action=="device.feedbackTimeout") return {smart_factory::DeviceFeedbackState::Pending,"withheld","null"};
    return {smart_factory::DeviceFeedbackState::Confirmed,"independent feedback confirmed","true"};
  }
 private:
  std::shared_ptr<SharedState> state_;
  std::atomic<std::int64_t> seq_{0};
  std::mutex mutex_;
  std::unordered_map<std::string,std::string> feedback_;
};

class UnsafeRealAdapter final : public smart_factory::DeviceAdapter {
 public:
  explicit UnsafeRealAdapter(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}
  std::string name() const override { return "unsafe-real-v1"; }
  bool simulated() const override { return false; }
  smart_factory::DeviceDataSnapshot readSnapshot() override {
    const auto now=nowMs(); smart_factory::DeviceDataSnapshot s; s.generatedAt=now;
    s.devices.emplace_back(boost::json::object{{"id","ctrl-01"},{"name","Control Device"},{"scenario","temperature_humidity"},{"status","ONLINE"},{"location","Lab"},{"protocol","Test"},{"osNode","edge"},{"lastSeen",now},{"tags",boost::json::array{}}});
    s.telemetry.emplace_back(boost::json::object{{"deviceId","ctrl-01"},{"pointId","pv"},{"label","PV"},{"value",1.0},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",1}});
    s.communication.emplace_back(boost::json::object{{"deviceId","ctrl-01"},{"online",true},{"quality","GOOD"},{"lastSeen",now}});
    return s;
  }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override {
    ++state_->executeCount; return {true,"unsafe real call unexpectedly executed"};
  }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override {
    return {smart_factory::DeviceFeedbackState::Confirmed,"confirmed","true"};
  }
 private: std::shared_ptr<SharedState> state_;
};

std::string stateOf(const smart_factory::ApplicationService& app,const std::string& id) {
  auto c=app.commandV2(id,actor());
  const auto* v=c.if_contains("state"); return v&&v->is_string()?std::string(v->as_string()):std::string{};
}
bool waitState(const smart_factory::ApplicationService& app,const std::string& id,const std::string& expected,int timeout=1500) {
  auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
  while(std::chrono::steady_clock::now()<end){if(stateOf(app,id)==expected)return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  return stateOf(app,id)==expected;
}
boost::json::object previewReq(double value=12.5) {
  return {{"deviceId","ctrl-01"},{"action","device.setValue"},{"requestedValue",value},{"reason","B4 controlled test"}};
}
boost::json::object submitFrom(const boost::json::object& p,const std::string& key,const std::string& commandId="") {
  boost::json::object r{{"previewId",p.at("previewId")},{"idempotencyKey",key},{"deviceId",p.at("deviceId")},{"action",p.at("action")},{"requestedValue",p.at("requestedValue")},{"reason",p.at("reason")},{"ttlMs",2000}};
  if(!commandId.empty()) r["commandId"]=commandId;
  return r;
}
}

int main(){
  const auto root=std::filesystem::temp_directory_path()/ ("sf-b4-"+std::to_string(nowMs()));
  std::filesystem::create_directories(root);
  auto reg=registry(root/"devices.json");
  { std::ofstream auth(root/"auth.json"); auth << R"JSON({"users":[{"id":"placeholder","displayName":"Placeholder","role":"OBSERVER","tokenSha256":"0000000000000000000000000000000000000000000000000000000000000000","enabled":true}]})JSON"; }
  smart_factory::BackendConfig cfg; cfg.enableRealtime=false; cfg.historianEnabled=false; cfg.auditEnabled=false; cfg.authEnabled=true; cfg.authUsersFile=(root/"auth.json").string();
  cfg.commandLedgerEnabled=true; cfg.commandLedgerDirectory=(root/"commands").string(); cfg.commandLedgerMaxRecords=100;
  cfg.platformConfigEnabled=true; cfg.platformConfigDirectory=(root/"config").string(); cfg.commandExecutionTimeoutMs=80;
  cfg.commandFeedbackTimeoutMs=120; cfg.commandFeedbackPollMs=10; cfg.acquisitionPollMs=30;

  auto shared=std::make_shared<SharedState>();
  {
    smart_factory::ApplicationService app(cfg,std::make_unique<B4Adapter>(shared),[](const std::string&){},nullptr,reg);
    auto p=app.previewCommandV2(previewReq(),actor());
    if(p.httpStatus!=200){std::cerr<<"preview failed\n";return 1;}
    auto first=app.issueCommandV2(submitFrom(p.body,"idem-1","cmd-a"),actor());
    if(first.httpStatus!=202||!waitState(app,"cmd-a","CONFIRMED")){std::cerr<<"normal v2 command failed\n";return 2;}
    if(shared->executeCount.load()!=1){std::cerr<<"normal execute count invalid\n";return 3;}

    auto p2=app.previewCommandV2(previewReq(),actor());
    auto replay=app.issueCommandV2(submitFrom(p2.body,"idem-1","cmd-b"),actor());
    if(replay.httpStatus!=200||std::string(replay.body.at("id").as_string())!="cmd-a"||shared->executeCount.load()!=1){std::cerr<<"idempotency key replayed physical command\n";return 4;}
    auto conflictReq=previewReq(13.5); auto p3=app.previewCommandV2(conflictReq,actor());
    auto conflict=app.issueCommandV2(submitFrom(p3.body,"idem-1","cmd-c"),actor());
    if(conflict.httpStatus!=409||std::string(conflict.body.at("code").as_string())!="COMMAND_IDEMPOTENCY_CONFLICT"){std::cerr<<"idempotency content conflict missing\n";return 5;}

    boost::json::object notSent{{"deviceId","ctrl-01"},{"action","device.deliveryNotSent"}};
    auto p4=app.previewCommandV2(notSent,actor()); auto r4=app.issueCommandV2(submitFrom(p4.body,"idem-not-sent","cmd-not-sent"),actor());
    if(r4.httpStatus!=202||!waitState(app,"cmd-not-sent","FAILED")){std::cerr<<"NOT_SENT did not become proven failure\n";return 6;}
    auto c4=app.commandV2("cmd-not-sent",actor()); if(std::string(c4.at("deliveryCertainty").as_string())!="NOT_SENT"){std::cerr<<"NOT_SENT certainty missing\n";return 7;}

    boost::json::object unknown{{"deviceId","ctrl-01"},{"action","device.deliveryUnknown"}};
    auto p5=app.previewCommandV2(unknown,actor()); auto r5=app.issueCommandV2(submitFrom(p5.body,"idem-unknown","cmd-unknown"),actor());
    if(r5.httpStatus!=202||!waitState(app,"cmd-unknown","OUTCOME_UNKNOWN")){std::cerr<<"POSSIBLY_APPLIED did not become unknown\n";return 8;}
    auto blocked=app.previewCommandV2(unknown,actor());
    if(blocked.httpStatus!=409||std::string(blocked.body.at("code").as_string())!="COMMAND_UNRESOLVED_BLOCK"){std::cerr<<"unresolved unknown did not block duplicate action\n";return 9;}
    auto recon=app.reconcileCommandV2("cmd-unknown",boost::json::object{{"conclusion","CONFIRMED_NOT_APPLIED"},{"evidence","operator inspected independent device state"},{"allowRetry",true}},actor("engineer-1",smart_factory::UserRole::Engineer));
    if(recon.httpStatus!=200||std::string(recon.body.at("state").as_string())!="OUTCOME_UNKNOWN"||recon.body.at("reconciliations").as_array().size()!=1){std::cerr<<"reconciliation rewrote or failed to preserve unknown\n";return 10;}
    auto released=app.previewCommandV2(unknown,actor());
    if(released.httpStatus!=200){std::cerr<<"allowRetry reconciliation did not release action lock\n";return 11;}

    boost::json::object feedbackTimeout{{"deviceId","ctrl-01"},{"action","device.feedbackTimeout"}};
    auto p6=app.previewCommandV2(feedbackTimeout,actor()); app.issueCommandV2(submitFrom(p6.body,"idem-feedback","cmd-feedback"),actor());
    if(!waitState(app,"cmd-feedback","OUTCOME_UNKNOWN",1000)){std::cerr<<"feedback timeout did not become unknown\n";return 10;}

    auto oldPreview=app.previewCommandV2(previewReq(20),actor());
    auto observer=actor("operator-1",smart_factory::UserRole::Observer);
    auto permissionChanged=app.issueCommandV2(submitFrom(oldPreview.body,"idem-permission","cmd-permission"),observer);
    if(permissionChanged.httpStatus!=403){std::cerr<<"permission was not revalidated at submit\n";return 11;}

    auto offlinePreview=app.previewCommandV2(previewReq(21),actor());
    shared->online.store(false); std::this_thread::sleep_for(std::chrono::milliseconds(120));
    auto offlineSubmit=app.issueCommandV2(submitFrom(offlinePreview.body,"idem-offline","cmd-offline"),actor());
    if(offlineSubmit.httpStatus!=409||!offlineSubmit.body.if_contains("code")||std::string(offlineSubmit.body.at("code").as_string())!="COMMAND_PREVIEW_DEVICE_STATE_CHANGED"){std::cerr<<"device state was not revalidated status="<<offlineSubmit.httpStatus<<" body="<<boost::json::serialize(offlineSubmit.body)<<"\n";return 12;}
    shared->online.store(true); std::this_thread::sleep_for(std::chrono::milliseconds(100));

    boost::json::object slow{{"deviceId","ctrl-01"},{"action","device.slow"}};
    auto ps=app.previewCommandV2(slow,actor()); app.issueCommandV2(submitFrom(ps.body,"idem-slow","cmd-slow"),actor());
    if(!waitState(app,"cmd-slow","OUTCOME_UNKNOWN",1000)){std::cerr<<"bounded contract overrun did not become unknown\n";return 13;}
    auto afterQuarantine=app.previewCommandV2(previewReq(22),actor());
    if(afterQuarantine.httpStatus!=409||std::string(afterQuarantine.body.at("code").as_string())!="DRIVER_ISOLATION_REQUIRED"){std::cerr<<"adapter quarantine did not stop new writes\n";return 14;}
  }

  // Restart: same idempotency key returns durable original command, never re-executes.
  shared->online.store(true);
  const int beforeRestart=shared->executeCount.load();
  {
    smart_factory::ApplicationService app(cfg,std::make_unique<B4Adapter>(shared),{},nullptr,reg);
    auto p=app.previewCommandV2(previewReq(),actor()); auto replay=app.issueCommandV2(submitFrom(p.body,"idem-1","cmd-after-restart"),actor());
    if(replay.httpStatus!=200||std::string(replay.body.at("id").as_string())!="cmd-a"||shared->executeCount.load()!=beforeRestart){std::cerr<<"restart idempotency failed\n";return 15;}
  }

  // A real legacy v1 adapter without bounded/isolated execution is denied before execute().
  auto unsafeState=std::make_shared<SharedState>();
  {
    smart_factory::BackendConfig unsafeCfg=cfg; unsafeCfg.commandLedgerDirectory=(root/"unsafe-commands").string(); unsafeCfg.platformConfigDirectory=(root/"unsafe-config").string();
    smart_factory::ApplicationService app(unsafeCfg,std::make_unique<UnsafeRealAdapter>(unsafeState),{},nullptr,reg);
    auto p=app.previewCommandV2(previewReq(),actor());
    if(p.httpStatus!=409||std::string(p.body.at("code").as_string())!="DRIVER_ISOLATION_REQUIRED"||unsafeState->executeCount.load()!=0){std::cerr<<"unsafe real driver crossed write boundary\n";return 16;}
  }

  std::filesystem::remove_all(root);
  std::cout<<"platform command B4 PASS\n";
  return 0;
}
