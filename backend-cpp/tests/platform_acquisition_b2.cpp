#include "smart_factory/application.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/historian.hpp"
#include "smart_factory/platform/acquisition_service.hpp"
#include "smart_factory/platform/quality_service.hpp"

#include <boost/json.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <atomic>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::filesystem::path tempDir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto p = std::filesystem::temp_directory_path() / ("smart-factory-b2-" + std::to_string(stamp));
  std::filesystem::create_directories(p); return p;
}
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}
const boost::json::object* findPoint(const boost::json::array& points, const std::string& deviceId, const std::string& pointId) {
  for (const auto& v : points) if (v.is_object()) {
    const auto& p = v.as_object();
    const auto* d=p.if_contains("deviceId"); const auto* id=p.if_contains("pointId");
    if(d&&d->is_string()&&id&&id->is_string()&&d->as_string()==deviceId&&id->as_string()==pointId) return &p;
  }
  return nullptr;
}
const boost::json::object* findDevice(const boost::json::array& values, const std::string& deviceId, const char* key="id") {
  for (const auto& v : values) if (v.is_object()) {
    const auto& o=v.as_object(); const auto* id=o.if_contains(key);
    if(id&&id->is_string()&&id->as_string()==deviceId) return &o;
  }
  return nullptr;
}

class BatchTestAdapter final : public smart_factory::DeviceAdapter, public smart_factory::DeviceAdapterBatchExtension {
 public:
  std::string name() const override { return "b2-batch-test"; }
  bool simulated() const override { return true; }
  bool start(std::string& detail) override { detail="ready"; return true; }
  void stop() noexcept override {}
  smart_factory::DeviceAdapterHealth health() const override { return {true,"READY","b2 test adapter",0,0}; }
  smart_factory::DeviceDataSnapshot readSnapshot() override { throw std::runtime_error("B2 test must use batch extension"); }
  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override { return {false,"not used"}; }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override { return {smart_factory::DeviceFeedbackState::Unsupported,"not used","null"}; }

  smart_factory::DeviceAcquisitionBatch readBatch() override {
    std::lock_guard lock(mutex_);
    const auto now=nowMs(); const auto n=++reads_;
    smart_factory::DeviceAcquisitionBatch batch;
    batch.sourceEpoch = n < 3 ? "batch-epoch-1" : "batch-epoch-2";
    batch.ownedDeviceIds={"env-01","env-02"};
    batch.snapshot.generatedAt=now;
    if(n==1){
      batch.completeDeviceIds={"env-01","env-02"};
      batch.snapshot.devices={boost::json::object{{"id","env-01"},{"name","A"},{"status","ONLINE"}},boost::json::object{{"id","env-02"},{"name","B"},{"status","ONLINE"}}};
      batch.snapshot.telemetry={
        boost::json::object{{"deviceId","env-01"},{"pointId","temperature"},{"value",25.0},{"quality","GOOD"},{"sampleTs",now-5},{"receiveTs",now},{"seq",10}},
        boost::json::object{{"deviceId","env-02"},{"pointId","temperature"},{"value",30.0},{"quality","GOOD"},{"sampleTs",now-5},{"receiveTs",now},{"seq",10}}};
      batch.snapshot.communication={boost::json::object{{"deviceId","env-01"},{"online",true},{"quality","GOOD"}},boost::json::object{{"deviceId","env-02"},{"online",true},{"quality","GOOD"}}};
      batch.snapshot.alarms={boost::json::object{{"id","field-a"},{"deviceId","env-01"},{"title","A alarm"},{"message","active"},{"severity","WARNING"},{"state","ACTIVE"},{"raisedAt",now-100}}};
    }else{
      batch.completeDeviceIds={"env-02"}; batch.failedDeviceIds={"env-01"}; batch.failureDetails["env-01"]="cable unplugged";
      batch.snapshot.devices={boost::json::object{{"id","env-02"},{"name","B"},{"status","ONLINE"}}};
      batch.snapshot.telemetry={boost::json::object{{"deviceId","env-02"},{"pointId","temperature"},{"value",30.0+static_cast<double>(n)},{"quality","GOOD"},{"sampleTs",now-5},{"receiveTs",now},{"seq",static_cast<std::int64_t>(n==3?1:10+n)}}};
      batch.snapshot.communication={boost::json::object{{"deviceId","env-02"},{"online",true},{"quality","GOOD"}}};
    }
    return batch;
  }
 private:
  std::mutex mutex_; int reads_{0};
};
}


