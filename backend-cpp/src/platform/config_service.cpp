#include "smart_factory/platform/config_service.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace smart_factory {
namespace {

std::string jsonText(sqlite3_stmt* stmt, int column) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
  return text ? std::string(text) : std::string();
}

std::string stringField(const boost::json::object& obj, const char* key) {
  const auto* value = obj.if_contains(key);
  return value && value->is_string() ? std::string(value->as_string()) : std::string();
}

std::string areaIdFor(const std::string& location) {
  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : location) { hash ^= c; hash *= 1099511628211ull; }
  std::ostringstream out; out << "area-" << std::hex << (hash & 0xffffffffull);
  return out.str();
}

boost::json::object pointRef(const std::string& deviceId, const std::string& pointId) {
  return {{"deviceId",deviceId},{"pointId",pointId}};
}

bool pointExists(const boost::json::array& devices, const std::string& deviceId, const std::string& pointId,
                 std::string* valueType = nullptr, std::string* unit = nullptr) {
  for (const auto& dv : devices) {
    if (!dv.is_object()) continue;
    const auto& d = dv.as_object();
    if (stringField(d,"id") != deviceId) continue;
    const auto* points = d.if_contains("points");
    if (!points || !points->is_array()) return false;
    for (const auto& pv : points->as_array()) {
      if (!pv.is_object()) continue;
      const auto& p = pv.as_object();
      if (stringField(p,"pointId") != pointId) continue;
      if (valueType) *valueType = stringField(p,"valueType");
      if (unit) *unit = stringField(p,"unit");
      return true;
    }
  }
  return false;
}

bool sameJson(const boost::json::value& a, const boost::json::value& b) {
  return boost::json::serialize(a) == boost::json::serialize(b);
}

}  // namespace

