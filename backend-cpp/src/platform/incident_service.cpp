#include "smart_factory/platform/incident_service.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace smart_factory {
namespace {
void execSql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}
std::string textCol(sqlite3_stmt* stmt, int column) {
  const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
  return p ? std::string(p) : std::string();
}
}  // namespace

std::int64_t IncidentService::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string IncidentService::str(const boost::json::object& obj, const char* key, const std::string& fallback) {
  if (const auto* v = obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return fallback;
}
std::int64_t IncidentService::integer(const boost::json::object& obj, const char* key, std::int64_t fallback) {
  if (const auto* v = obj.if_contains(key)) {
    if (v->is_int64()) return v->as_int64();
    if (v->is_uint64()) return static_cast<std::int64_t>(v->as_uint64());
  }
  return fallback;
}

IncidentService::IncidentService(std::string directory) : directory_(std::move(directory)) {
  std::filesystem::create_directories(directory_);
  path_ = (std::filesystem::path(directory_) / "platform-incident-b3.sqlite3").string();
  if (sqlite3_open_v2(path_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    throw std::runtime_error("cannot open incident store");
  init();
}
IncidentService::~IncidentService() {
  std::lock_guard lock(mutex_);
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}
void IncidentService::init() {
  std::lock_guard lock(mutex_);
  execSql(db_, "PRAGMA journal_mode=WAL;");
  execSql(db_, "PRAGMA synchronous=FULL;");
  execSql(db_, R"SQL(
    CREATE TABLE IF NOT EXISTS incidents(
      incident_id TEXT PRIMARY KEY,
      dedup_key TEXT UNIQUE NOT NULL,
      status TEXT NOT NULL,
      revision INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      incident_json TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS incident_events(
      event_id TEXT PRIMARY KEY,
      incident_id TEXT NOT NULL,
      event_type TEXT NOT NULL,
      timestamp INTEGER NOT NULL,
      event_json TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS incident_outbox(
      event_id TEXT PRIMARY KEY,
      object_id TEXT NOT NULL,
      revision INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      published INTEGER NOT NULL DEFAULT 0
    );
  )SQL");
}

boost::json::object IncidentService::readLocked(const std::string& id) const {
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT incident_json FROM incidents WHERE incident_id=?1;", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("incident not found");
  }
  auto parsed = boost::json::parse(textCol(stmt, 0));
  sqlite3_finalize(stmt);
  return parsed.as_object();
}

void IncidentService::appendEventLocked(const boost::json::object& incident, const std::string& type, const std::string& comment) {
  const std::int64_t ts = nowMs();
  const std::string incidentId = str(incident, "incidentId");
  const std::int64_t revision = integer(incident, "revision", 1);
  const std::string eventId = incidentId + ":" + type + ":" + std::to_string(revision) + ":" + std::to_string(ts);
  boost::json::object event{{"eventId",eventId},{"incidentId",incidentId},{"eventType",type},{"timestamp",ts},{"revision",revision},{"status",str(incident,"status")}};
  if (!comment.empty()) event["comment"] = comment;
  const auto text = boost::json::serialize(event);

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "INSERT INTO incident_events(event_id,incident_id,event_type,timestamp,event_json) VALUES(?1,?2,?3,?4,?5);", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt,1,eventId.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,2,incidentId.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,3,type.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt,4,ts);
  sqlite3_bind_text(stmt,5,text.c_str(),-1,SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) { const auto m=std::string(sqlite3_errmsg(db_)); sqlite3_finalize(stmt); throw std::runtime_error(m); }
  sqlite3_finalize(stmt);

  sqlite3_prepare_v2(db_, "INSERT INTO incident_outbox(event_id,object_id,revision,payload_json,created_at,published) VALUES(?1,?2,?3,?4,?5,0);", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt,1,eventId.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,2,incidentId.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt,3,revision);
  sqlite3_bind_text(stmt,4,text.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt,5,ts);
  if (sqlite3_step(stmt) != SQLITE_DONE) { const auto m=std::string(sqlite3_errmsg(db_)); sqlite3_finalize(stmt); throw std::runtime_error(m); }
  sqlite3_finalize(stmt);
}

boost::json::object IncidentService::create(const boost::json::object& request, const SecurityContext& actor) {
  const auto* occurrenceValue = request.if_contains("occurrenceIds");
  if (!occurrenceValue || !occurrenceValue->is_array() || occurrenceValue->as_array().empty())
    throw std::runtime_error("occurrenceIds[] required");
  std::vector<std::string> ids;
  for (const auto& value : occurrenceValue->as_array()) if (value.is_string()) ids.emplace_back(value.as_string());
  if (ids.empty()) throw std::runtime_error("occurrenceIds[] required");
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  std::string dedupKey;
  boost::json::array occurrenceIds;
  for (const auto& id : ids) { dedupKey += id + "|"; occurrenceIds.emplace_back(id); }

  std::lock_guard lock(mutex_);
  sqlite3_stmt* query = nullptr;
  sqlite3_prepare_v2(db_, "SELECT incident_json FROM incidents WHERE dedup_key=?1 AND status!='CLOSED' LIMIT 1;", -1, &query, nullptr);
  sqlite3_bind_text(query,1,dedupKey.c_str(),-1,SQLITE_TRANSIENT);
  if (sqlite3_step(query) == SQLITE_ROW) {
    auto parsed = boost::json::parse(textCol(query,0)); sqlite3_finalize(query); return parsed.as_object();
  }
  sqlite3_finalize(query);

  const auto ts = nowMs();
  const auto id = "inc-" + std::to_string(ts);
  boost::json::object incident{{"incidentId",id},{"status","OPEN"},{"revision",1},{"title",str(request,"title","报警处置")},{"priority",str(request,"priority","NORMAL")},{"assignee",str(request,"assignee",actor.principal.id)},{"occurrenceIds",occurrenceIds},{"createdAt",ts},{"updatedAt",ts},{"createdBy",actor.principal.id},{"description",str(request,"description")}};
  const auto text = boost::json::serialize(incident);

  execSql(db_, "BEGIN IMMEDIATE;");
  try {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO incidents(incident_id,dedup_key,status,revision,updated_at,incident_json) VALUES(?1,?2,'OPEN',1,?3,?4);", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,dedupKey.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,3,ts);
    sqlite3_bind_text(stmt,4,text.c_str(),-1,SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) { const auto m=std::string(sqlite3_errmsg(db_)); sqlite3_finalize(stmt); throw std::runtime_error(m); }
    sqlite3_finalize(stmt);
    appendEventLocked(incident,"CREATED","");
    execSql(db_, "COMMIT;");
    return incident;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

boost::json::object IncidentService::transition(const std::string& id, const boost::json::object& request, const SecurityContext& actor) {
  std::lock_guard lock(mutex_);
  execSql(db_, "BEGIN IMMEDIATE;");
  try {
    auto incident = readLocked(id);
    const auto expectedRevision = integer(request,"expectedRevision",-1);
    if (expectedRevision >= 0 && expectedRevision != integer(incident,"revision")) throw std::runtime_error("INCIDENT_REVISION_CONFLICT");
    const auto action = str(request,"action");
    const auto current = str(incident,"status");
    std::string next;
    if (action=="START" && current=="OPEN") next="IN_PROGRESS";
    else if (action=="RESOLVE" && (current=="OPEN"||current=="IN_PROGRESS")) next="RESOLVED";
    else if (action=="CLOSE" && current=="RESOLVED") next="CLOSED";
    else if (action=="REOPEN" && (current=="RESOLVED"||current=="CLOSED")) next="IN_PROGRESS";
    else if (action=="ASSIGN") next=current;
    else throw std::runtime_error("INCIDENT_TRANSITION_INVALID");

    incident["status"] = next;
    incident["revision"] = integer(incident,"revision") + 1;
    incident["updatedAt"] = nowMs();
    incident["lastActor"] = actor.principal.id;
    if (const auto assignee=str(request,"assignee"); !assignee.empty()) incident["assignee"] = assignee;
    if (const auto comment=str(request,"comment"); !comment.empty()) incident["lastComment"] = comment;
    const auto text = boost::json::serialize(incident);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE incidents SET status=?2,revision=?3,updated_at=?4,incident_json=?5 WHERE incident_id=?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,next.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,3,integer(incident,"revision"));
    sqlite3_bind_int64(stmt,4,integer(incident,"updatedAt"));
    sqlite3_bind_text(stmt,5,text.c_str(),-1,SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) { const auto m=std::string(sqlite3_errmsg(db_)); sqlite3_finalize(stmt); throw std::runtime_error(m); }
    sqlite3_finalize(stmt);
    appendEventLocked(incident,action,str(request,"comment"));
    execSql(db_, "COMMIT;");
    return incident;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

boost::json::array IncidentService::list() const {
  std::lock_guard lock(mutex_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT incident_json FROM incidents ORDER BY updated_at DESC;", -1, &stmt, nullptr);
  boost::json::array out;
  while (sqlite3_step(stmt) == SQLITE_ROW) { auto parsed=boost::json::parse(textCol(stmt,0)); out.emplace_back(parsed.as_object()); }
  sqlite3_finalize(stmt);
  return out;
}
std::optional<boost::json::object> IncidentService::get(const std::string& id) const {
  std::lock_guard lock(mutex_);
  try { return readLocked(id); } catch (...) { return std::nullopt; }
}
boost::json::array IncidentService::events(std::int64_t fromTs, std::int64_t toTs) const {
  std::lock_guard lock(mutex_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT event_json FROM incident_events WHERE timestamp>=?1 AND timestamp<=?2 ORDER BY timestamp ASC;", -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt,1,fromTs); sqlite3_bind_int64(stmt,2,toTs);
  boost::json::array out;
  while (sqlite3_step(stmt) == SQLITE_ROW) { auto parsed=boost::json::parse(textCol(stmt,0)); out.emplace_back(parsed.as_object()); }
  sqlite3_finalize(stmt);
  return out;
}

}  // namespace smart_factory
