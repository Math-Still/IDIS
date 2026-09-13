#pragma once

#include "smart_factory/security.hpp"
#include <boost/json.hpp>
#include <mutex>
#include <string>

struct sqlite3;

namespace smart_factory {

class AlarmService {
 public:
  explicit AlarmService(std::string directory);
  ~AlarmService();
  AlarmService(const AlarmService&)=delete; AlarmService& operator=(const AlarmService&)=delete;

  boost::json::object observe(const boost::json::object& conditionEvent);
  boost::json::object syncLegacyAlarm(const boost::json::object& alarm);
  boost::json::object acknowledge(const std::string& occurrenceId, const SecurityContext& actor, const std::string& comment);
  boost::json::array occurrences(const std::string& deviceId={}, const std::string& sourceDomain={}) const;
  boost::json::array currentRuleProjections() const;
  boost::json::array events(std::int64_t fromTs=0, std::int64_t toTs=INT64_MAX) const;
  std::optional<boost::json::object> occurrence(const std::string& occurrenceId) const;

 private:
  void initializeLocked();
  static std::int64_t nowMs();
  static std::string stringField(const boost::json::object& obj,const char* key,const std::string& fallback={});
  static std::int64_t intField(const boost::json::object& obj,const char* key,std::int64_t fallback=0);
  boost::json::object upsertConditionLocked(const boost::json::object& event);
  void appendEventAndOutboxLocked(const boost::json::object& occurrence,const std::string& eventType,const std::string& actor={},const std::string& comment={});
  boost::json::object readOccurrenceLocked(const std::string& occurrenceId) const;
  std::string latestOpenOccurrenceIdLocked(const std::string& alarmId) const;

  std::string directory_,databasePath_;
  mutable std::mutex mutex_;
  sqlite3* db_{nullptr};
};

}  // namespace smart_factory