int main() {
  const auto temp = tempDir();
  try {
    const auto registry = smart_factory::DeviceRegistry::load(std::string(SMART_FACTORY_SOURCE_DIR) + "/backend-cpp/config/devices.development.json");
    require(registry->find("env-01") && registry->find("env-02"), "B2 fixture requires env-01/env-02");
    smart_factory::QualityService quality(registry);
    smart_factory::AcquisitionService acquisition(registry, "b2-site", "b2-test-adapter", true);
    const auto now = nowMs();

    // Missing source timestamp must be explicit, never masquerade as device time.
    boost::json::object missingTs{{"deviceId","env-01"},{"pointId","temperature"},{"label","温度"},{"value",26.5},{"quality","GOOD"},{"receiveTs",now},{"seq",1}};
    auto normalized = quality.normalizePoint(missingTs, now, "epoch-a", "SIMULATION", "b2-test-adapter", "cfg-42", "live:b2-site");
    require(normalized.at("sampleTs").as_int64()==now, "missing source time should use explicit host ordering timestamp");
    require(normalized.at("quality").as_string()=="UNCERTAIN", "missing source time must degrade quality");
    require(normalized.at("provenance").as_object().at("sourceTimeBasis").as_string()=="HOST", "host timestamp basis must be explicit");
    require(normalized.at("configRevision").as_string()=="cfg-42", "config revision missing from normalized point");
    require(normalized.at("sourceEpoch").as_string()=="epoch-a", "source epoch missing from normalized point");

    // Wrong value type must fail safe as BAD rather than being coerced.
    boost::json::object wrongType{{"deviceId","env-01"},{"pointId","temperature"},{"value","26.5"},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",2}};
    auto bad = quality.normalizePoint(wrongType, now, "epoch-a", "SIMULATION", "b2-test-adapter", "cfg-42", "live:b2-site");
    require(bad.at("quality").as_string()=="BAD", "type mismatch must be BAD");

    // A stale sample must remain identifiable as stale, not silently become current.
    boost::json::object staleRaw{{"deviceId","env-01"},{"pointId","temperature"},{"value",26.2},{"quality","GOOD"},{"sampleTs",now-10000},{"receiveTs",now},{"seq",3}};
    auto stale = quality.normalizePoint(staleRaw, now, "epoch-a", "SIMULATION", "b2-test-adapter", "cfg-42", "live:b2-site");
    require(stale.at("quality").as_string()=="STALE", "aged sample must be STALE");

    // Previous state contains two same-named temperature points.
    smart_factory::DeviceDataSnapshot previous;
    previous.generatedAt=now-1000;
    previous.devices = {boost::json::object{{"id","env-01"},{"name","A"},{"status","ONLINE"}}, boost::json::object{{"id","env-02"},{"name","B"},{"status","ONLINE"}}};
    previous.telemetry = {
      boost::json::object{{"deviceId","env-01"},{"pointId","temperature"},{"value",25.0},{"quality","GOOD"},{"sampleTs",now-1000},{"receiveTs",now-990},{"seq",10},{"sourceEpoch","old"}},
      boost::json::object{{"deviceId","env-02"},{"pointId","temperature"},{"value",30.0},{"quality","GOOD"},{"sampleTs",now-1000},{"receiveTs",now-990},{"seq",10},{"sourceEpoch","old"}}
    };
    previous.communication = {boost::json::object{{"deviceId","env-01"},{"online",true},{"quality","GOOD"}}, boost::json::object{{"deviceId","env-02"},{"online",true},{"quality","GOOD"}}};

    smart_factory::DeviceAcquisitionBatch batch;
    batch.sourceEpoch="epoch-b";
    batch.ownedDeviceIds={"env-01","env-02"};
    batch.completeDeviceIds={"env-02"};
    batch.failedDeviceIds={"env-01"};
    batch.failureDetails["env-01"]="simulated cable unplugged";
    batch.snapshot.generatedAt=now;
    batch.snapshot.devices={boost::json::object{{"id","env-02"},{"name","B"},{"status","ONLINE"}}};
    batch.snapshot.telemetry={boost::json::object{{"deviceId","env-02"},{"pointId","temperature"},{"value",31.25},{"quality","GOOD"},{"sampleTs",now-15},{"receiveTs",now-8},{"seq",1}}};
    batch.snapshot.communication={boost::json::object{{"deviceId","env-02"},{"online",true},{"quality","GOOD"}}};

    auto mergedBatch = acquisition.normalize(std::move(batch), previous, "cfg-43", quality);
    const auto* a = findPoint(mergedBatch.snapshot.telemetry,"env-01","temperature");
    const auto* b = findPoint(mergedBatch.snapshot.telemetry,"env-02","temperature");
    require(a && b, "partial batch must retain failed A and update complete B");
    require(a->at("quality").as_string()=="STALE", "failed A must become STALE");
    require(a->at("value").as_double()==25.0, "failed A must retain last known value");
    require(b->at("value").as_double()==31.25, "complete B must update independently");
    require(b->at("sourceEpoch").as_string()=="epoch-b", "updated B must carry new source epoch");
    require(b->at("configRevision").as_string()=="cfg-43", "updated B must carry running config revision");
    const auto* aDevice = findDevice(mergedBatch.snapshot.devices,"env-01");
    const auto* aComm = findDevice(mergedBatch.snapshot.communication,"env-01","deviceId");
    require(aDevice && aDevice->at("status").as_string()=="DEGRADED", "failed A device must be DEGRADED");
    require(aComm && !aComm->at("online").as_bool(), "failed A communication must be offline");

    // Existing beta1/RC2 databases must migrate in place to sourceEpoch-aware
    // identity without deleting legacy rows.
    {
      const auto legacyDir=temp/"legacy-historian"; std::filesystem::create_directories(legacyDir);
      const auto legacyDb=(legacyDir/"historian.sqlite3").string(); sqlite3* db=nullptr;
      require(sqlite3_open(legacyDb.c_str(),&db)==SQLITE_OK,"cannot create legacy historian fixture");
      const auto legacyTs=now-1000;
      const auto legacyReceiveTs=legacyTs+1;
      const auto sql=std::string("CREATE TABLE telemetry(id INTEGER PRIMARY KEY AUTOINCREMENT,device_id TEXT NOT NULL,point_id TEXT NOT NULL,sample_ts INTEGER NOT NULL,receive_ts INTEGER NOT NULL DEFAULT 0,seq INTEGER NOT NULL DEFAULT -1,point_json TEXT NOT NULL,UNIQUE(device_id,point_id,sample_ts,seq));")
                    + "CREATE INDEX idx_telemetry_point_time ON telemetry(device_id,point_id,sample_ts);CREATE INDEX idx_telemetry_time ON telemetry(sample_ts);"
                    + "INSERT INTO telemetry(device_id,point_id,sample_ts,receive_ts,seq,point_json) VALUES('legacy-device','p'," + std::to_string(legacyTs) + "," + std::to_string(legacyReceiveTs) + ",1,'{\"deviceId\":\"legacy-device\",\"pointId\":\"p\",\"value\":1,\"sampleTs\":" + std::to_string(legacyTs) + ",\"receiveTs\":" + std::to_string(legacyReceiveTs) + ",\"seq\":1}');";
      char* err=nullptr; const int rc=sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&err); if(rc!=SQLITE_OK){std::string m=err?err:"legacy SQL failed";sqlite3_free(err);sqlite3_close(db);throw std::runtime_error(m);} sqlite3_close(db);
      smart_factory::HistorianOptions legacyOptions; legacyOptions.backupEnabled=false;
      smart_factory::HistorianStore migrated(legacyDir.string(),legacyOptions);
      require(migrated.telemetryRecordCount()==1,"legacy telemetry row lost during B2 migration");
      const auto legacyCursor=migrated.latestTelemetryCursors().at("legacy-device:p");
      require(legacyCursor.sourceEpoch.empty()&&legacyCursor.sampleTs==legacyTs,"legacy cursor migration is incorrect");
      migrated.appendTelemetry(boost::json::object{{"deviceId","legacy-device"},{"pointId","p"},{"value",2},{"sourceEpoch","new-epoch"},{"sampleTs",legacyTs},{"receiveTs",legacyReceiveTs+1},{"seq",1}});
      require(migrated.telemetryRecordCount()==2,"new epoch with repeated timestamp/seq was incorrectly deduplicated after migration");
      require(migrated.latestTelemetryCursors().at("legacy-device:p").sourceEpoch=="new-epoch","migrated cursor did not advance to new epoch");
    }

    // Historian must preserve the exact normalized identity/provenance object.
    smart_factory::HistorianOptions options; options.backupEnabled=false; options.retentionDays=30;
    smart_factory::HistorianStore historian((temp/"historian").string(), options);
    historian.appendTelemetry(*b);
    smart_factory::TelemetryHistoryQuery query; query.deviceId="env-02"; query.pointId="temperature"; query.fromTs=now-1000; query.toTs=now+1000;
    auto history=historian.queryTelemetry(query);
    require(history.at("items").as_array().size()==1, "normalized point must be queryable from historian");
    const auto& stored=history.at("items").as_array().front().as_object();
    require(stored.at("sourceEpoch").as_string()=="epoch-b", "historian lost sourceEpoch");
    require(stored.at("configRevision").as_string()=="cfg-43", "historian lost configRevision");
    require(stored.at("provenance").as_object().at("originKind").as_string()=="SIMULATION", "historian lost provenance");

    // Application-level batch integration: env-01 fails after an initial good
    // cycle while env-02 keeps updating. A's FIELD alarm must not be inferred
    // cleared from absence, and a sourceEpoch switch may restart sequence.
    {
      smart_factory::BackendConfig cfg;
      cfg.enableRealtime=false; cfg.authEnabled=false; cfg.auditEnabled=false; cfg.commandLedgerEnabled=false;
      cfg.historianEnabled=false; cfg.platformConfigEnabled=false; cfg.acquisitionPollMs=40; cfg.telemetryPublishMs=40;
      std::atomic<int> events{0};
      auto adapter=std::make_unique<BatchTestAdapter>();
      smart_factory::ApplicationService app(cfg,std::move(adapter),[&](const std::string&){++events;},nullptr,registry);
      std::this_thread::sleep_for(std::chrono::milliseconds(170));
      auto dashboard=app.dashboardSnapshot();
      const auto& live=dashboard.at("telemetry").as_array();
      const auto* liveA=findPoint(live,"env-01","temperature");
      const auto* liveB=findPoint(live,"env-02","temperature");
      require(liveA && liveA->at("quality").as_string()=="STALE", "Application must retain failed A as STALE");
      require(liveB && liveB->at("value").as_double()>31.0, "Application must continue updating complete B");
      require(liveB->at("sourceEpoch").as_string()=="batch-epoch-2", "source epoch switch must reach latest state");
      bool alarmStillActive=false;
      for(const auto& v: dashboard.at("alarms").as_array()) if(v.is_object()) {
        const auto& a=v.as_object();
        if(a.at("id").as_string()=="field-a" && a.at("state").as_string()!="CLEARED") alarmStillActive=true;
      }
      require(alarmStillActive, "failed A must not clear FIELD alarm by absence");
      require(app.runtimeStatus().acquisitionState=="DEGRADED", "partial device failure must expose DEGRADED acquisition state");
      require(events.load()>0, "application batch integration must continue realtime publication");
    }

    std::filesystem::remove_all(temp);
    std::cout << "platform_acquisition_b2 PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "platform_acquisition_b2 FAIL: " << e.what() << "\n";
    std::filesystem::remove_all(temp);
    return 1;
  }
}
