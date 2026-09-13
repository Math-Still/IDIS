#include "smart_factory/application.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/platform/config_service.hpp"

#include <boost/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const std::string& message){ if(!value) throw std::runtime_error(message); }
std::filesystem::path tempDir(){ auto p=std::filesystem::temp_directory_path()/("smart-factory-b3-vertical-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); std::filesystem::create_directories(p); return p; }
std::int64_t nowMs(){ return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string str(const boost::json::object& o,const char* k){const auto*v=o.if_contains(k);return v&&v->is_string()?std::string(v->as_string()):std::string();}

class SequenceAdapter final : public smart_factory::DeviceAdapter, public smart_factory::DeviceAdapterBatchExtension {
 public:
  std::string name() const override { return "b3-sequence"; }
  bool simulated() const override { return true; }
  bool start(std::string& detail) override { detail="ready"; return true; }
  void stop() noexcept override {}
  smart_factory::DeviceAdapterHealth health() const override { return {true,"READY","b3 sequence",0,0}; }
  smart_factory::DeviceDataSnapshot readSnapshot() override { throw std::runtime_error("batch only"); }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override { return {false,"not used"}; }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override { return {smart_factory::DeviceFeedbackState::Unsupported,"not used","null"}; }
  smart_factory::DeviceAcquisitionBatch readBatch() override {
    const auto n=++reads_; const auto ts=nowMs();
    const double value=n<=20?31.5:27.0;
    smart_factory::DeviceAcquisitionBatch b; b.sourceEpoch="b3-seq"; b.ownedDeviceIds={"env-02"}; b.completeDeviceIds={"env-02"}; b.snapshot.generatedAt=ts;
    b.snapshot.devices={boost::json::object{{"id","env-02"},{"name","B3 Env"},{"status","ONLINE"},{"scenario","temperature_humidity"}}};
    b.snapshot.telemetry={boost::json::object{{"deviceId","env-02"},{"pointId","temperature"},{"label","温度"},{"unit","℃"},{"value",value},{"quality","GOOD"},{"sampleTs",ts},{"receiveTs",ts},{"seq",static_cast<std::int64_t>(n)}}};
    b.snapshot.communication={boost::json::object{{"deviceId","env-02"},{"online",true},{"quality","GOOD"}}};
    return b;
  }
 private: std::atomic<int> reads_{0};
};
}

int main(){
  const auto root=tempDir();
  try {
    auto registry=smart_factory::DeviceRegistry::load(std::string(SMART_FACTORY_SOURCE_DIR)+"/backend-cpp/config/devices.development.json");
    const auto configDir=(root/"config").string();
    {
      smart_factory::ConfigService seed(configDir,"B3 Vertical",registry);
      auto snap=seed.activeSnapshot();
      auto& rules=snap.at("rules").as_array(); bool changed=false;
      for(auto& value:rules){auto& rule=value.as_object(); if(str(rule,"ruleId")=="temperature-high-env-02"){rule["durationMs"]=0;changed=true;}}
      require(changed,"env-02 bootstrap rule missing");
      const auto base=std::string(seed.status().at("publishedRevision").as_string());
      auto draft=seed.createDraft(base,snap,"engineer");
      auto pub=seed.publishDraft(std::string(draft.at("draftId").as_string()),"engineer","B3 vertical duration=0");
      require(pub.at("ok").as_bool()&&pub.at("state").as_string()=="RUNNING","B3 seed rule publish failed");
    }

    smart_factory::BackendConfig cfg;
    cfg.mode="development"; cfg.siteName="B3 Vertical"; cfg.platformConfigEnabled=true; cfg.platformConfigDirectory=configDir;
    cfg.historianEnabled=true; cfg.historianDirectory=(root/"historian").string(); cfg.historianBackupEnabled=false;
    cfg.commandLedgerEnabled=false; cfg.auditEnabled=false; cfg.authEnabled=false; cfg.enableRealtime=false;
    cfg.acquisitionPollMs=20; cfg.telemetryPublishMs=20; cfg.workerThreads=1;
    std::atomic<int> events{0};
    smart_factory::ApplicationService app(cfg,std::make_unique<SequenceAdapter>(),[&](const std::string&){++events;},nullptr,registry);

    std::string occurrenceId;
    for(int i=0;i<15&&!occurrenceId.size();++i){
      for(const auto& value:app.alarmOccurrencesV2("env-02","RULE")){const auto&o=value.as_object();if(str(o,"conditionState")=="ACTIVE") occurrenceId=str(o,"occurrenceId");}
      if(occurrenceId.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(!occurrenceId.empty(),"vertical rule occurrence did not activate");

    smart_factory::SecurityContext op; op.principal={"operator","Operator",smart_factory::UserRole::Operator,true};
    auto incidentResult=app.createIncidentV2({{"title","B3 温度处置"},{"occurrenceIds",boost::json::array{occurrenceId}}},op);
    require(incidentResult.httpStatus==201,"incident create failed");
    const auto incidentId=str(incidentResult.body,"incidentId");
    auto started=app.transitionIncidentV2(incidentId,{{"action","START"},{"expectedRevision",1},{"comment","开始核查"}},op);
    require(started.httpStatus==200,"incident start failed");
    auto resolved=app.transitionIncidentV2(incidentId,{{"action","RESOLVE"},{"expectedRevision",2},{"comment","等待过程恢复"}},op);
    require(resolved.httpStatus==200,"incident resolve failed");
    smart_factory::SecurityContext engineer; engineer.principal={"engineer","Engineer",smart_factory::UserRole::Engineer,true};
    auto earlyClose=app.transitionIncidentV2(incidentId,{{"action","CLOSE"},{"expectedRevision",3},{"comment","过早关闭"}},engineer);
    require(earlyClose.httpStatus==409,"active/unacknowledged occurrence must block close");

    bool normalized=false;
    for(int i=0;i<80&&!normalized;++i){
      for(const auto& value:app.alarmOccurrencesV2("env-02","RULE")){const auto&o=value.as_object();if(str(o,"occurrenceId")==occurrenceId&&str(o,"conditionState")=="NORMAL") normalized=true;}
      if(!normalized) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(normalized,"vertical rule occurrence did not return to normal");
    auto ack=app.acknowledgeAlarmOccurrenceV2(occurrenceId,{{"comment","恢复后确认"}},op);
    require(ack.httpStatus==200&&str(ack.body,"ackState")=="ACKNOWLEDGED"&&str(ack.body,"conditionState")=="NORMAL","restore-before-ack must be accepted");
    auto close=app.transitionIncidentV2(incidentId,{{"action","CLOSE"},{"expectedRevision",3},{"comment","条件恢复且已确认"}},engineer);
    require(close.httpStatus==200&&str(close.body,"status")=="CLOSED","incident close after normal+ack failed");
    auto timeline=app.timelineV2(0,INT64_MAX);
    require(timeline.at("items").as_array().size()>=6,"timeline must include alarm+incident lifecycle events");
    require(events.load()>0,"realtime events should be emitted");

    std::filesystem::remove_all(root);
    std::cout<<"platform_vertical_b3 PASS\n"; return 0;
  } catch(const std::exception& e){ std::cerr<<"platform_vertical_b3 FAIL: "<<e.what()<<"\n"; std::filesystem::remove_all(root); return 1; }
}
