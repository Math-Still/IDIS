#include "smart_factory/historian.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <string>
#include <utility>

namespace smart_factory {
namespace {

class Statement {
 public:
  Statement(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("sqlite prepare failed: ") + sqlite3_errmsg(db));
    }
  }
  ~Statement() { if (stmt_) sqlite3_finalize(stmt_); }
  sqlite3_stmt* get() const noexcept { return stmt_; }
 private:
  sqlite3_stmt* stmt_{nullptr};
};

void exec(sqlite3* db, const char* sql) {
  char* error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

std::size_t boundedLimit(std::size_t limit) {
  if (limit == 0) return 1;
  return std::min<std::size_t>(limit, 10000);
}

boost::json::object parseObjectColumn(sqlite3_stmt* stmt, int column) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
  if (!text) return {};
  auto parsed = boost::json::parse(text);
  if (!parsed.is_object()) throw std::runtime_error("historian JSON column is not an object");
  return parsed.as_object();
}

std::string legacyOccurrenceId(const boost::json::object& alarm, const std::string& alarmId) {
  std::int64_t raisedAt = 0;
  if (const auto* v = alarm.if_contains("raisedAt"); v && v->is_int64()) raisedAt = v->as_int64();
  return "legacy:" + alarmId + ":" + std::to_string(raisedAt);
}

std::uint64_t fileSizeOrZero(const std::string& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  return ec ? 0 : static_cast<std::uint64_t>(size);
}

}  // namespace

