#include "smart_factory/platform/rule_service.hpp"
#include "smart_factory/platform/alarm_service.hpp"
#include "smart_factory/platform/incident_service.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::filesystem::path tempDir() {
  auto p=std::filesystem::temp_directory_path()/ ("smart-factory-b3-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(p); return p;
}
boost::json::object snapshot(const std::string& version="1", bool enabled=true) {
  return {{"rules",boost::json::array{boost::json::object{
    {"ruleId","temp-high-env-01"},{"version",version},{"enabled",enabled},
    {"point",boost::json::object{{"deviceId","env-01"},{"pointId","temperature"}}},
    {"direction","HIGH"},{"triggerThreshold",30.0},{"recoveryThreshold",28.0},{"durationMs",10000},
    {"minimumQuality","GOOD"},{"severity","WARNING"},{"title","温度持续偏高"},{"message","B3 test"}
  }}}};
}
boost::json::array sample(double value, std::int64_t ts, const char* quality="GOOD") {
  return {boost::json::object{{"deviceId","env-01"},{"pointId","temperature"},{"value",value},{"quality",quality},{"sampleTs",ts}}};
}
std::string str(const boost::json::object& o,const char* key){const auto*v=o.if_contains(key);return v&&v->is_string()?std::string(v->as_string()):std::string();}
}

