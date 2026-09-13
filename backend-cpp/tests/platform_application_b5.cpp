#include "smart_factory/application.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/platform/query_service.hpp"
#include "smart_factory/platform/report_service.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {
std::int64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
smart_factory::SecurityContext actor(){smart_factory::SecurityContext c;c.principal={"operator-b5","B5 Operator",smart_factory::UserRole::Administrator,true};c.remoteAddress="127.0.0.1";return c;}
std::string str(const boost::json::object&o,const char*k){const auto*v=o.if_contains(k);return v&&v->is_string()?std::string(v->as_string()):std::string();}
}

int main(){
  // Counter reset segmentation must never report a negative production delta.
  boost::json::array counter{
    boost::json::object{{"sampleTs",1000},{"value",100},{"quality","GOOD"}},
    boost::json::object{{"sampleTs",2000},{"value",103},{"quality","GOOD"}},
    boost::json::object{{"sampleTs",3000},{"value",2},{"quality","GOOD"}},
    boost::json::object{{"sampleTs",4000},{"value",5},{"quality","GOOD"}}
  };
  auto segments=smart_factory::QueryService::counterSegments(counter,2500);
  if(segments.size()!=2||segments[0].as_object().at("delta").as_double()!=3.0||segments[1].as_object().at("delta").as_double()!=3.0){std::cerr<<"counter reset segmentation failed\n";return 1;}
  auto coverage=smart_factory::QueryService::coverage(counter,1000,9000,1000);
  if(coverage.at("coverage").as_double()>=1.0||coverage.at("expectedSamples").as_int64()!=9){std::cerr<<"coverage denominator ignored missing samples\n";return 2;}
  if(smart_factory::ReportService::csvCell("=2+2").rfind("'=2+2",0)!=0){std::cerr<<"CSV formula hardening missing\n";return 3;}

  const auto root=std::filesystem::temp_directory_path()/("sf-b5-"+std::to_string(nowMs()));std::filesystem::create_directories(root);
  auto registry=smart_factory::DeviceRegistry::load(std::string(SMART_FACTORY_SOURCE_DIR)+"/backend-cpp/config/devices.development.json");
  smart_factory::BackendConfig cfg;cfg.siteName="B5 Test";cfg.enableRealtime=false;cfg.authEnabled=false;cfg.auditEnabled=false;
  cfg.platformConfigEnabled=true;cfg.platformConfigDirectory=(root/"config").string();
  cfg.historianEnabled=true;cfg.historianDirectory=(root/"historian").string();cfg.historianBackupEnabled=false;
  cfg.commandLedgerEnabled=true;cfg.commandLedgerDirectory=(root/"commands").string();cfg.commandLedgerMaxRecords=200;cfg.acquisitionPollMs=50;
  smart_factory::ApplicationService app(cfg,smart_factory::makeSimulatedDeviceAdapter(registry),{},nullptr,registry);

  auto instances=app.applicationInstances(); std::set<std::string> templates;
  for(const auto&v:instances)if(v.is_object()){const auto&o=v.as_object();templates.insert(str(o,"templateId"));const auto&b=o.at("bindings").as_object();const auto t=str(o,"templateId");
    if(t=="temperature_humidity"&&(!b.if_contains("ambientTemperature")||!b.if_contains("ambientHumidity"))){std::cerr<<"temperature bindings missing\n";return 4;}
    if(t=="pir_lighting"&&!b.if_contains("occupied")){std::cerr<<"PIR binding missing\n";return 5;}
    if(t=="hazardous_gas"&&!b.if_contains("gasConcentration")){std::cerr<<"gas binding missing\n";return 6;}
    if(t=="agv_obstacle"&&!b.if_contains("obstacleDistance")){std::cerr<<"AGV binding missing\n";return 7;}
    if(t=="goods_counting"&&!b.if_contains("totalCount")){std::cerr<<"count binding missing\n";return 8;}
  }
  if(templates.size()!=5){std::cerr<<"not all five application templates bootstrapped\n";return 9;}

  // Publish a sixth/same-template instance through configuration only.
  auto snap=app.platformConfigSnapshot();auto& apps=snap.at("applicationInstances").as_array();auto clone=apps.front().as_object();clone["instanceId"]="temperature_humidity-env-01-extra";clone["displayName"]="Extra configured environment instance";apps.emplace_back(clone);
  auto draft=app.createConfigDraft(boost::json::object{{"baseRevision",app.platformConfigStatus().at("publishedRevision")},{"snapshot",snap}},actor());
  if(draft.httpStatus!=201){std::cerr<<"B5 draft failed\n";return 10;}
  const auto draftId=str(draft.body,"draftId");auto valid=app.validateConfigDraft(draftId,actor());if(valid.httpStatus!=200){std::cerr<<"B5 duplicate template instance validation failed "<<boost::json::serialize(valid.body)<<"\n";return 11;}
  auto pub=app.publishConfigDraft(draftId,boost::json::object{{"reason","B5 configurable application instance acceptance"}},actor());if(pub.httpStatus!=200||app.applicationInstances().size()!=instances.size()+1){std::cerr<<"B5 config-only instance publish failed\n";return 12;}

  const auto to=nowMs()+1000,from=to-10000; const auto replayRevision=str(app.platformConfigStatus(),"runningRevision");
  auto replay=app.createReplaySessionV2(boost::json::object{{"instanceId","temperature_humidity-env-01"},{"from",from},{"to",to},{"limit",1000},{"configRevision",replayRevision}},actor());
  if(replay.httpStatus!=201||str(replay.body,"deliveryMode")!="REPLAY"||str(replay.body,"configRevision")!=replayRevision||replay.body.at("commandAllowed").as_bool()||!replay.body.if_contains("ruleTransitions")||!replay.body.if_contains("writesLiveAlarmState")||replay.body.at("writesLiveAlarmState").as_bool()){std::cerr<<"isolated replay/versioned rule evaluation failed\n";return 13;}
  auto replayCommand=app.previewCommandV2(boost::json::object{{"deviceId","env-01"},{"action","device.selfTest"},{"contextId",str(replay.body,"contextId")}},actor());
  if(replayCommand.httpStatus!=409||str(replayCommand.body,"code")!="REPLAY_COMMAND_FORBIDDEN"){std::cerr<<"replay context allowed command\n";return 14;}

  auto report=app.createReportJobV2(boost::json::object{{"instanceId","temperature_humidity-env-01"},{"from",from},{"to",to},{"format","HTML_CSV"}},actor());
  if(report.httpStatus!=201||str(report.body,"status")!="COMPLETED"||!report.body.if_contains("csv")||!report.body.if_contains("html")){std::cerr<<"report generation failed "<<boost::json::serialize(report.body)<<"\n";return 15;}
  const auto summary=report.body.at("summary").as_object();if(str(summary,"configRevision").empty()||summary.at("points").as_array().empty()){std::cerr<<"report provenance/coverage summary missing\n";return 16;}

  auto timeline=app.timelineV2(0,INT64_MAX);
  bool sawConfig=false;for(const auto&v:timeline.at("items").as_array())if(v.is_object()&&str(v.as_object(),"timelineDomain")=="CONFIG")sawConfig=true;
  if(!sawConfig){std::cerr<<"unified timeline lacks config event\n";return 17;}

  std::filesystem::remove_all(root);std::cout<<"platform application B5 PASS\n";return 0;
}
