#include "smart_factory/application.hpp"
#include "smart_factory/historian.hpp"
#include "smart_factory/security.hpp"

#include <boost/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

smart_factory::SecurityContext actor() {
  smart_factory::SecurityContext ctx;
  ctx.principal = {"operator-beta1","Operator Beta1",smart_factory::UserRole::Operator,true};
  ctx.remoteAddress = "127.0.0.1";
  return ctx;
}

const boost::json::object* findAlarm(const boost::json::array& alarms, const std::string& id) {
  for (const auto& v : alarms) {
    if (!v.is_object()) continue;
    const auto& o=v.as_object();
    if (const auto* p=o.if_contains("id"); p && p->is_string() && p->as_string()==id) return &o;
  }
  return nullptr;
}

class AcquisitionSafetyAdapter final : public smart_factory::DeviceAdapter {
 public:
  std::string name() const override { return "beta1-acquisition-test"; }
  bool simulated() const override { return false; }
  bool start(std::string& detail) override { running_=true; detail="ready"; return true; }
  void stop() noexcept override { running_=false; }
  smart_factory::DeviceAdapterHealth health() const override {
    return {running_,running_?"READY":"OFFLINE","beta1 test adapter",lastSuccess_.load(),failures_.load()};
  }
  void setFail(bool value) { fail_.store(value); }
  void setPartialSuccess(bool value) { partialSuccess_.store(value); }
  void setAlarmActive(bool value) { alarmActive_.store(value); if(value) raisedAt_.compare_exchange_strong(zero_,nowMs()); }

  smart_factory::DeviceDataSnapshot readSnapshot() override {
    if (fail_.load()) { ++failures_; throw std::runtime_error("intentional incomplete acquisition cycle"); }
    const auto now=nowMs(); const auto seq=++seq_;
    smart_factory::DeviceDataSnapshot s; s.generatedAt=now;
    if(partialSuccess_.load()) { lastSuccess_.store(now); return s; }
    s.devices.emplace_back(boost::json::object{{"id","gas-01"},{"name","Gas sensor"},{"scenario","hazardous_gas"},{"status","ONLINE"},{"lastSeen",now}});
    s.telemetry.emplace_back(boost::json::object{{"deviceId","gas-01"},{"pointId","co"},{"label","CO"},{"value",42.0},{"unit","ppm"},{"quality","GOOD"},{"sampleTs",now},{"receiveTs",now},{"seq",seq}});
    if (alarmActive_.load()) {
      auto raised=raisedAt_.load(); if(raised==0){ raised=now; raisedAt_.store(raised); }
      s.alarms.emplace_back(boost::json::object{{"id","gas-high-01"},{"deviceId","gas-01"},{"title","Gas high"},{"message","Threshold exceeded"},{"severity","CRITICAL"},{"state","ACTIVE"},{"raisedAt",raised}});
    }
    s.communication.emplace_back(boost::json::object{{"deviceId","gas-01"},{"online",true},{"quality","GOOD"},{"lastSeen",now},{"errorCount",0}});
    lastSuccess_.store(now); return s;
  }

  smart_factory::DeviceCommandResult execute(const smart_factory::DeviceCommand&) override { return {false,"not used"}; }
  smart_factory::DeviceCommandFeedback readCommandFeedback(const smart_factory::DeviceCommand&) override {
    return {smart_factory::DeviceFeedbackState::Unsupported,"not used","null"};
  }
 private:
  std::atomic<bool> running_{false}, fail_{false}, partialSuccess_{false}, alarmActive_{true};
  std::atomic<std::int64_t> lastSuccess_{0}, raisedAt_{0};
  std::atomic<std::uint64_t> failures_{0};
  std::atomic<std::int64_t> seq_{0};
  std::int64_t zero_{0};
};

bool waitFor(std::function<bool()> fn, int timeoutMs=1500) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
  while(std::chrono::steady_clock::now()<end){ if(fn()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
  return fn();
}
}