int main() {
  const auto root=tempDir();
  try {
    smart_factory::RuleService rules;
    rules.configure(snapshot(),"cfg-1");
    const std::int64_t t0=1'700'000'000'000LL;
    require(rules.evaluate(sample(31.0,t0),t0).empty(),"first over-threshold sample must start duration only");
    require(rules.evaluate(sample(31.0,t0+9'999),t0+9'999).empty(),"duration must not trigger early");
    auto active=rules.evaluate(sample(31.0,t0+10'000),t0+10'000);
    require(active.size()==1 && str(active.front().as_object(),"conditionState")=="ACTIVE","sustained threshold must activate once");

    smart_factory::AlarmService alarms(root.string());
    auto first=alarms.observe(active.front().as_object());
    const auto firstId=str(first,"occurrenceId");
    require(!firstId.empty()&&str(first,"conditionState")=="ACTIVE"&&str(first,"ackState")=="UNACKNOWLEDGED","new occurrence semantics invalid");

    auto unknownTransition=rules.evaluate(sample(31.0,t0+11'000,"BAD"),t0+11'000);
    require(unknownTransition.size()==1&&str(unknownTransition.front().as_object(),"conditionState")=="UNKNOWN","BAD quality must not clear active rule");
    auto unknown=alarms.observe(unknownTransition.front().as_object());
    require(str(unknown,"occurrenceId")==firstId&&str(unknown,"conditionState")=="UNKNOWN","UNKNOWN must preserve occurrence");

    auto activeAgain=rules.evaluate(sample(29.0,t0+12'000),t0+12'000);
    require(activeAgain.size()==1&&str(activeAgain.front().as_object(),"conditionState")=="ACTIVE","hysteresis band after UNKNOWN must preserve active condition");
    alarms.observe(activeAgain.front().as_object());

    auto normalTransition=rules.evaluate(sample(27.0,t0+13'000),t0+13'000);
    require(normalTransition.size()==1&&str(normalTransition.front().as_object(),"conditionState")=="NORMAL","recovery threshold must normalize");
    auto normalized=alarms.observe(normalTransition.front().as_object());
    require(str(normalized,"conditionState")=="NORMAL"&&str(normalized,"ackState")=="UNACKNOWLEDGED","recovery and acknowledgement must be independent");

    smart_factory::SecurityContext operatorCtx; operatorCtx.principal={"operator","Operator",smart_factory::UserRole::Operator,true};
    auto acked=alarms.acknowledge(firstId,operatorCtx,"恢复后确认");
    require(str(acked,"conditionState")=="NORMAL"&&str(acked,"ackState")=="ACKNOWLEDGED","restored occurrence must still be acknowledgeable");
    auto ackReplay=alarms.acknowledge(firstId,operatorCtx,"duplicate");
    require(str(ackReplay,"ackState")=="ACKNOWLEDGED","duplicate ACK must be idempotent");

    // New occurrence after return-to-normal; old occurrence ACK cannot affect it.
    require(rules.evaluate(sample(31.0,t0+20'000),t0+20'000).empty(),"new occurrence duration starts fresh");
    auto secondActive=rules.evaluate(sample(31.0,t0+30'000),t0+30'000);
    require(secondActive.size()==1,"second occurrence should trigger after duration");
    auto second=alarms.observe(secondActive.front().as_object());
    const auto secondId=str(second,"occurrenceId");
    require(secondId!=firstId&&str(second,"ackState")=="UNACKNOWLEDGED","new occurrence must have new ID and ACK state");
    alarms.acknowledge(firstId,operatorCtx,"late old ack");
    auto secondAfterOldAck=alarms.occurrence(secondId);
    require(secondAfterOldAck&&str(*secondAfterOldAck,"ackState")=="UNACKNOWLEDGED","late old ACK must not acknowledge new occurrence");

    // Config/rule version switch resets pending timer; restart also resets pending timer.
    rules.configure(snapshot("2"),"cfg-2");
    require(rules.evaluate(sample(31.0,t0+40'000),t0+40'000).empty(),"rule version switch must reset duration timer");
    rules.configure(snapshot("3",false),"cfg-3");
    require(rules.evaluate(sample(40.0,t0+60'000),t0+60'000).empty(),"disabled rule must not fire");
    smart_factory::RuleService restarted;
    restarted.configure(snapshot("4"),"cfg-4");
    require(restarted.evaluate(sample(40.0,t0+100'000),t0+100'000).empty(),"restart must not infer duration through downtime");

    // Incident dedup and lifecycle using the already normalized+acknowledged first occurrence.
    smart_factory::IncidentService incidents(root.string());
    boost::json::object create{{"title","温度异常处置"},{"occurrenceIds",boost::json::array{firstId}}};
    auto incident=incidents.create(create,operatorCtx); const auto incidentId=str(incident,"incidentId");
    auto duplicate=incidents.create(create,operatorCtx); require(str(duplicate,"incidentId")==incidentId,"same occurrence must deduplicate open incident");
    auto started=incidents.transition(incidentId,{{"action","START"},{"expectedRevision",1},{"comment","现场核查"}},operatorCtx); require(str(started,"status")=="IN_PROGRESS","incident START failed");
    auto resolved=incidents.transition(incidentId,{{"action","RESOLVE"},{"expectedRevision",2},{"comment","条件恢复"}},operatorCtx); require(str(resolved,"status")=="RESOLVED","incident RESOLVE failed");
    smart_factory::SecurityContext engineer; engineer.principal={"engineer","Engineer",smart_factory::UserRole::Engineer,true};
    auto closed=incidents.transition(incidentId,{{"action","CLOSE"},{"expectedRevision",3},{"comment","复核关闭"}},engineer); require(str(closed,"status")=="CLOSED","incident CLOSE failed");

    // Reopen stores survive process/service recreation; alarm occurrence also persists.
    {
      smart_factory::AlarmService recovered(root.string());
      auto occurrence=recovered.occurrence(firstId); require(occurrence&&str(*occurrence,"ackState")=="ACKNOWLEDGED"&&str(*occurrence,"conditionState")=="NORMAL","alarm v2 occurrence must survive restart");
    }
    {
      smart_factory::IncidentService recovered(root.string());
      auto saved=recovered.get(incidentId); require(saved&&str(*saved,"status")=="CLOSED","incident must survive restart");
    }

    std::filesystem::remove_all(root);
    std::cout << "platform_rule_alarm_incident_b3 PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "platform_rule_alarm_incident_b3 FAIL: " << e.what() << "\n";
    std::filesystem::remove_all(root);
    return 1;
  }
}
