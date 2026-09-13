#include "smart_factory/platform/rule_service.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smart_factory {

std::string RuleService::stringField(const boost::json::object& obj, const char* key, const std::string& fallback) {
  if (const auto* v=obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return fallback;
}
std::int64_t RuleService::intField(const boost::json::object& obj, const char* key, std::int64_t fallback) {
  if (const auto* v=obj.if_contains(key)) {
    if (v->is_int64()) return v->as_int64();
    if (v->is_uint64()) return static_cast<std::int64_t>(v->as_uint64());
  }
  return fallback;
}
double RuleService::numberField(const boost::json::object& obj, const char* key, double fallback) {
  if (const auto* v=obj.if_contains(key)) {
    if (v->is_double()) return v->as_double();
    if (v->is_int64()) return static_cast<double>(v->as_int64());
    if (v->is_uint64()) return static_cast<double>(v->as_uint64());
  }
  return fallback;
}
bool RuleService::boolField(const boost::json::object& obj, const char* key, bool fallback) {
  if (const auto* v=obj.if_contains(key); v && v->is_bool()) return v->as_bool();
  return fallback;
}
bool RuleService::numericValue(const boost::json::object& sample, double& out) {
  const auto* v=sample.if_contains("value"); if(!v) return false;
  if(v->is_double()){out=v->as_double();return std::isfinite(out);} if(v->is_int64()){out=static_cast<double>(v->as_int64());return true;} if(v->is_uint64()){out=static_cast<double>(v->as_uint64());return true;} return false;
}
int RuleService::qualityRank(const std::string& q) {
  if(q=="GOOD") return 3; if(q=="UNCERTAIN") return 2; if(q=="STALE") return 1; return 0;
}

void RuleService::configure(const boost::json::object& snapshot, const std::string& configRevision) {
  std::lock_guard lock(mutex_);
  if(configRevision==configRevision_) return;
  std::vector<Rule> next;
  if(const auto* rv=snapshot.if_contains("rules"); rv && rv->is_array()) {
    for(const auto& value:rv->as_array()) {
      if(!value.is_object()) continue; const auto& obj=value.as_object();
      Rule r; r.ruleId=stringField(obj,"ruleId"); r.version=stringField(obj,"version","1"); r.enabled=boolField(obj,"enabled",true);
      if(const auto* p=obj.if_contains("point"); p && p->is_object()){r.deviceId=stringField(p->as_object(),"deviceId");r.pointId=stringField(p->as_object(),"pointId");}
      r.direction=stringField(obj,"direction","HIGH"); r.triggerThreshold=numberField(obj,"triggerThreshold"); r.recoveryThreshold=numberField(obj,"recoveryThreshold",r.triggerThreshold);
      r.durationMs=std::max<std::int64_t>(0,intField(obj,"durationMs",0)); r.minimumQuality=stringField(obj,"minimumQuality","GOOD"); r.severity=stringField(obj,"severity","WARNING");
      r.title=stringField(obj,"title",r.ruleId); r.message=stringField(obj,"message",r.title);
      if(!r.ruleId.empty()&&!r.deviceId.empty()&&!r.pointId.empty()) next.push_back(std::move(r));
    }
  }
  rules_=std::move(next); states_.clear(); configRevision_=configRevision; // version/config switch intentionally resets pending timers.
}

boost::json::array RuleService::rules() const {
  std::lock_guard lock(mutex_); boost::json::array out;
  for(const auto& r:rules_) out.emplace_back(boost::json::object{{"ruleId",r.ruleId},{"version",r.version},{"enabled",r.enabled},{"point",boost::json::object{{"deviceId",r.deviceId},{"pointId",r.pointId}}},{"direction",r.direction},{"triggerThreshold",r.triggerThreshold},{"recoveryThreshold",r.recoveryThreshold},{"durationMs",r.durationMs},{"minimumQuality",r.minimumQuality},{"severity",r.severity},{"title",r.title},{"message",r.message}});
  return out;
}

boost::json::object RuleService::transitionPayload(const Rule& rule, const State&, const std::string& conditionState, std::int64_t ts, const std::string& reason, const std::string& configRevision) {
  return {{"alarmId","rule:"+rule.ruleId},{"ruleId",rule.ruleId},{"ruleVersion",rule.version},{"deviceId",rule.deviceId},{"pointId",rule.pointId},{"conditionState",conditionState},{"severity",rule.severity},{"title",rule.title},{"message",rule.message},{"timestamp",ts},{"reason",reason},{"sourceDomain","RULE"},{"configRevision",configRevision}};
}

boost::json::array RuleService::evaluate(const boost::json::array& telemetry, std::int64_t evaluationTs) {
  std::lock_guard lock(mutex_); boost::json::array transitions;
  std::unordered_map<std::string,const boost::json::object*> latest;
  for(const auto& value:telemetry){if(!value.is_object())continue;const auto& o=value.as_object();const auto d=stringField(o,"deviceId"),p=stringField(o,"pointId");if(!d.empty()&&!p.empty())latest[d+"\n"+p]=&o;}
  for(const auto& rule:rules_) {
    auto& state=states_[rule.ruleId];
    if(!rule.enabled){state={};continue;}
    const auto it=latest.find(rule.deviceId+"\n"+rule.pointId);
    if(it==latest.end()) continue;
    const auto& sample=*it->second; const auto quality=stringField(sample,"quality","BAD");
    const auto ts=intField(sample,"sampleTs",evaluationTs)>0?intField(sample,"sampleTs",evaluationTs):evaluationTs;
    state.lastEvaluationTs=ts;
    if(qualityRank(quality)<qualityRank(rule.minimumQuality)) {
      state.pendingSince=0;
      if(state.conditionState=="ACTIVE") {state.conditionState="UNKNOWN"; transitions.emplace_back(transitionPayload(rule,state,"UNKNOWN",ts,"quality below rule gate: "+quality,configRevision_));}
      continue;
    }
    double value=0; if(!numericValue(sample,value)){state.pendingSince=0;if(state.conditionState=="ACTIVE"){state.conditionState="UNKNOWN";transitions.emplace_back(transitionPayload(rule,state,"UNKNOWN",ts,"non-numeric sample",configRevision_));}continue;}
    const bool trigger=rule.direction=="LOW" ? value<rule.triggerThreshold : value>rule.triggerThreshold;
    const bool recover=rule.direction=="LOW" ? value>rule.recoveryThreshold : value<rule.recoveryThreshold;
    if(state.conditionState=="ACTIVE"||state.conditionState=="UNKNOWN") {
      if(recover){state.conditionState="NORMAL";state.pendingSince=0;transitions.emplace_back(transitionPayload(rule,state,"NORMAL",ts,"recovery threshold satisfied",configRevision_));}
      else if(state.conditionState=="UNKNOWN"){state.conditionState="ACTIVE";transitions.emplace_back(transitionPayload(rule,state,"ACTIVE",ts,"good-quality sample confirms abnormal condition persists",configRevision_));}
      continue;
    }
    if(!trigger){state.pendingSince=0;continue;}
    if(state.pendingSince==0) state.pendingSince=ts;
    if(rule.durationMs==0 || ts-state.pendingSince>=rule.durationMs){state.conditionState="ACTIVE";state.pendingSince=0;transitions.emplace_back(transitionPayload(rule,state,"ACTIVE",ts,"trigger threshold sustained",configRevision_));}
  }
  return transitions;
}

}  // namespace smart_factory
