#pragma once

#include <boost/json.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace smart_factory {

struct PersistedCommand {
  std::string commandId;
  std::string fingerprint;
  boost::json::object command;
};

struct CommandDedupRecord {
  std::string idempotencyKey;
  std::string requestHash;
  std::string commandId;
  std::int64_t createdAt{0};
  std::int64_t expiresAt{0};
};

struct CommandReconciliationRecord {
  std::string reconciliationId;
  std::string commandId;
  std::string actorId;
  std::string conclusion;
  std::string evidence;
  bool allowRetry{false};
  std::int64_t createdAt{0};
};

enum class CommandInsertOutcome { Inserted, Duplicate, DedupConflict, CapacityExhausted };

class CommandLedger {
 public:
  CommandLedger(std::string directory, std::size_t maxRecords);
  ~CommandLedger();
  CommandLedger(const CommandLedger&) = delete;
  CommandLedger& operator=(const CommandLedger&) = delete;

  const std::string& directory() const noexcept { return directory_; }
  const std::string& databasePath() const noexcept { return databasePath_; }

  std::optional<PersistedCommand> find(const std::string& commandId) const;
  bool insert(const std::string& commandId, const std::string& fingerprint,
              const boost::json::object& command);
  CommandInsertOutcome insertWithDedup(const std::string& commandId,
                                       const std::string& fingerprint,
                                       const boost::json::object& command,
                                       const std::string& idempotencyKey,
                                       const std::string& requestHash,
                                       std::int64_t dedupExpiresAt);
  std::optional<CommandDedupRecord> findDedup(const std::string& idempotencyKey,
                                              std::int64_t now) const;
  void appendReconciliation(const CommandReconciliationRecord& record);
  std::vector<CommandReconciliationRecord> reconciliations(const std::string& commandId) const;
  std::optional<std::string> blockingUnknownFor(const std::string& deviceId, const std::string& action) const;
  void update(const boost::json::object& command);
  std::vector<PersistedCommand> recent(std::size_t limit) const;
  std::vector<PersistedCommand> nonTerminal() const;
  std::size_t recordCount() const;

 private:
  static bool terminalState(const std::string& state);
  void initialize();
  void pruneTerminalLocked();

  std::string directory_;
  std::string databasePath_;
  std::size_t maxRecords_{0};
  mutable std::mutex mutex_;
  sqlite3* db_{nullptr};
};

}  // namespace smart_factory
