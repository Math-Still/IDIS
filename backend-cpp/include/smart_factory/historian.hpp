#pragma once

#include <boost/json.hpp>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>

struct sqlite3;

namespace smart_factory {

struct TelemetryHistoryQuery {
  std::string deviceId;
  std::string pointId;
  std::int64_t fromTs{0};
  std::int64_t toTs{INT64_MAX};
  std::size_t limit{2000};
};

struct AlarmHistoryQuery {
  std::string deviceId;
  std::string alarmId;
  std::int64_t fromTs{0};
  std::int64_t toTs{INT64_MAX};
  std::size_t limit{2000};
};

struct TelemetryCursorRecord {
  std::int64_t sampleTs{0};
  std::int64_t seq{-1};
  std::string sourceEpoch;
};

struct HistorianStorageHealth {
  bool healthy{true};
  std::uint64_t databaseBytes{0};
  std::uint64_t freeBytes{0};
  std::uint64_t minFreeBytes{0};
  bool backupEnabled{false};
  bool backupHealthy{true};
  std::int64_t lastBackupAt{0};
  std::string lastBackupPath;
  std::string lastMaintenanceError;
  std::string detail;
};

struct HistorianOptions {
  int retentionDays{30};
  bool backupEnabled{true};
  std::string backupDirectory{"data/backup/historian"};
  int backupIntervalHours{24};
  std::uint64_t minFreeSpaceMb{512};
};

// SQLite/WAL-backed historian. Telemetry/event retention is time based;
// alarm_current is intentionally outside retention so an ACTIVE/ACK state can
// always be recovered even if the originating event is older than retention.
class HistorianStore {
 public:
  HistorianStore(std::string directory, HistorianOptions options = {});
  ~HistorianStore();
  HistorianStore(const HistorianStore&) = delete;
  HistorianStore& operator=(const HistorianStore&) = delete;

  void appendTelemetry(const boost::json::object& point);
  void appendAlarmEvent(const boost::json::object& event);

  boost::json::object queryTelemetry(const TelemetryHistoryQuery& query) const;
  boost::json::object queryAlarmEvents(const AlarmHistoryQuery& query) const;

  std::unordered_map<std::string, boost::json::object> replayAlarmStates() const;
  std::unordered_map<std::string, TelemetryCursorRecord> latestTelemetryCursors() const;

  std::string directory() const { return directory_; }
  std::string databasePath() const { return databasePath_; }
  std::size_t telemetryRecordCount() const;
  std::size_t alarmEventCount() const;
  std::size_t alarmOccurrenceCount() const;

  void pruneExpired();
  std::string backupNow();
  HistorianStorageHealth storageHealth() const;

  static bool verifyDatabaseFile(const std::string& path, std::string* detail = nullptr);
  // Offline maintenance only: the backend must not have the target historian
  // open when this function is called.
  static void restoreBackup(const std::string& backupPath, const std::string& targetDirectory);

 private:
  static std::int64_t intField(const boost::json::object& obj, const char* key, std::int64_t fallback = 0);
  static std::string stringField(const boost::json::object& obj, const char* key);

  void initializeLocked();
  void ensureTelemetryEpochSchemaLocked();
  void seedTelemetryCursorsLocked();
  void migrateLegacyJsonlLocked();
  void insertTelemetryLocked(const boost::json::object& point);
  void insertAlarmEventLocked(const boost::json::object& event);
  void pruneExpiredLocked(std::int64_t now);
  void maintenanceLoop();
  std::string createOnlineBackup(std::int64_t now);
  void recoverLastBackupState();
  static bool verifyDatabaseHandle(sqlite3* db, std::string* detail);

  std::string directory_;
  std::string databasePath_;
  HistorianOptions options_;
  mutable std::mutex mutex_;
  sqlite3* db_{nullptr};
  std::int64_t lastMaintenanceAt_{0};
  std::int64_t lastBackupAt_{0};
  std::string lastBackupPath_;
  std::string lastMaintenanceError_;
  std::atomic<bool> maintenanceRunning_{false};
  std::thread maintenanceThread_;
  std::mutex maintenanceWaitMutex_;
  std::condition_variable maintenanceCv_;
};

}  // namespace smart_factory
