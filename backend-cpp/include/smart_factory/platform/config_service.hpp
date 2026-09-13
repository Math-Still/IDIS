#pragma once

#include "smart_factory/device_registry.hpp"

#include <boost/json.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace smart_factory {

class ConfigService {
 public:
  ConfigService(std::string directory, std::string siteName, std::shared_ptr<const DeviceRegistry> registry);
  ~ConfigService();
  ConfigService(const ConfigService&) = delete;
  ConfigService& operator=(const ConfigService&) = delete;

  boost::json::object status() const;
  boost::json::array assets() const;
  boost::json::array applicationInstances() const;
  boost::json::object activeSnapshot() const;
  boost::json::object publishedSnapshot() const;
  boost::json::object snapshotForRevision(const std::string& revision) const;
  boost::json::array events(std::int64_t fromTs = 0, std::int64_t toTs = INT64_MAX) const;

  boost::json::object createDraft(const std::string& baseRevision, const std::optional<boost::json::object>& snapshot,
                                  const std::string& actorId);
  boost::json::object validateDraft(const std::string& draftId) const;
  boost::json::object publishDraft(const std::string& draftId, const std::string& actorId, const std::string& reason);
  boost::json::object rollback(const std::string& revision, const std::string& actorId, const std::string& reason);

  std::string databasePath() const { return databasePath_; }

 private:
  struct ValidationResult {
    boost::json::array errors;
    boost::json::array warnings;
    bool requiresRestart{false};
    boost::json::array affectedObjects;
  };

  void initializeLocked();
  boost::json::object makeInitialSnapshot() const;
  ValidationResult validateSnapshotLocked(const boost::json::object& snapshot, const boost::json::object* runningSnapshot) const;
  boost::json::object revisionSnapshotLocked(const std::string& revision) const;
  boost::json::object draftSnapshotLocked(const std::string& draftId, std::string* baseRevision = nullptr) const;
  std::string stateLocked(const char* key) const;
  void setStateLocked(const char* key, const std::string& value);
  std::string nextRevisionLocked() const;
  std::string nextDraftIdLocked() const;
  void appendOutboxLocked(const std::string& type, const std::string& objectId, const std::string& revision,
                          const boost::json::object& payload);
  static std::int64_t nowMs();
  static std::string digest(const boost::json::value& value);

  std::string directory_;
  std::string databasePath_;
  std::string siteName_;
  std::shared_ptr<const DeviceRegistry> registry_;
  mutable std::mutex mutex_;
  sqlite3* db_{nullptr};
};

}  // namespace smart_factory