int main() {
  const auto base=std::filesystem::temp_directory_path()/("smart-factory-beta1-"+std::to_string(nowMs()));
  const auto historianDir=(base/"historian").string();
  const auto backupDir=(base/"backup").string();
  const auto auditDir=(base/"audit").string();
  std::filesystem::create_directories(base);

  smart_factory::BackendConfig cfg;
  cfg.enableRealtime=false;
  cfg.authEnabled=false;
  cfg.auditEnabled=false;
  cfg.commandLedgerEnabled=false;
  cfg.historianEnabled=true;
  cfg.historianDirectory=historianDir;
  cfg.historianRetentionDays=30;
  cfg.historianBackupEnabled=true;
  cfg.historianBackupDirectory=backupDir;
  cfg.historianBackupIntervalHours=24;
  cfg.historianMinFreeSpaceMb=32;
  cfg.telemetryPublishMs=100;
  cfg.acquisitionStaleAfterMs=100;
  cfg.acquisitionOfflineAfterMs=400;
  cfg.acquisitionFailureAlarmThreshold=2;

  std::string occurrence;
  {
    auto adapter=std::make_unique<AcquisitionSafetyAdapter>();
    smart_factory::ApplicationService app(cfg,std::move(adapter),[](const std::string&){});
    const auto initialAlarms=app.alarms();
    const auto* initial=findAlarm(initialAlarms,"gas-high-01");
    if(!initial || !initial->if_contains("occurrenceId")) { std::cerr<<"missing occurrenceId on raised alarm\n"; return 1; }
    occurrence=std::string(initial->at("occurrenceId").as_string());
    const auto ack=app.acknowledgeAlarm("gas-high-01",boost::json::object{{"comment","checked"}},actor());
    if(ack.httpStatus!=200 || std::string(ack.body.at("state").as_string())!="ACKNOWLEDGED") { std::cerr<<"ACK failed\n"; return 2; }
  }

  // Durable current state must be restored independently of alarm event retention.
  {
    auto adapter=std::make_unique<AcquisitionSafetyAdapter>();
    auto* raw=adapter.get();
    smart_factory::ApplicationService app(cfg,std::move(adapter),[](const std::string&){});
    const auto recoveredAlarms=app.alarms();
    const auto* recovered=findAlarm(recoveredAlarms,"gas-high-01");
    if(!recovered || std::string(recovered->at("state").as_string())!="ACKNOWLEDGED" ||
       std::string(recovered->at("occurrenceId").as_string())!=occurrence) {
      std::cerr<<"durable alarm_current recovery failed\n"; return 3;
    }

    // An incomplete/failed acquisition cycle MUST NOT clear a previously active alarm.
    raw->setFail(true);
    if(!waitFor([&]{
      if(app.runtimeStatus().acquisitionConsecutiveFailures<2) return false;
      const auto alarms=app.alarms();
      const auto* health=findAlarm(alarms,"system-acquisition-health");
      return health && std::string(health->at("state").as_string())!="CLEARED";
    })) { std::cerr<<"acquisition failure/alarm not observed\n"; return 4; }
    const auto failedAlarms=app.alarms();
    const auto* duringFailure=findAlarm(failedAlarms,"gas-high-01");
    const auto* acquisitionAlarm=findAlarm(failedAlarms,"system-acquisition-health");
    if(!duringFailure || std::string(duringFailure->at("state").as_string())!="ACKNOWLEDGED") { std::cerr<<"field alarm was falsely cleared by failed snapshot\n"; return 5; }
    if(!acquisitionAlarm || std::string(acquisitionAlarm->at("state").as_string())=="CLEARED") { std::cerr<<"acquisition system alarm missing\n"; return 6; }

    // A buggy adapter that returns success with the active alarm source device
    // omitted must still be treated as incomplete by the host.
    raw->setFail(false); raw->setPartialSuccess(true);
    const auto failuresBeforePartial=app.runtimeStatus().acquisitionTotalFailures;
    if(!waitFor([&]{ return app.runtimeStatus().acquisitionTotalFailures>failuresBeforePartial; })) {
      std::cerr<<"host did not reject partial successful snapshot\n"; return 20;
    }
    const auto partialAlarms=app.alarms();
    const auto* stillActive=findAlarm(partialAlarms,"gas-high-01");
    if(!stillActive || std::string(stillActive->at("state").as_string())!="ACKNOWLEDGED") {
      std::cerr<<"partial successful snapshot falsely cleared field alarm\n"; return 21;
    }
    raw->setPartialSuccess(false); raw->setFail(true);
    const auto snapshot=app.dashboardSnapshot();
    const auto& telemetry=snapshot.at("telemetry").as_array();
    if(telemetry.empty() || !telemetry.front().as_object().if_contains("quality") ||
       std::string(telemetry.front().as_object().at("quality").as_string())=="GOOD") {
      std::cerr<<"stale telemetry quality not surfaced\n"; return 7;
    }

    raw->setFail(false);
    if(!waitFor([&]{ return app.runtimeStatus().acquisitionState=="GOOD"; })) { std::cerr<<"acquisition recovery not observed\n"; return 8; }
    raw->setAlarmActive(false);
    if(!waitFor([&]{ const auto alarms=app.alarms(); const auto* a=findAlarm(alarms,"gas-high-01"); return a && std::string(a->at("state").as_string())=="CLEARED"; })) {
      std::cerr<<"complete healthy snapshot did not clear field alarm\n"; return 9;
    }
    const auto clearedAlarms=app.alarms();
    const auto* cleared=findAlarm(clearedAlarms,"gas-high-01");
    if(std::string(cleared->at("occurrenceId").as_string())!=occurrence) { std::cerr<<"occurrenceId changed inside one incident\n"; return 10; }

    smart_factory::AlarmHistoryQuery q; q.alarmId="gas-high-01"; q.limit=100;
    const auto events=app.alarmHistory(q).at("items").as_array();
    bool raised=false,acked=false,clearedEvent=false;
    for(const auto& v:events){
      const auto& e=v.as_object();
      if(std::string(e.at("occurrenceId").as_string())!=occurrence){std::cerr<<"event occurrence mismatch\n";return 11;}
      const auto t=std::string(e.at("eventType").as_string()); raised|=t=="RAISED"; acked|=t=="ACKNOWLEDGED"; clearedEvent|=t=="CLEARED";
    }
    if(!(raised&&acked&&clearedEvent)){std::cerr<<"alarm occurrence lifecycle incomplete\n";return 12;}
  }

  smart_factory::HistorianOptions options;
  options.retentionDays=30; options.backupEnabled=true; options.backupDirectory=backupDir; options.backupIntervalHours=24; options.minFreeSpaceMb=32;
  std::string backupPath;
  std::size_t beforeTelemetry=0, beforeEvents=0;
  {
    smart_factory::HistorianStore store(historianDir,options);
    beforeTelemetry=store.telemetryRecordCount(); beforeEvents=store.alarmEventCount();
    if(beforeTelemetry==0 || beforeEvents<3 || store.alarmOccurrenceCount()==0){std::cerr<<"sqlite historian missing persisted rows\n";return 13;}
    boost::json::object duplicate{{"deviceId","dedupe-01"},{"pointId","p"},{"value",1.0},{"sampleTs",1234567890000LL},{"receiveTs",1234567890001LL},{"seq",7}};
    const auto count=store.telemetryRecordCount(); store.appendTelemetry(duplicate); store.appendTelemetry(duplicate);
    if(store.telemetryRecordCount()!=count+1){std::cerr<<"telemetry uniqueness/dedupe failed\n";return 14;}
    // Restart cursor recovery must follow the newest sample timestamp/sequence,
    // not simply the most recently inserted SQLite row.
    const auto cursorBase=nowMs()+10000;
    store.appendTelemetry(boost::json::object{{"deviceId","cursor-device"},{"pointId","cursor-point"},{"value",2},{"sampleTs",cursorBase+2000},{"receiveTs",nowMs()},{"seq",2}});
    store.appendTelemetry(boost::json::object{{"deviceId","cursor-device"},{"pointId","cursor-point"},{"value",1},{"sampleTs",cursorBase+1000},{"receiveTs",nowMs()},{"seq",1}});
    const auto cursors=store.latestTelemetryCursors();
    const auto cursorIt=cursors.find("cursor-device:cursor-point");
    if(cursorIt==cursors.end() || cursorIt->second.sampleTs!=cursorBase+2000 || cursorIt->second.seq!=2){
      std::cerr<<"latest telemetry cursor regressed on out-of-order insert\n"; return 19;
    }
    // B2 sourceEpoch is part of sample identity. A restarted source may reuse
    // the same sample timestamp and sequence without being deduplicated away.
    const auto epochCount=store.telemetryRecordCount();
    store.appendTelemetry(boost::json::object{{"deviceId","epoch-device"},{"pointId","p"},{"value",1},{"sourceEpoch","epoch-1"},{"sampleTs",cursorBase+3000},{"receiveTs",nowMs()},{"seq",1}});
    store.appendTelemetry(boost::json::object{{"deviceId","epoch-device"},{"pointId","p"},{"value",2},{"sourceEpoch","epoch-2"},{"sampleTs",cursorBase+3000},{"receiveTs",nowMs()+1},{"seq",1}});
    if(store.telemetryRecordCount()!=epochCount+2){std::cerr<<"sourceEpoch uniqueness failed\n";return 20;}
    const auto epochCursors=store.latestTelemetryCursors();
    const auto epochIt=epochCursors.find("epoch-device:p");
    if(epochIt==epochCursors.end() || epochIt->second.sourceEpoch!="epoch-2" || epochIt->second.seq!=1){std::cerr<<"sourceEpoch cursor recovery failed\n";return 21;}
    backupPath=store.backupNow();
    std::string detail; if(!smart_factory::HistorianStore::verifyDatabaseFile(backupPath,&detail)){std::cerr<<"backup integrity failed: "<<detail<<"\n";return 15;}
  }

  const auto restoreDir=(base/"restored").string();
  smart_factory::HistorianStore::restoreBackup(backupPath,restoreDir);
  {
    smart_factory::HistorianOptions restoredOptions=options; restoredOptions.backupEnabled=false;
    smart_factory::HistorianStore restored(restoreDir,restoredOptions);
    if(restored.telemetryRecordCount()<beforeTelemetry || restored.alarmEventCount()!=beforeEvents){std::cerr<<"offline restore did not preserve historian data\n";return 16;}
  }

  // Time retention deletes expired telemetry/events without rewriting the database.
  const auto retentionDir=(base/"retention").string();
  {
    smart_factory::HistorianOptions retention; retention.retentionDays=1; retention.backupEnabled=false; retention.minFreeSpaceMb=32;
    smart_factory::HistorianStore store(retentionDir,retention);
    const auto now=nowMs();
    store.appendTelemetry(boost::json::object{{"deviceId","r"},{"pointId","p"},{"value",1},{"sampleTs",now-3LL*24*60*60*1000},{"receiveTs",now},{"seq",1}});
    store.appendTelemetry(boost::json::object{{"deviceId","r"},{"pointId","p"},{"value",2},{"sampleTs",now},{"receiveTs",now},{"seq",2}});
    store.pruneExpired();
    smart_factory::TelemetryHistoryQuery q; q.deviceId="r"; q.pointId="p"; q.limit=10;
    const auto items=store.queryTelemetry(q).at("items").as_array();
    if(items.size()!=1 || items.front().as_object().at("seq").as_int64()!=2){std::cerr<<"time retention failed\n";return 17;}
  }

  // Audit storage uses SQLite/WAL as well; row pruning is SQL DELETE rather than
  // whole-file JSONL rewrite.
  {
    smart_factory::AuditStore audit(auditDir,1000);
    audit.append(boost::json::object{{"eventId","a1"},{"timestamp",nowMs()},{"actorId","u"},{"action","TEST"},{"resourceType","SYSTEM"},{"resourceId","r"},{"outcome","PASS"},{"detail","beta1"}});
    smart_factory::AuditQuery q; q.actorId="u"; q.limit=10;
    if(audit.query(q).at("items").as_array().size()!=1 || !std::filesystem::exists(std::filesystem::path(auditDir)/"audit.sqlite3")) {
      std::cerr<<"sqlite audit persistence failed\n"; return 18;
    }
  }

  std::filesystem::remove_all(base);
  std::cout<<"alarm/acquisition/historian beta1 PASS\n";
  return 0;
}
