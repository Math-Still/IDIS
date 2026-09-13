#include "smart_factory/platform/replay_service.hpp"
#include "smart_factory/platform/rule_service.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace smart_factory {
namespace {
std::string s(const boost::json::object&o,const char*k){const auto*v=o.if_contains(k);return v&&v->is_string()?std::string(v->as_string()):std::string();}
std::string text(sqlite3_stmt* st,int c){const auto*p=reinterpret_cast<const char*>(sqlite3_column_text(st,c));return p?std::string(p):std::string();}
}
std::int64_t ReplayService::nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
ReplayService::ReplayService(std::string directory,HistorianStore* historian,ConfigService* config):directory_(std::move(directory)),historian_(historian),config_(config){std::filesystem::create_directories(directory_);path_=(std::filesystem::path(directory_)/"platform-replay.sqlite3").string();if(sqlite3_open_v2(path_.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("cannot open replay store");init();}
ReplayService::~ReplayService(){std::lock_guard l(mutex_);if(db_)sqlite3_close(db_);}
void ReplayService::init(){std::lock_guard l(mutex_);const char*sql="PRAGMA journal_mode=WAL;CREATE TABLE IF NOT EXISTS replay_sessions(session_id TEXT PRIMARY KEY,instance_id TEXT NOT NULL,from_ts INTEGER NOT NULL,to_ts INTEGER NOT NULL,config_revision TEXT NOT NULL,created_at INTEGER NOT NULL,payload_json TEXT NOT NULL);";char*e=nullptr;if(sqlite3_exec(db_,sql,nullptr,nullptr,&e)!=SQLITE_OK){std::string m=e?e:"replay init failed";sqlite3_free(e);throw std::runtime_error(m);}}
boost::json::object ReplayService::create(const std::string& instanceId,std::int64_t fromTs,std::int64_t toTs,std::size_t limit,const std::string& configRevision){
  if(!historian_||!config_)throw std::runtime_error("replay requires historian and config service");
  const auto runningRev=s(config_->status(),"runningRevision"); const auto rev=configRevision.empty()?runningRev:configRevision; auto snap=config_->snapshotForRevision(rev); const auto* arr=snap.if_contains("applicationInstances"); if(!arr||!arr->is_array())throw std::runtime_error("application instances unavailable");
  const boost::json::object* inst=nullptr; for(const auto&v:arr->as_array())if(v.is_object()&&s(v.as_object(),"instanceId")==instanceId){inst=&v.as_object();break;} if(!inst)throw std::runtime_error("application instance not found");
  boost::json::array streams; boost::json::array orderedSamples; const auto* bindings=inst->if_contains("bindings"); if(bindings&&bindings->is_object()) for(const auto&[role,rv]:bindings->as_object()){if(!rv.is_object())continue;const auto did=s(rv.as_object(),"deviceId"),pid=s(rv.as_object(),"pointId");TelemetryHistoryQuery q{did,pid,fromTs,toTs,limit};auto h=historian_->queryTelemetry(q);if(const auto* items=h.if_contains("items");items&&items->is_array())for(const auto&sample:items->as_array())orderedSamples.emplace_back(sample);streams.emplace_back(boost::json::object{{"role",role},{"deviceId",did},{"pointId",pid},{"history",h}});}
  std::sort(orderedSamples.begin(),orderedSamples.end(),[](const auto&a,const auto&b){auto ts=[](const boost::json::value&v){if(!v.is_object())return std::int64_t(0);const auto* x=v.as_object().if_contains("sampleTs");return x&&x->is_int64()?x->as_int64():std::int64_t(0);};return ts(a)<ts(b);});
  RuleService replayRules; replayRules.configure(snap,rev); boost::json::array ruleTransitions;
  for(const auto& sample:orderedSamples){if(!sample.is_object())continue;boost::json::array one;one.emplace_back(sample);const auto* tsv=sample.as_object().if_contains("sampleTs");const auto ts=tsv&&tsv->is_int64()?tsv->as_int64():fromTs;for(auto&tr:replayRules.evaluate(one,ts))ruleTransitions.emplace_back(std::move(tr));}
  const auto id="replay-"+std::to_string(nowMs()); boost::json::object payload{{"sessionId",id},{"instanceId",instanceId},{"contextId","replay:"+id},{"deliveryMode","REPLAY"},{"configRevision",rev},{"from",fromTs},{"to",toTs},{"createdAt",nowMs()},{"streams",streams},{"rules",replayRules.rules()},{"ruleTransitions",ruleTransitions},{"commandAllowed",false},{"writesLiveAlarmState",false}};
  std::lock_guard l(mutex_);sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db_,"INSERT INTO replay_sessions VALUES(?1,?2,?3,?4,?5,?6,?7);",-1,&st,nullptr);const auto js=boost::json::serialize(payload);sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,instanceId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(st,3,fromTs);sqlite3_bind_int64(st,4,toTs);sqlite3_bind_text(st,5,rev.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(st,6,nowMs());sqlite3_bind_text(st,7,js.c_str(),-1,SQLITE_TRANSIENT);const auto rc=sqlite3_step(st);sqlite3_finalize(st);if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));return payload;
}
boost::json::object ReplayService::get(const std::string& id)const{std::lock_guard l(mutex_);sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db_,"SELECT payload_json FROM replay_sessions WHERE session_id=?1;",-1,&st,nullptr);sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);const auto rc=sqlite3_step(st);if(rc!=SQLITE_ROW){sqlite3_finalize(st);throw std::runtime_error("replay session not found");}auto p=boost::json::parse(text(st,0));sqlite3_finalize(st);return p.as_object();}
} // namespace smart_factory