HistorianStore::HistorianStore(std::string directory, HistorianOptions options)
    : directory_(std::move(directory)), options_(std::move(options)) {
  if (directory_.empty()) throw std::runtime_error("historian directory is empty");
  std::filesystem::create_directories(directory_);
  if (options_.backupEnabled) {
    if (options_.backupDirectory.empty()) {
      options_.backupDirectory = (std::filesystem::path(directory_) / "backup").string();
    }
    std::filesystem::create_directories(options_.backupDirectory);
  }
  databasePath_ = (std::filesystem::path(directory_) / "historian.sqlite3").string();
  if (sqlite3_open_v2(databasePath_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown sqlite open error";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("cannot open historian: " + message);
  }
  std::lock_guard lock(mutex_);
  initializeLocked();
  migrateLegacyJsonlLocked();
  pruneExpiredLocked(nowMs());
  lastMaintenanceAt_ = nowMs();
  recoverLastBackupState();
  maintenanceRunning_.store(true);
  maintenanceThread_ = std::thread([this] { maintenanceLoop(); });
}

HistorianStore::~HistorianStore() {
  maintenanceRunning_.store(false);
  maintenanceCv_.notify_all();
  if (maintenanceThread_.joinable()) maintenanceThread_.join();
  std::lock_guard lock(mutex_);
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

void HistorianStore::initializeLocked() {
  exec(db_, "PRAGMA journal_mode=WAL;");
  exec(db_, "PRAGMA synchronous=FULL;");
  exec(db_, "PRAGMA foreign_keys=ON;");
  exec(db_, "PRAGMA busy_timeout=3000;");
  exec(db_, "PRAGMA wal_autocheckpoint=1000;");
  exec(db_, R"SQL(
    CREATE TABLE IF NOT EXISTS telemetry (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      point_id TEXT NOT NULL,
      source_epoch TEXT NOT NULL DEFAULT '',
      sample_ts INTEGER NOT NULL,
      receive_ts INTEGER NOT NULL DEFAULT 0,
      seq INTEGER NOT NULL DEFAULT -1,
      point_json TEXT NOT NULL,
      UNIQUE(device_id, point_id, source_epoch, sample_ts, seq)
    );
    CREATE INDEX IF NOT EXISTS idx_telemetry_point_time
      ON telemetry(device_id, point_id, sample_ts);
    CREATE INDEX IF NOT EXISTS idx_telemetry_time ON telemetry(sample_ts);
    CREATE TABLE IF NOT EXISTS telemetry_cursor (
      device_id TEXT NOT NULL,
      point_id TEXT NOT NULL,
      source_epoch TEXT NOT NULL DEFAULT '',
      sample_ts INTEGER NOT NULL,
      seq INTEGER NOT NULL DEFAULT -1,
      updated_at INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY(device_id, point_id)
    );

    CREATE TABLE IF NOT EXISTS alarm_current (
      alarm_id TEXT PRIMARY KEY,
      occurrence_id TEXT NOT NULL,
      state TEXT NOT NULL,
      updated_at INTEGER NOT NULL,
      alarm_json TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS alarm_occurrences (
      occurrence_id TEXT PRIMARY KEY,
      alarm_id TEXT NOT NULL,
      device_id TEXT NOT NULL,
      raised_at INTEGER NOT NULL,
      cleared_at INTEGER NOT NULL DEFAULT 0,
      state TEXT NOT NULL,
      alarm_json TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_alarm_occurrences_alarm_time
      ON alarm_occurrences(alarm_id, raised_at);

    CREATE TABLE IF NOT EXISTS alarm_events (
      event_id TEXT PRIMARY KEY,
      occurrence_id TEXT NOT NULL,
      alarm_id TEXT NOT NULL,
      device_id TEXT NOT NULL,
      event_type TEXT NOT NULL,
      timestamp INTEGER NOT NULL,
      source TEXT NOT NULL,
      event_json TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_alarm_events_alarm_time
      ON alarm_events(alarm_id, timestamp);
    CREATE INDEX IF NOT EXISTS idx_alarm_events_device_time
      ON alarm_events(device_id, timestamp);
  )SQL");
  ensureTelemetryEpochSchemaLocked();
  seedTelemetryCursorsLocked();
}

void HistorianStore::ensureTelemetryEpochSchemaLocked() {
  bool hasSourceEpoch = false;
  {
    Statement info(db_, "PRAGMA table_info(telemetry);");
    while (sqlite3_step(info.get()) == SQLITE_ROW) {
      const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(info.get(), 1));
      if (name && std::string(name) == "source_epoch") { hasSourceEpoch = true; break; }
    }
  }
  if (hasSourceEpoch) return;

  exec(db_, "BEGIN IMMEDIATE;");
  try {
    exec(db_, "ALTER TABLE telemetry RENAME TO telemetry_legacy_b2;");
    exec(db_, "DROP INDEX IF EXISTS idx_telemetry_point_time;");
    exec(db_, "DROP INDEX IF EXISTS idx_telemetry_time;");
    exec(db_, R"SQL(
      CREATE TABLE telemetry (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id TEXT NOT NULL,
        point_id TEXT NOT NULL,
        source_epoch TEXT NOT NULL DEFAULT '',
        sample_ts INTEGER NOT NULL,
        receive_ts INTEGER NOT NULL DEFAULT 0,
        seq INTEGER NOT NULL DEFAULT -1,
        point_json TEXT NOT NULL,
        UNIQUE(device_id, point_id, source_epoch, sample_ts, seq)
      );
      INSERT INTO telemetry(id,device_id,point_id,source_epoch,sample_ts,receive_ts,seq,point_json)
        SELECT id,device_id,point_id,'',sample_ts,receive_ts,seq,point_json FROM telemetry_legacy_b2;
      DROP TABLE telemetry_legacy_b2;
      CREATE INDEX idx_telemetry_point_time ON telemetry(device_id, point_id, sample_ts);
      CREATE INDEX idx_telemetry_time ON telemetry(sample_ts);
    )SQL");
    exec(db_, "COMMIT;");
  } catch (...) {
    try { exec(db_, "ROLLBACK;"); } catch (...) {}
    throw;
  }
}

void HistorianStore::seedTelemetryCursorsLocked() {
  // Existing legacy rows have no epoch. Seed the cursor only when a key does
  // not yet have an authoritative live cursor. Within the legacy epoch choose
  // the greatest sampleTs/seq rather than last SQLite insertion order.
  exec(db_, R"SQL(
    INSERT OR IGNORE INTO telemetry_cursor(device_id,point_id,source_epoch,sample_ts,seq,updated_at)
    SELECT t.device_id,t.point_id,t.source_epoch,t.sample_ts,t.seq,t.receive_ts
    FROM telemetry t
    WHERE NOT EXISTS (
      SELECT 1 FROM telemetry_cursor existing
      WHERE existing.device_id=t.device_id AND existing.point_id=t.point_id
    ) AND NOT EXISTS (
      SELECT 1 FROM telemetry newer
      WHERE newer.device_id=t.device_id AND newer.point_id=t.point_id AND newer.source_epoch=t.source_epoch
        AND (newer.sample_ts>t.sample_ts OR (newer.sample_ts=t.sample_ts AND newer.seq>t.seq)
             OR (newer.sample_ts=t.sample_ts AND newer.seq=t.seq AND newer.id>t.id))
    );
  )SQL");
}

std::int64_t HistorianStore::intField(const boost::json::object& obj, const char* key, std::int64_t fallback) {
  const auto* v = obj.if_contains(key);
  if (!v) return fallback;
  if (v->is_int64()) return v->as_int64();
  if (v->is_uint64() && v->as_uint64() <= static_cast<std::uint64_t>(INT64_MAX)) return static_cast<std::int64_t>(v->as_uint64());
  return fallback;
}

std::string HistorianStore::stringField(const boost::json::object& obj, const char* key) {
  if (const auto* v = obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return {};
}

void HistorianStore::insertTelemetryLocked(const boost::json::object& point) {
  const auto deviceId = stringField(point, "deviceId");
  const auto pointId = stringField(point, "pointId");
  const auto sourceEpoch = stringField(point, "sourceEpoch");
  const auto sampleTs = intField(point, "sampleTs");
  const auto receiveTs = intField(point, "receiveTs");
  const auto seq = intField(point, "seq", -1);
  if (deviceId.empty() || pointId.empty() || sampleTs <= 0) {
    throw std::runtime_error("historian telemetry requires deviceId, pointId and positive sampleTs");
  }
  const auto payload = boost::json::serialize(point);
  Statement stmt(db_, R"SQL(
    INSERT OR IGNORE INTO telemetry(device_id,point_id,source_epoch,sample_ts,receive_ts,seq,point_json)
    VALUES(?1,?2,?3,?4,?5,?6,?7);
  )SQL");
  sqlite3_bind_text(stmt.get(), 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, pointId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 3, sourceEpoch.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt.get(), 4, sampleTs);
  sqlite3_bind_int64(stmt.get(), 5, receiveTs);
  sqlite3_bind_int64(stmt.get(), 6, seq);
  sqlite3_bind_text(stmt.get(), 7, payload.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
    throw std::runtime_error(std::string("sqlite telemetry insert failed: ") + sqlite3_errmsg(db_));
  }
  Statement cursor(db_, R"SQL(
    INSERT INTO telemetry_cursor(device_id,point_id,source_epoch,sample_ts,seq,updated_at)
    VALUES(?1,?2,?3,?4,?5,?6)
    ON CONFLICT(device_id,point_id) DO UPDATE SET
      source_epoch=excluded.source_epoch,sample_ts=excluded.sample_ts,seq=excluded.seq,updated_at=excluded.updated_at
    WHERE telemetry_cursor.source_epoch<>excluded.source_epoch
       OR excluded.sample_ts>telemetry_cursor.sample_ts
       OR (excluded.sample_ts=telemetry_cursor.sample_ts AND excluded.seq>telemetry_cursor.seq);
  )SQL");
  sqlite3_bind_text(cursor.get(),1,deviceId.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(cursor.get(),2,pointId.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(cursor.get(),3,sourceEpoch.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(cursor.get(),4,sampleTs);
  sqlite3_bind_int64(cursor.get(),5,seq);
  sqlite3_bind_int64(cursor.get(),6,receiveTs>0?receiveTs:nowMs());
  if(sqlite3_step(cursor.get())!=SQLITE_DONE) throw std::runtime_error(std::string("sqlite telemetry cursor update failed: ")+sqlite3_errmsg(db_));
}

void HistorianStore::insertAlarmEventLocked(const boost::json::object& event) {
  const auto alarmId = stringField(event, "alarmId");
  const auto deviceId = stringField(event, "deviceId");
  const auto eventType = stringField(event, "eventType");
  const auto timestamp = intField(event, "timestamp");
  const auto source = stringField(event, "source");
  const auto* alarmValue = event.if_contains("alarm");
  if (alarmId.empty() || eventType.empty() || timestamp <= 0 || !alarmValue || !alarmValue->is_object()) {
    throw std::runtime_error("historian alarm event missing lifecycle fields");
  }
  auto alarm = alarmValue->as_object();
  auto occurrenceId = stringField(event, "occurrenceId");
  if (occurrenceId.empty()) occurrenceId = stringField(alarm, "occurrenceId");
  if (occurrenceId.empty()) occurrenceId = legacyOccurrenceId(alarm, alarmId);
  alarm["occurrenceId"] = occurrenceId;

  auto eventId = stringField(event, "eventId");
  if (eventId.empty()) eventId = occurrenceId + ":" + eventType + ":" + std::to_string(timestamp);
  auto persistedEvent = event;
  persistedEvent["eventId"] = eventId;
  persistedEvent["occurrenceId"] = occurrenceId;
  persistedEvent["alarm"] = alarm;
  const auto eventPayload = boost::json::serialize(persistedEvent);
  const auto alarmPayload = boost::json::serialize(alarm);
  const auto state = stringField(alarm, "state");
  const auto raisedAt = intField(alarm, "raisedAt", timestamp);
  const auto clearedAt = intField(alarm, "clearedAt", 0);

  exec(db_, "BEGIN IMMEDIATE;");
  try {
    {
      Statement stmt(db_, R"SQL(
        INSERT OR IGNORE INTO alarm_events(event_id,occurrence_id,alarm_id,device_id,event_type,timestamp,source,event_json)
        VALUES(?1,?2,?3,?4,?5,?6,?7,?8);
      )SQL");
      sqlite3_bind_text(stmt.get(), 1, eventId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 2, occurrenceId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 3, alarmId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 4, deviceId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 5, eventType.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt.get(), 6, timestamp);
      sqlite3_bind_text(stmt.get(), 7, source.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 8, eventPayload.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    {
      Statement stmt(db_, R"SQL(
        INSERT INTO alarm_current(alarm_id,occurrence_id,state,updated_at,alarm_json)
        VALUES(?1,?2,?3,?4,?5)
        ON CONFLICT(alarm_id) DO UPDATE SET
          occurrence_id=excluded.occurrence_id,state=excluded.state,
          updated_at=excluded.updated_at,alarm_json=excluded.alarm_json;
      )SQL");
      sqlite3_bind_text(stmt.get(), 1, alarmId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 2, occurrenceId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 3, state.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt.get(), 4, timestamp);
      sqlite3_bind_text(stmt.get(), 5, alarmPayload.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    {
      Statement stmt(db_, R"SQL(
        INSERT INTO alarm_occurrences(occurrence_id,alarm_id,device_id,raised_at,cleared_at,state,alarm_json)
        VALUES(?1,?2,?3,?4,?5,?6,?7)
        ON CONFLICT(occurrence_id) DO UPDATE SET
          cleared_at=excluded.cleared_at,state=excluded.state,alarm_json=excluded.alarm_json;
      )SQL");
      sqlite3_bind_text(stmt.get(), 1, occurrenceId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 2, alarmId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 3, deviceId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt.get(), 4, raisedAt);
      sqlite3_bind_int64(stmt.get(), 5, clearedAt);
      sqlite3_bind_text(stmt.get(), 6, state.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt.get(), 7, alarmPayload.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    exec(db_, "COMMIT;");
  } catch (...) {
    try { exec(db_, "ROLLBACK;"); } catch (...) {}
    throw;
  }
}

void HistorianStore::appendTelemetry(const boost::json::object& point) {
  std::lock_guard lock(mutex_);
  insertTelemetryLocked(point);
}

void HistorianStore::appendAlarmEvent(const boost::json::object& event) {
  std::lock_guard lock(mutex_);
  insertAlarmEventLocked(event);
}

boost::json::object HistorianStore::queryTelemetry(const TelemetryHistoryQuery& query) const {
  std::lock_guard lock(mutex_);
  const auto limit = boundedLimit(query.limit);
  std::string where = " WHERE sample_ts>=?1 AND sample_ts<=?2";
  if (!query.deviceId.empty()) where += " AND device_id=?3";
  if (!query.pointId.empty()) where += query.deviceId.empty() ? " AND point_id=?3" : " AND point_id=?4";

  std::size_t total = 0;
  {
    Statement count(db_, ("SELECT COUNT(*) FROM telemetry" + where + ";").c_str());
    sqlite3_bind_int64(count.get(), 1, query.fromTs);
    sqlite3_bind_int64(count.get(), 2, query.toTs);
    int bind = 3;
    if (!query.deviceId.empty()) sqlite3_bind_text(count.get(), bind++, query.deviceId.c_str(), -1, SQLITE_TRANSIENT);
    if (!query.pointId.empty()) sqlite3_bind_text(count.get(), bind++, query.pointId.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(count.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    total = static_cast<std::size_t>(sqlite3_column_int64(count.get(), 0));
  }

  boost::json::array items;
  int bind = 3 + (!query.deviceId.empty() ? 1 : 0) + (!query.pointId.empty() ? 1 : 0);
  std::string sql = "SELECT point_json FROM telemetry" + where + " ORDER BY sample_ts DESC,id DESC LIMIT ?" + std::to_string(bind) + ";";
  Statement rows(db_, sql.c_str());
  sqlite3_bind_int64(rows.get(), 1, query.fromTs);
  sqlite3_bind_int64(rows.get(), 2, query.toTs);
  int rowBind = 3;
  if (!query.deviceId.empty()) sqlite3_bind_text(rows.get(), rowBind++, query.deviceId.c_str(), -1, SQLITE_TRANSIENT);
  if (!query.pointId.empty()) sqlite3_bind_text(rows.get(), rowBind++, query.pointId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(rows.get(), rowBind, static_cast<sqlite3_int64>(limit));
  while (true) {
    const int rc = sqlite3_step(rows.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    items.emplace_back(parseObjectColumn(rows.get(), 0));
  }
  std::reverse(items.begin(), items.end());
  return {{"items",std::move(items)},{"totalMatched",static_cast<std::int64_t>(total)},
          {"limit",static_cast<std::int64_t>(limit)},{"from",query.fromTs},{"to",query.toTs},{"truncated",total>limit}};
}

boost::json::object HistorianStore::queryAlarmEvents(const AlarmHistoryQuery& query) const {
  std::lock_guard lock(mutex_);
  const auto limit = boundedLimit(query.limit);
  std::string where = " WHERE timestamp>=?1 AND timestamp<=?2";
  if (!query.deviceId.empty()) where += " AND device_id=?3";
  if (!query.alarmId.empty()) where += query.deviceId.empty() ? " AND alarm_id=?3" : " AND alarm_id=?4";

  std::size_t total = 0;
  {
    Statement count(db_, ("SELECT COUNT(*) FROM alarm_events" + where + ";").c_str());
    sqlite3_bind_int64(count.get(), 1, query.fromTs);
    sqlite3_bind_int64(count.get(), 2, query.toTs);
    int bind = 3;
    if (!query.deviceId.empty()) sqlite3_bind_text(count.get(), bind++, query.deviceId.c_str(), -1, SQLITE_TRANSIENT);
    if (!query.alarmId.empty()) sqlite3_bind_text(count.get(), bind++, query.alarmId.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(count.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    total = static_cast<std::size_t>(sqlite3_column_int64(count.get(), 0));
  }

  boost::json::array items;
  int bind = 3 + (!query.deviceId.empty() ? 1 : 0) + (!query.alarmId.empty() ? 1 : 0);
  std::string sql = "SELECT event_json FROM alarm_events" + where + " ORDER BY timestamp DESC,rowid DESC LIMIT ?" + std::to_string(bind) + ";";
  Statement rows(db_, sql.c_str());
  sqlite3_bind_int64(rows.get(), 1, query.fromTs);
  sqlite3_bind_int64(rows.get(), 2, query.toTs);
  int rowBind = 3;
  if (!query.deviceId.empty()) sqlite3_bind_text(rows.get(), rowBind++, query.deviceId.c_str(), -1, SQLITE_TRANSIENT);
  if (!query.alarmId.empty()) sqlite3_bind_text(rows.get(), rowBind++, query.alarmId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(rows.get(), rowBind, static_cast<sqlite3_int64>(limit));
  while (true) {
    const int rc = sqlite3_step(rows.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    items.emplace_back(parseObjectColumn(rows.get(), 0));
  }
  std::reverse(items.begin(), items.end());
  return {{"items",std::move(items)},{"totalMatched",static_cast<std::int64_t>(total)},
          {"limit",static_cast<std::int64_t>(limit)},{"from",query.fromTs},{"to",query.toTs},{"truncated",total>limit}};
}

std::unordered_map<std::string, boost::json::object> HistorianStore::replayAlarmStates() const {
  std::lock_guard lock(mutex_);
  std::unordered_map<std::string, boost::json::object> out;
  Statement stmt(db_, "SELECT alarm_id,alarm_json FROM alarm_current;");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    const auto* alarmId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    if (alarmId) out[alarmId] = parseObjectColumn(stmt.get(), 1);
  }
  return out;
}

std::unordered_map<std::string, TelemetryCursorRecord> HistorianStore::latestTelemetryCursors() const {
  std::lock_guard lock(mutex_);
  std::unordered_map<std::string, TelemetryCursorRecord> out;
  Statement stmt(db_, "SELECT device_id,point_id,sample_ts,seq,source_epoch FROM telemetry_cursor;");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    const auto* device = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    const auto* point = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
    const auto* epoch = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4));
    if (!device || !point) continue;
    out[std::string(device) + ":" + point] = {sqlite3_column_int64(stmt.get(),2), sqlite3_column_int64(stmt.get(),3), epoch?epoch:""};
  }
  return out;
}

std::size_t HistorianStore::telemetryRecordCount() const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT COUNT(*) FROM telemetry;");
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
  return static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
}

std::size_t HistorianStore::alarmEventCount() const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT COUNT(*) FROM alarm_events;");
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
  return static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
}

std::size_t HistorianStore::alarmOccurrenceCount() const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT COUNT(*) FROM alarm_occurrences;");
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
  return static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
}

void HistorianStore::pruneExpiredLocked(std::int64_t now) {
  if (options_.retentionDays <= 0) return;
  const auto cutoff = now - static_cast<std::int64_t>(options_.retentionDays) * 24LL * 60LL * 60LL * 1000LL;
  exec(db_, "BEGIN IMMEDIATE;");
  try {
    for (const char* sql : {
        "DELETE FROM telemetry WHERE sample_ts < ?1;",
        "DELETE FROM alarm_events WHERE timestamp < ?1;",
        "DELETE FROM alarm_occurrences WHERE cleared_at > 0 AND cleared_at < ?1;"}) {
      Statement stmt(db_, sql);
      sqlite3_bind_int64(stmt.get(), 1, cutoff);
      if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    exec(db_, "COMMIT;");
  } catch (...) {
    try { exec(db_, "ROLLBACK;"); } catch (...) {}
    throw;
  }
}

void HistorianStore::pruneExpired() {
  std::lock_guard lock(mutex_);
  pruneExpiredLocked(nowMs());
  lastMaintenanceAt_ = nowMs();
}

void HistorianStore::recoverLastBackupState() {
  if (!options_.backupEnabled) {
    lastBackupAt_ = nowMs();
    return;
  }
  std::int64_t newest = 0;
  std::string newestPath;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(options_.backupDirectory, ec)) {
    if (ec || !entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    constexpr const char* prefix = "historian-";
    constexpr const char* suffix = ".sqlite3";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)) continue;
    const auto token = name.substr(std::char_traits<char>::length(prefix),
                                   name.size() - std::char_traits<char>::length(prefix) - std::char_traits<char>::length(suffix));
    try {
      const auto ts = std::stoll(token);
      if (ts > newest) { newest = ts; newestPath = entry.path().string(); }
    } catch (...) {}
  }
  lastBackupAt_ = newest > 0 ? newest : nowMs();
  lastBackupPath_ = std::move(newestPath);
}

void HistorianStore::maintenanceLoop() {
  std::unique_lock waitLock(maintenanceWaitMutex_);
  while (maintenanceRunning_.load()) {
    maintenanceCv_.wait_for(waitLock, std::chrono::minutes(1), [this] { return !maintenanceRunning_.load(); });
    if (!maintenanceRunning_.load()) break;
    waitLock.unlock();
    const auto now = nowMs();
    try {
      bool pruneDue = false;
      {
        std::lock_guard lock(mutex_);
        pruneDue = lastMaintenanceAt_ == 0 || now - lastMaintenanceAt_ >= 60LL * 60LL * 1000LL;
      }
      if (pruneDue) pruneExpired();

      bool backupDue = false;
      if (options_.backupEnabled && options_.backupIntervalHours > 0) {
        std::lock_guard lock(mutex_);
        backupDue = now - lastBackupAt_ >= static_cast<std::int64_t>(options_.backupIntervalHours) * 60LL * 60LL * 1000LL;
      }
      if (backupDue) backupNow();
      {
        std::lock_guard lock(mutex_);
        lastMaintenanceError_.clear();
      }
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex_);
      lastMaintenanceError_ = e.what();
    } catch (...) {
      std::lock_guard lock(mutex_);
      lastMaintenanceError_ = "unknown historian maintenance failure";
    }
    waitLock.lock();
  }
}

bool HistorianStore::verifyDatabaseHandle(sqlite3* db, std::string* detail) {
  Statement stmt(db, "PRAGMA integrity_check;");
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_ROW) {
    if (detail) *detail = sqlite3_errmsg(db);
    return false;
  }
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
  const std::string result = text ? text : "";
  if (detail) *detail = result;
  return result == "ok";
}

bool HistorianStore::verifyDatabaseFile(const std::string& path, std::string* detail) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    if (detail) *detail = db ? sqlite3_errmsg(db) : "cannot open database";
    if (db) sqlite3_close(db);
    return false;
  }
  bool ok = false;
  try { ok = verifyDatabaseHandle(db, detail); } catch (const std::exception& e) { if (detail) *detail = e.what(); ok = false; }
  sqlite3_close(db);
  return ok;
}

std::string HistorianStore::createOnlineBackup(std::int64_t now) {
  if (!options_.backupEnabled) throw std::runtime_error("historian backup is disabled");
  std::filesystem::create_directories(options_.backupDirectory);
  const auto path = (std::filesystem::path(options_.backupDirectory) /
                     ("historian-" + std::to_string(now) + ".sqlite3")).string();

  // Use a separate read connection so a potentially large online backup does
  // not hold HistorianStore::mutex_ and stall the acquisition write path.
  sqlite3* source = nullptr;
  if (sqlite3_open_v2(databasePath_.c_str(), &source, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    const std::string message = source ? sqlite3_errmsg(source) : "cannot open historian source database";
    if (source) sqlite3_close(source);
    throw std::runtime_error("historian backup source open failed: " + message);
  }
  sqlite3_busy_timeout(source, 3000);

  sqlite3* destination = nullptr;
  if (sqlite3_open_v2(path.c_str(), &destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    const std::string message = destination ? sqlite3_errmsg(destination) : "cannot create backup database";
    if (destination) sqlite3_close(destination);
    sqlite3_close(source);
    throw std::runtime_error("historian backup open failed: " + message);
  }
  sqlite3_busy_timeout(destination, 3000);

  sqlite3_backup* backup = sqlite3_backup_init(destination, "main", source, "main");
  if (!backup) {
    const std::string message = sqlite3_errmsg(destination);
    sqlite3_close(destination); sqlite3_close(source);
    throw std::runtime_error("historian backup init failed: " + message);
  }

  int step = SQLITE_OK;
  do {
    step = sqlite3_backup_step(backup, 256);
    if (step == SQLITE_BUSY || step == SQLITE_LOCKED) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (step == SQLITE_OK || step == SQLITE_BUSY || step == SQLITE_LOCKED);
  const int finish = sqlite3_backup_finish(backup);
  const std::string finishMessage = sqlite3_errmsg(destination);
  sqlite3_close(destination);
  sqlite3_close(source);
  if (step != SQLITE_DONE || finish != SQLITE_OK) {
    std::filesystem::remove(path);
    throw std::runtime_error("historian backup failed: " + finishMessage);
  }

  std::string verifyDetail;
  if (!verifyDatabaseFile(path, &verifyDetail)) {
    std::filesystem::remove(path);
    throw std::runtime_error("historian backup integrity check failed: " + verifyDetail);
  }
  {
    std::lock_guard lock(mutex_);
    lastBackupAt_ = now;
    lastBackupPath_ = path;
    lastMaintenanceError_.clear();
  }
  return path;
}

std::string HistorianStore::backupNow() {
  return createOnlineBackup(nowMs());
}

void HistorianStore::restoreBackup(const std::string& backupPath, const std::string& targetDirectory) {
  std::string detail;
  if (!verifyDatabaseFile(backupPath, &detail)) {
    throw std::runtime_error("refusing historian restore from invalid backup: " + detail);
  }
  std::filesystem::create_directories(targetDirectory);
  const auto target = (std::filesystem::path(targetDirectory) / "historian.sqlite3").string();
  const auto temp = target + ".restore.tmp";
  std::filesystem::copy_file(backupPath, temp, std::filesystem::copy_options::overwrite_existing);
  if (!verifyDatabaseFile(temp, &detail)) {
    std::filesystem::remove(temp);
    throw std::runtime_error("restored historian copy failed integrity check: " + detail);
  }
  std::error_code ec;
  std::filesystem::remove(target + "-wal", ec); ec.clear();
  std::filesystem::remove(target + "-shm", ec); ec.clear();
  if (std::filesystem::exists(target)) {
    const auto previous = target + ".pre-restore." + std::to_string(nowMs());
    std::filesystem::rename(target, previous, ec);
    if (ec) { std::filesystem::remove(temp); throw std::runtime_error("historian pre-restore rename failed: " + ec.message()); }
  }
  ec.clear();
  std::filesystem::rename(temp, target, ec);
  if (ec) throw std::runtime_error("historian restore atomic replace failed: " + ec.message());
}

HistorianStorageHealth HistorianStore::storageHealth() const {
  std::lock_guard lock(mutex_);
  HistorianStorageHealth out;
  out.databaseBytes = fileSizeOrZero(databasePath_) + fileSizeOrZero(databasePath_ + "-wal");
  out.minFreeBytes = options_.minFreeSpaceMb * 1024ULL * 1024ULL;
  out.backupEnabled = options_.backupEnabled;
  out.backupHealthy = lastMaintenanceError_.empty();
  out.lastBackupAt = lastBackupAt_;
  out.lastBackupPath = lastBackupPath_;
  out.lastMaintenanceError = lastMaintenanceError_;
  std::error_code ec;
  const auto space = std::filesystem::space(directory_, ec);
  if (ec) {
    out.healthy = false;
    out.detail = "filesystem space query failed: " + ec.message();
    return out;
  }
  out.freeBytes = static_cast<std::uint64_t>(space.available);
  const bool capacityHealthy = out.freeBytes >= out.minFreeBytes;
  out.healthy = capacityHealthy && out.backupHealthy;
  if (!capacityHealthy) out.detail = "free storage below configured historian safety threshold";
  else if (!out.backupHealthy) out.detail = "historian maintenance/backup failure: " + out.lastMaintenanceError;
  else out.detail = "storage capacity and maintenance healthy";
  return out;
}

void HistorianStore::migrateLegacyJsonlLocked() {
  Statement count(db_, "SELECT (SELECT COUNT(*) FROM telemetry)+(SELECT COUNT(*) FROM alarm_events);");
  if (sqlite3_step(count.get()) != SQLITE_ROW || sqlite3_column_int64(count.get(), 0) != 0) return;
  const auto migrate = [&](const std::string& path, bool telemetry) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      try {
        auto value = boost::json::parse(line);
        if (!value.is_object()) continue;
        if (telemetry) insertTelemetryLocked(value.as_object());
        else insertAlarmEventLocked(value.as_object());
      } catch (...) {
        // Preserve the original JSONL for forensic review. Invalid legacy rows
        // are skipped rather than making the new storage engine unstartable.
      }
    }
    in.close();
    std::error_code ec;
    std::filesystem::rename(path, path + ".migrated", ec);
  };
  migrate((std::filesystem::path(directory_) / "telemetry.jsonl").string(), true);
  migrate((std::filesystem::path(directory_) / "alarm-events.jsonl").string(), false);
}

}  // namespace smart_factory