std::int64_t ConfigService::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ConfigService::digest(const boost::json::value& value) {
  const auto text = boost::json::serialize(value);
  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
  std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

ConfigService::ConfigService(std::string directory, std::string siteName, std::shared_ptr<const DeviceRegistry> registry)
    : directory_(std::move(directory)), siteName_(std::move(siteName)), registry_(std::move(registry)) {
  if (!registry_) throw std::runtime_error("ConfigService requires DeviceRegistry");
  std::filesystem::create_directories(directory_);
  databasePath_ = (std::filesystem::path(directory_) / "platform-config.sqlite3").string();
  if (sqlite3_open_v2(databasePath_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown sqlite open error";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("cannot open platform config store: " + message);
  }
  std::lock_guard lock(mutex_);
  initializeLocked();
}

ConfigService::~ConfigService() {
  std::lock_guard lock(mutex_);
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

void ConfigService::initializeLocked() {
  auto exec = [this](const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
      std::string message = error ? error : sqlite3_errmsg(db_); sqlite3_free(error);
      throw std::runtime_error("config sqlite exec failed: " + message);
    }
  };
  exec("PRAGMA journal_mode=WAL;");
  exec("PRAGMA synchronous=FULL;");
  exec("PRAGMA busy_timeout=3000;");
  exec(R"SQL(
    CREATE TABLE IF NOT EXISTS config_revisions (
      revision TEXT PRIMARY KEY,
      parent_revision TEXT,
      snapshot_json TEXT NOT NULL,
      content_digest TEXT NOT NULL,
      schema_version TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      published_at INTEGER NOT NULL,
      published_by TEXT NOT NULL,
      reason TEXT NOT NULL,
      requires_restart INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS config_drafts (
      draft_id TEXT PRIMARY KEY,
      base_revision TEXT NOT NULL,
      snapshot_json TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      created_by TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS config_state (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS config_outbox (
      event_id INTEGER PRIMARY KEY AUTOINCREMENT,
      event_type TEXT NOT NULL,
      object_id TEXT NOT NULL,
      object_revision TEXT NOT NULL,
      payload_json TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      published INTEGER NOT NULL DEFAULT 0
    );
  )SQL");

  if (stateLocked("published_revision").empty()) {
    const auto snapshot = makeInitialSnapshot();
    const std::string revision = "cfg-000001";
    const auto text = boost::json::serialize(snapshot);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO config_revisions(revision,parent_revision,snapshot_json,content_digest,schema_version,created_at,published_at,published_by,reason,requires_restart) VALUES(?1,'',?2,?3,'smart-factory-platform-config/v1',?4,?4,'bootstrap','initial DeviceRegistry snapshot',0);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
    sqlite3_bind_text(stmt,1,revision.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,text.c_str(),-1,SQLITE_TRANSIENT);
    const auto hash = digest(snapshot); sqlite3_bind_text(stmt,3,hash.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,4,nowMs());
    const int rc=sqlite3_step(stmt); sqlite3_finalize(stmt);
    if(rc!=SQLITE_DONE) throw std::runtime_error("cannot create initial config revision: "+std::string(sqlite3_errmsg(db_)));
    setStateLocked("published_revision", revision);
    setStateLocked("running_revision", revision);
    appendOutboxLocked("CONFIG_BOOTSTRAPPED", revision, revision, {{"revision",revision}});
  }
}

boost::json::object ConfigService::makeInitialSnapshot() const {
  boost::json::object root;
  root["schemaVersion"] = "smart-factory-platform-config/v1";
  root["site"] = boost::json::object{{"id", registry_->site().empty()?"site-01":registry_->site()}, {"name",siteName_}};
  auto registryJson = registry_->toJson(false);
  root["devices"] = registryJson.at("devices");

  boost::json::array assets;
  const std::string siteId = registry_->site().empty()?"site-01":registry_->site();
  assets.emplace_back(boost::json::object{{"id",siteId},{"type","SITE"},{"name",siteName_}});
  std::set<std::string> areaIds;
  std::unordered_map<std::string,std::string> deviceArea;
  for (const auto& d : registry_->devices()) {
    const auto areaId = areaIdFor(d.location);
    if (areaIds.insert(areaId).second) assets.emplace_back(boost::json::object{{"id",areaId},{"type","AREA"},{"parentId",siteId},{"name",d.location}});
    assets.emplace_back(boost::json::object{{"id","device:"+d.id},{"type","DEVICE"},{"parentId",areaId},{"deviceId",d.id},{"name",d.name}});
    deviceArea[d.id] = areaId;
  }
  root["assets"] = std::move(assets);

  // Versioned threshold rules loaded with the initial site configuration.
  boost::json::array rules;
  for (const auto& d : registry_->devices()) {
    if (d.scenario != "temperature_humidity" || !registry_->findPoint(d.id,"temperature")) continue;
    const auto ruleId = "temperature-high-" + d.id;
    rules.emplace_back(boost::json::object{
      {"ruleId",ruleId},{"version","1"},{"enabled",true},
      {"point",pointRef(d.id,"temperature")},{"direction","HIGH"},
      {"triggerThreshold",30.0},{"recoveryThreshold",28.0},{"durationMs",10000},
      {"minimumQuality","GOOD"},{"severity","WARNING"},
      {"title",d.name + " 温度持续偏高"},{"message","温度持续高于设定阈值，请检查现场环境与通风状态"}
    });
  }
  root["rules"] = rules;

  boost::json::array instances;
  for (const auto& d : registry_->devices()) {
    boost::json::object bindings;
    if (d.scenario == "temperature_humidity") {
      if (registry_->findPoint(d.id,"temperature")) bindings["ambientTemperature"] = pointRef(d.id,"temperature");
      if (registry_->findPoint(d.id,"humidity")) bindings["ambientHumidity"] = pointRef(d.id,"humidity");
    } else if (d.scenario == "pir_lighting") {
      if (registry_->findPoint(d.id,"occupied")) bindings["occupied"] = pointRef(d.id,"occupied");
      if (registry_->findPoint(d.id,"lightState")) bindings["lightState"] = pointRef(d.id,"lightState");
      if (registry_->findPoint(d.id,"lightMode")) bindings["lightMode"] = pointRef(d.id,"lightMode");
    } else if (d.scenario == "hazardous_gas") {
      if (registry_->findPoint(d.id,"level")) bindings["gasConcentration"] = pointRef(d.id,"level");
      if (registry_->findPoint(d.id,"fanRunning")) bindings["fanState"] = pointRef(d.id,"fanRunning");
    } else if (d.scenario == "agv_obstacle") {
      if (registry_->findPoint(d.id,"distance")) bindings["obstacleDistance"] = pointRef(d.id,"distance");
      if (registry_->findPoint(d.id,"motionState")) bindings["motionState"] = pointRef(d.id,"motionState");
      if (registry_->findPoint(d.id,"speed")) bindings["speed"] = pointRef(d.id,"speed");
      if (registry_->findPoint(d.id,"position")) bindings["position"] = pointRef(d.id,"position");
      if (registry_->findPoint(d.id,"task")) bindings["task"] = pointRef(d.id,"task");
    } else if (d.scenario == "goods_counting") {
      if (registry_->findPoint(d.id,"totalCount")) bindings["totalCount"] = pointRef(d.id,"totalCount");
      if (registry_->findPoint(d.id,"rate")) bindings["countRate"] = pointRef(d.id,"rate");
      if (registry_->findPoint(d.id,"target")) bindings["target"] = pointRef(d.id,"target");
    }
    boost::json::array allowedActions;
    for (const auto& [actionId, command] : d.commandDefinitions)
      allowedActions.emplace_back(boost::json::object{{"deviceId",d.id},{"actionId",actionId}});
    boost::json::array instanceRuleIds;
    if (d.scenario == "temperature_humidity" && registry_->findPoint(d.id,"temperature"))
      instanceRuleIds.emplace_back("temperature-high-" + d.id);
    instances.emplace_back(boost::json::object{
      {"instanceId",d.scenario+"-"+d.id},{"templateId",d.scenario},{"templateVersion","1"},
      {"assetId",deviceArea[d.id]},{"displayName",d.name},{"bindings",std::move(bindings)},
      {"ruleIds",std::move(instanceRuleIds)},{"allowedActions",std::move(allowedActions)}
    });
  }
  root["applicationInstances"] = std::move(instances);
  return root;
}

std::string ConfigService::stateLocked(const char* key) const {
  sqlite3_stmt* stmt=nullptr;
  if(sqlite3_prepare_v2(db_,"SELECT value FROM config_state WHERE key=?1;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_text(stmt,1,key,-1,SQLITE_TRANSIENT);
  const int rc=sqlite3_step(stmt); std::string result=rc==SQLITE_ROW?jsonText(stmt,0):std::string(); sqlite3_finalize(stmt); return result;
}

void ConfigService::setStateLocked(const char* key, const std::string& value) {
  sqlite3_stmt* stmt=nullptr;
  if(sqlite3_prepare_v2(db_,"INSERT INTO config_state(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_text(stmt,1,key,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,value.c_str(),-1,SQLITE_TRANSIENT);
  const int rc=sqlite3_step(stmt); sqlite3_finalize(stmt); if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
}

boost::json::object ConfigService::revisionSnapshotLocked(const std::string& revision) const {
  sqlite3_stmt* stmt=nullptr;
  if(sqlite3_prepare_v2(db_,"SELECT snapshot_json FROM config_revisions WHERE revision=?1;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_text(stmt,1,revision.c_str(),-1,SQLITE_TRANSIENT); const int rc=sqlite3_step(stmt);
  if(rc!=SQLITE_ROW){sqlite3_finalize(stmt);throw std::runtime_error("config revision not found: "+revision);} const auto text=jsonText(stmt,0); sqlite3_finalize(stmt);
  auto parsed=boost::json::parse(text); if(!parsed.is_object()) throw std::runtime_error("stored config revision is invalid"); return parsed.as_object();
}

boost::json::object ConfigService::draftSnapshotLocked(const std::string& draftId, std::string* baseRevision) const {
  sqlite3_stmt* stmt=nullptr;
  if(sqlite3_prepare_v2(db_,"SELECT base_revision,snapshot_json FROM config_drafts WHERE draft_id=?1;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_text(stmt,1,draftId.c_str(),-1,SQLITE_TRANSIENT); const int rc=sqlite3_step(stmt);
  if(rc!=SQLITE_ROW){sqlite3_finalize(stmt);throw std::runtime_error("config draft not found: "+draftId);} if(baseRevision)*baseRevision=jsonText(stmt,0); const auto text=jsonText(stmt,1); sqlite3_finalize(stmt);
  auto parsed=boost::json::parse(text); if(!parsed.is_object()) throw std::runtime_error("stored config draft is invalid"); return parsed.as_object();
}

std::string ConfigService::nextRevisionLocked() const {
  sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db_,"SELECT COUNT(*) FROM config_revisions;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  const int rc=sqlite3_step(stmt); const auto n=rc==SQLITE_ROW?sqlite3_column_int64(stmt,0):0; sqlite3_finalize(stmt);
  std::ostringstream out; out<<"cfg-"<<std::setfill('0')<<std::setw(6)<<(n+1); return out.str();
}
std::string ConfigService::nextDraftIdLocked() const {
  sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db_,"SELECT COUNT(*) FROM config_drafts;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  const int rc=sqlite3_step(stmt); const auto n=rc==SQLITE_ROW?sqlite3_column_int64(stmt,0):0; sqlite3_finalize(stmt);
  std::ostringstream out; out<<"draft-"<<nowMs()<<"-"<<(n+1); return out.str();
}

ConfigService::ValidationResult ConfigService::validateSnapshotLocked(const boost::json::object& snapshot, const boost::json::object* runningSnapshot) const {
  ValidationResult result;
  const auto addError=[&](const std::string& code,const std::string& message,const std::string& object={}){boost::json::object e{{"code",code},{"message",message}};if(!object.empty())e["objectId"]=object;result.errors.emplace_back(std::move(e));};
  const auto* devicesValue=snapshot.if_contains("devices"); const auto* assetsValue=snapshot.if_contains("assets"); const auto* instancesValue=snapshot.if_contains("applicationInstances");
  if(!devicesValue||!devicesValue->is_array()) addError("DEVICES_REQUIRED","devices[] is required");
  if(!assetsValue||!assetsValue->is_array()) addError("ASSETS_REQUIRED","assets[] is required");
  if(!instancesValue||!instancesValue->is_array()) addError("INSTANCES_REQUIRED","applicationInstances[] is required");
  if(!result.errors.empty()) return result;
  const auto& devices=devicesValue->as_array(); const auto& assets=assetsValue->as_array(); const auto& instances=instancesValue->as_array();
  std::set<std::string> deviceIds, pointKeys, assetIds, instanceIds;
  for(const auto& value:devices){if(!value.is_object()){addError("DEVICE_INVALID","device must be object");continue;}const auto& d=value.as_object();const auto id=stringField(d,"id");if(id.empty()){addError("DEVICE_ID_REQUIRED","device id required");continue;}if(!deviceIds.insert(id).second)addError("DEVICE_DUPLICATE","duplicate device id",id);const auto* points=d.if_contains("points");if(!points||!points->is_array()){addError("POINTS_REQUIRED","device points[] required",id);continue;}for(const auto& pv:points->as_array()){if(!pv.is_object())continue;const auto pid=stringField(pv.as_object(),"pointId");if(pid.empty())addError("POINT_ID_REQUIRED","pointId required",id);else if(!pointKeys.insert(id+":"+pid).second)addError("POINT_DUPLICATE","duplicate pointId",id+":"+pid);}}
  std::unordered_map<std::string,std::string> assetParent;
  for(const auto& value:assets){if(!value.is_object())continue;const auto& a=value.as_object();const auto id=stringField(a,"id");if(id.empty()){addError("ASSET_ID_REQUIRED","asset id required");continue;}if(!assetIds.insert(id).second)addError("ASSET_DUPLICATE","duplicate asset id",id);assetParent[id]=stringField(a,"parentId");}
  for(const auto& [id,parent]:assetParent){std::set<std::string> seen;std::string cur=id;while(!cur.empty()){if(!seen.insert(cur).second){addError("ASSET_CYCLE","asset hierarchy contains a cycle",id);break;}const auto it=assetParent.find(cur);if(it==assetParent.end())break;cur=it->second;if(!cur.empty()&&!assetIds.contains(cur)){addError("ASSET_PARENT_MISSING","asset parent does not exist",id);break;}}}
  for(const auto& value:instances){if(!value.is_object()){addError("INSTANCE_INVALID","application instance must be object");continue;}const auto& i=value.as_object();const auto id=stringField(i,"instanceId"), templateId=stringField(i,"templateId"), assetId=stringField(i,"assetId");if(id.empty()){addError("INSTANCE_ID_REQUIRED","instanceId required");continue;}if(!instanceIds.insert(id).second)addError("INSTANCE_DUPLICATE","duplicate application instance",id);if(!assetId.empty()&&!assetIds.contains(assetId))addError("INSTANCE_ASSET_MISSING","instance references missing asset",id);const auto* bindingsValue=i.if_contains("bindings");if(!bindingsValue||!bindingsValue->is_object()){addError("BINDINGS_REQUIRED","instance bindings object required",id);continue;}const auto& bindings=bindingsValue->as_object();
    auto validateBinding=[&](const char* role,bool required,const char* expectedType,const std::set<std::string>& acceptedUnits){const auto* ref=bindings.if_contains(role);if(!ref){if(required)addError("BINDING_REQUIRED",std::string("required binding missing: ")+role,id);return;}if(!ref->is_object()){addError("BINDING_INVALID",std::string("binding must be PointRef: ")+role,id);return;}const auto did=stringField(ref->as_object(),"deviceId"),pid=stringField(ref->as_object(),"pointId");std::string valueType,unit;if(!pointExists(devices,did,pid,&valueType,&unit)){addError("BINDING_POINT_MISSING",std::string("binding references missing point: ")+role,id);return;}if(expectedType&&*expectedType&&valueType!=expectedType)addError("BINDING_TYPE_MISMATCH",std::string(role)+" requires "+expectedType+", got "+valueType,id);if(!acceptedUnits.empty()&&!acceptedUnits.contains(unit))addError("BINDING_UNIT_MISMATCH",std::string(role)+" has incompatible unit: "+unit,id);};
    std::set<std::string> validatedRoles;
    auto required=[&](const char* role,const char* type,const std::set<std::string>& units={}){validateBinding(role,true,type,units);validatedRoles.insert(role);};
    auto optional=[&](const char* role,const char* type,const std::set<std::string>& units={}){validateBinding(role,false,type,units);validatedRoles.insert(role);};
    if(templateId=="temperature_humidity"){required("ambientTemperature","number",{"℃","°C","C"});required("ambientHumidity","number",{"%RH","%"});optional("fanState","boolean");}
    else if(templateId=="pir_lighting"){required("occupied","boolean");optional("lightState","boolean");optional("lightMode","");}
    else if(templateId=="hazardous_gas"){required("gasConcentration","number",{"ppm","%LEL","mg/m3","mg/m³"});}
    else if(templateId=="agv_obstacle"){required("obstacleDistance","number",{"cm","m","mm"});optional("motionState","");optional("speed","number",{"m/s","km/h"});optional("position","");optional("task","");}
    else if(templateId=="goods_counting"){required("totalCount","integer",{"件","pcs","count"});optional("countRate","number");optional("target","number");}
    else addError("TEMPLATE_UNSUPPORTED","unsupported application template: "+templateId,id);
    for(const auto& [role,ref]:bindings){if(validatedRoles.contains(std::string(role)))continue;if(!ref.is_object()){addError("BINDING_INVALID","binding must be PointRef: "+std::string(role),id);continue;}const auto did=stringField(ref.as_object(),"deviceId"),pid=stringField(ref.as_object(),"pointId");if(!pointExists(devices,did,pid))addError("BINDING_POINT_MISSING","binding references missing point: "+did+":"+pid,id);}
  }
  std::set<std::string> ruleIds;
  if(const auto* rulesValue=snapshot.if_contains("rules"); rulesValue) {
    if(!rulesValue->is_array()) addError("RULES_INVALID","rules must be an array");
    else for(const auto& value:rulesValue->as_array()) {
      if(!value.is_object()){addError("RULE_INVALID","rule must be object");continue;}
      const auto& r=value.as_object(); const auto rid=stringField(r,"ruleId");
      if(rid.empty()){addError("RULE_ID_REQUIRED","ruleId required");continue;}
      if(!ruleIds.insert(rid).second)addError("RULE_DUPLICATE","duplicate ruleId",rid);
      const auto* pv=r.if_contains("point"); if(!pv||!pv->is_object()){addError("RULE_POINT_REQUIRED","rule point PointRef required",rid);continue;}
      const auto did=stringField(pv->as_object(),"deviceId"), pid=stringField(pv->as_object(),"pointId");
      if(!pointExists(devices,did,pid))addError("RULE_POINT_MISSING","rule references missing point",rid);
      const auto direction=stringField(r,"direction"); if(direction!="HIGH"&&direction!="LOW")addError("RULE_DIRECTION_INVALID","direction must be HIGH or LOW",rid);
      const auto* tv=r.if_contains("triggerThreshold"), *rvv=r.if_contains("recoveryThreshold");
      if(!tv||!(tv->is_double()||tv->is_int64()||tv->is_uint64())||!rvv||!(rvv->is_double()||rvv->is_int64()||rvv->is_uint64())) addError("RULE_THRESHOLD_REQUIRED","numeric trigger/recovery thresholds required",rid);
      else {
        const auto num=[](const boost::json::value& v){return v.is_double()?v.as_double():v.is_int64()?static_cast<double>(v.as_int64()):static_cast<double>(v.as_uint64());};
        const double trigger=num(*tv), recovery=num(*rvv);
        if(direction=="HIGH" && recovery>=trigger)addError("RULE_HYSTERESIS_INVALID","HIGH recovery threshold must be lower than trigger",rid);
        if(direction=="LOW" && recovery<=trigger)addError("RULE_HYSTERESIS_INVALID","LOW recovery threshold must be higher than trigger",rid);
      }
    }
  }
  for(const auto& value:instances) if(value.is_object()) {
    const auto& inst=value.as_object(); const auto iid=stringField(inst,"instanceId");
    if(const auto* rv=inst.if_contains("ruleIds"); rv&&rv->is_array()) for(const auto& ridv:rv->as_array())
      if(ridv.is_string()&&!ruleIds.contains(std::string(ridv.as_string()))) addError("INSTANCE_RULE_MISSING","instance references missing rule",iid);
  }
  if(runningSnapshot){const auto* runningDevices=runningSnapshot->if_contains("devices");if(!runningDevices||!runningDevices->is_array()||!sameJson(*devicesValue,*runningDevices)){result.requiresRestart=true;result.affectedObjects.emplace_back("devices");}}
  return result;
}

boost::json::object ConfigService::status() const {
  std::lock_guard lock(mutex_); const auto published=stateLocked("published_revision"), running=stateLocked("running_revision");
  return {{"schemaVersion","smart-factory-platform-config/v1"},{"publishedRevision",published},{"runningRevision",running},{"state",published==running?"RUNNING":"PUBLISHED_REQUIRES_RESTART"},{"databasePath",databasePath_}};
}
boost::json::object ConfigService::activeSnapshot() const { std::lock_guard lock(mutex_); return revisionSnapshotLocked(stateLocked("running_revision")); }
boost::json::object ConfigService::publishedSnapshot() const { std::lock_guard lock(mutex_); return revisionSnapshotLocked(stateLocked("published_revision")); }
boost::json::object ConfigService::snapshotForRevision(const std::string& revision) const { std::lock_guard lock(mutex_); return revisionSnapshotLocked(revision); }

boost::json::array ConfigService::events(std::int64_t fromTs, std::int64_t toTs) const {
  std::lock_guard lock(mutex_); boost::json::array out; sqlite3_stmt* stmt=nullptr;
  if(sqlite3_prepare_v2(db_,"SELECT event_id,event_type,object_id,object_revision,payload_json,created_at FROM config_outbox WHERE created_at>=?1 AND created_at<=?2 ORDER BY created_at ASC,event_id ASC;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_int64(stmt,1,fromTs); sqlite3_bind_int64(stmt,2,toTs);
  while(true){const int rc=sqlite3_step(stmt); if(rc==SQLITE_DONE) break; if(rc!=SQLITE_ROW){sqlite3_finalize(stmt);throw std::runtime_error(sqlite3_errmsg(db_));}
    auto payload=boost::json::parse(jsonText(stmt,4));
    out.emplace_back(boost::json::object{{"eventId","config-"+std::to_string(sqlite3_column_int64(stmt,0))},{"eventType",jsonText(stmt,1)},{"objectId",jsonText(stmt,2)},{"objectRevision",jsonText(stmt,3)},{"payload",payload},{"timestamp",sqlite3_column_int64(stmt,5)}});
  }
  sqlite3_finalize(stmt); return out;
}
boost::json::array ConfigService::assets() const { auto snapshot=activeSnapshot(); return snapshot.at("assets").as_array(); }
boost::json::array ConfigService::applicationInstances() const { auto snapshot=activeSnapshot(); return snapshot.at("applicationInstances").as_array(); }

boost::json::object ConfigService::createDraft(const std::string& baseRevisionArg, const std::optional<boost::json::object>& snapshotArg, const std::string& actorId) {
  std::lock_guard lock(mutex_); const auto published=stateLocked("published_revision"); const auto base=baseRevisionArg.empty()?published:baseRevisionArg;
  auto snapshot=snapshotArg?*snapshotArg:revisionSnapshotLocked(base); const auto id=nextDraftIdLocked(); const auto text=boost::json::serialize(snapshot);
  sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db_,"INSERT INTO config_drafts(draft_id,base_revision,snapshot_json,created_at,created_by) VALUES(?1,?2,?3,?4,?5);",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,base.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,text.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(stmt,4,nowMs());sqlite3_bind_text(stmt,5,actorId.c_str(),-1,SQLITE_TRANSIENT);const int rc=sqlite3_step(stmt);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));
  return {{"draftId",id},{"baseRevision",base},{"snapshot",snapshot}};
}

boost::json::object ConfigService::validateDraft(const std::string& draftId) const {
  std::lock_guard lock(mutex_); std::string base; const auto snapshot=draftSnapshotLocked(draftId,&base); const auto running=revisionSnapshotLocked(stateLocked("running_revision")); const auto validation=validateSnapshotLocked(snapshot,&running);
  return {{"draftId",draftId},{"baseRevision",base},{"valid",validation.errors.empty()},{"errors",validation.errors},{"warnings",validation.warnings},{"affectedObjects",validation.affectedObjects},{"requiresRestart",validation.requiresRestart}};
}

void ConfigService::appendOutboxLocked(const std::string& type, const std::string& objectId, const std::string& revision, const boost::json::object& payload) {
  sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db_,"INSERT INTO config_outbox(event_type,object_id,object_revision,payload_json,created_at,published) VALUES(?1,?2,?3,?4,?5,0);",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  const auto text=boost::json::serialize(payload);sqlite3_bind_text(stmt,1,type.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,objectId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,revision.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,4,text.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(stmt,5,nowMs());const int rc=sqlite3_step(stmt);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));
}

boost::json::object ConfigService::publishDraft(const std::string& draftId, const std::string& actorId, const std::string& reason) {
  std::lock_guard lock(mutex_); std::string base; const auto snapshot=draftSnapshotLocked(draftId,&base); const auto published=stateLocked("published_revision"); if(base!=published) return {{"ok",false},{"code","CONFIG_REVISION_CONFLICT"},{"expectedBaseRevision",published},{"draftBaseRevision",base}};
  const auto running=revisionSnapshotLocked(stateLocked("running_revision")); const auto validation=validateSnapshotLocked(snapshot,&running); if(!validation.errors.empty()) return {{"ok",false},{"code","CONFIG_VALIDATION_FAILED"},{"errors",validation.errors},{"warnings",validation.warnings},{"requiresRestart",validation.requiresRestart}};
  const auto revision=nextRevisionLocked();
  const auto text=boost::json::serialize(snapshot);
  const auto hash=digest(snapshot);
  const auto now=nowMs();
  char* error=nullptr; if(sqlite3_exec(db_,"BEGIN IMMEDIATE;",nullptr,nullptr,&error)!=SQLITE_OK){std::string msg=error?error:sqlite3_errmsg(db_);sqlite3_free(error);throw std::runtime_error(msg);} try {
    sqlite3_stmt* stmt=nullptr; const char* sql="INSERT INTO config_revisions(revision,parent_revision,snapshot_json,content_digest,schema_version,created_at,published_at,published_by,reason,requires_restart) VALUES(?1,?2,?3,?4,'smart-factory-platform-config/v1',?5,?5,?6,?7,?8);"; if(sqlite3_prepare_v2(db_,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db_)); sqlite3_bind_text(stmt,1,revision.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,base.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,text.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,4,hash.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(stmt,5,now);sqlite3_bind_text(stmt,6,actorId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,7,reason.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(stmt,8,validation.requiresRestart?1:0);const int rc=sqlite3_step(stmt);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));
    setStateLocked("published_revision",revision); if(!validation.requiresRestart)setStateLocked("running_revision",revision); appendOutboxLocked("CONFIG_PUBLISHED",revision,revision,{{"revision",revision},{"parentRevision",base},{"requiresRestart",validation.requiresRestart},{"actorId",actorId},{"reason",reason}});
    sqlite3_stmt* del=nullptr; if(sqlite3_prepare_v2(db_,"DELETE FROM config_drafts WHERE draft_id=?1;",-1,&del,nullptr)==SQLITE_OK){sqlite3_bind_text(del,1,draftId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(del);sqlite3_finalize(del);} if(sqlite3_exec(db_,"COMMIT;",nullptr,nullptr,&error)!=SQLITE_OK){std::string msg=error?error:sqlite3_errmsg(db_);sqlite3_free(error);throw std::runtime_error(msg);} }
  catch(...){sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);throw;}
  return {{"ok",true},{"revision",revision},{"parentRevision",base},{"requiresRestart",validation.requiresRestart},{"runningRevision",stateLocked("running_revision")},{"state",validation.requiresRestart?"PUBLISHED_REQUIRES_RESTART":"RUNNING"}};
}

boost::json::object ConfigService::rollback(const std::string& revision, const std::string& actorId, const std::string& reason) {
  std::lock_guard lock(mutex_);
  const auto target=revisionSnapshotLocked(revision);
  const auto published=stateLocked("published_revision");
  const auto running=revisionSnapshotLocked(stateLocked("running_revision"));
  const auto validation=validateSnapshotLocked(target,&running);
  if(!validation.errors.empty())return{{"ok",false},{"code","CONFIG_VALIDATION_FAILED"},{"errors",validation.errors}};
  const auto newRevision=nextRevisionLocked();
  const auto text=boost::json::serialize(target);
  const auto hash=digest(target);
  const auto now=nowMs();
  char* error=nullptr;if(sqlite3_exec(db_,"BEGIN IMMEDIATE;",nullptr,nullptr,&error)!=SQLITE_OK){std::string msg=error?error:sqlite3_errmsg(db_);sqlite3_free(error);throw std::runtime_error(msg);}try{sqlite3_stmt* stmt=nullptr;const char* sql="INSERT INTO config_revisions(revision,parent_revision,snapshot_json,content_digest,schema_version,created_at,published_at,published_by,reason,requires_restart) VALUES(?1,?2,?3,?4,'smart-factory-platform-config/v1',?5,?5,?6,?7,?8);";if(sqlite3_prepare_v2(db_,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db_));const auto rollbackReason="rollback to "+revision+": "+reason;sqlite3_bind_text(stmt,1,newRevision.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,published.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,text.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,4,hash.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(stmt,5,now);sqlite3_bind_text(stmt,6,actorId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,7,rollbackReason.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(stmt,8,validation.requiresRestart?1:0);const int rc=sqlite3_step(stmt);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));setStateLocked("published_revision",newRevision);if(!validation.requiresRestart)setStateLocked("running_revision",newRevision);appendOutboxLocked("CONFIG_ROLLED_BACK",newRevision,newRevision,{{"revision",newRevision},{"rollbackTarget",revision},{"requiresRestart",validation.requiresRestart},{"actorId",actorId},{"reason",reason}});if(sqlite3_exec(db_,"COMMIT;",nullptr,nullptr,&error)!=SQLITE_OK){std::string msg=error?error:sqlite3_errmsg(db_);sqlite3_free(error);throw std::runtime_error(msg);}}catch(...){sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);throw;}
  return {{"ok",true},{"revision",newRevision},{"rollbackTarget",revision},{"requiresRestart",validation.requiresRestart},{"runningRevision",stateLocked("running_revision")},{"state",validation.requiresRestart?"PUBLISHED_REQUIRES_RESTART":"RUNNING"}};
}

}  // namespace smart_factory
