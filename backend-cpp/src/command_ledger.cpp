#include "smart_factory/command_ledger.hpp"

#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>
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
    std::string message = error ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

std::int64_t intField(const boost::json::object& obj, const char* key, std::int64_t fallback = 0) {
  const auto* v = obj.if_contains(key);
  if (!v) return fallback;
  if (v->is_int64()) return v->as_int64();
  if (v->is_uint64()) return static_cast<std::int64_t>(v->as_uint64());
  return fallback;
}

std::string stringField(const boost::json::object& obj, const char* key, std::string fallback = {}) {
  const auto* v = obj.if_contains(key);
  return v && v->is_string() ? std::string(v->as_string()) : std::move(fallback);
}

PersistedCommand rowToCommand(sqlite3_stmt* stmt) {
  PersistedCommand out;
  out.commandId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  out.fingerprint = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  const auto* jsonText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
  if (!jsonText) throw std::runtime_error("command ledger row has no JSON payload");
  auto parsed = boost::json::parse(jsonText);
  if (!parsed.is_object()) throw std::runtime_error("command ledger JSON payload is not an object");
  out.command = parsed.as_object();
  return out;
}

}  // namespace

CommandLedger::CommandLedger(std::string directory, std::size_t maxRecords)
    : directory_(std::move(directory)), maxRecords_(maxRecords) {
  if (directory_.empty()) throw std::runtime_error("command ledger directory is empty");
  std::filesystem::create_directories(directory_);
  databasePath_ = (std::filesystem::path(directory_) / "commands.sqlite3").string();
  if (sqlite3_open_v2(databasePath_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown sqlite open error";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("cannot open command ledger: " + message);
  }
  initialize();
}

CommandLedger::~CommandLedger() {
  std::lock_guard lock(mutex_);
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

void CommandLedger::initialize() {
  std::lock_guard lock(mutex_);
  exec(db_, "PRAGMA journal_mode=WAL;");
  exec(db_, "PRAGMA synchronous=FULL;");
  exec(db_, "PRAGMA foreign_keys=ON;");
  exec(db_, "PRAGMA busy_timeout=3000;");
  exec(db_, R"SQL(
    CREATE TABLE IF NOT EXISTS commands (
      command_id TEXT PRIMARY KEY,
      fingerprint TEXT NOT NULL,
      state TEXT NOT NULL,
      issued_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      command_json TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_commands_updated_at ON commands(updated_at DESC);
    CREATE INDEX IF NOT EXISTS idx_commands_state ON commands(state);
    CREATE TABLE IF NOT EXISTS command_dedup (
      idempotency_key TEXT PRIMARY KEY,
      request_hash TEXT NOT NULL,
      command_id TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      expires_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_command_dedup_expires ON command_dedup(expires_at);
    CREATE TABLE IF NOT EXISTS command_reconciliations (
      reconciliation_id TEXT PRIMARY KEY,
      command_id TEXT NOT NULL,
      actor_id TEXT NOT NULL,
      conclusion TEXT NOT NULL,
      evidence TEXT NOT NULL,
      allow_retry INTEGER NOT NULL DEFAULT 0,
      created_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_command_reconciliations_command ON command_reconciliations(command_id,created_at);
  )SQL");
}

bool CommandLedger::terminalState(const std::string& state) {
  return state == "CONFIRMED" || state == "FAILED" || state == "REJECTED" || state == "EXPIRED" ||
         state == "OUTCOME_UNKNOWN";
}

std::optional<PersistedCommand> CommandLedger::find(const std::string& commandId) const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT command_id,fingerprint,command_json FROM commands WHERE command_id=?1;");
  sqlite3_bind_text(stmt.get(), 1, commandId.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) return std::nullopt;
  if (rc != SQLITE_ROW) throw std::runtime_error(std::string("sqlite command lookup failed: ") + sqlite3_errmsg(db_));
  return rowToCommand(stmt.get());
}

bool CommandLedger::insert(const std::string& commandId, const std::string& fingerprint,
                           const boost::json::object& command) {
  const auto state = stringField(command, "state", "ISSUED");
  const auto issuedAt = intField(command, "issuedAt");
  const auto updatedAt = intField(command, "updatedAt", issuedAt);
  const auto payload = boost::json::serialize(command);
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "INSERT OR IGNORE INTO commands(command_id,fingerprint,state,issued_at,updated_at,command_json) VALUES(?1,?2,?3,?4,?5,?6);");
  sqlite3_bind_text(stmt.get(), 1, commandId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 3, state.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt.get(), 4, issuedAt);
  sqlite3_bind_int64(stmt.get(), 5, updatedAt);
  sqlite3_bind_text(stmt.get(), 6, payload.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
    throw std::runtime_error(std::string("sqlite command insert failed: ") + sqlite3_errmsg(db_));
  }
  const bool inserted = sqlite3_changes(db_) == 1;
  if (inserted) pruneTerminalLocked();
  return inserted;
}


CommandInsertOutcome CommandLedger::insertWithDedup(const std::string& commandId,
                                                     const std::string& fingerprint,
                                                     const boost::json::object& command,
                                                     const std::string& idempotencyKey,
                                                     const std::string& requestHash,
                                                     std::int64_t dedupExpiresAt) {
  if (idempotencyKey.empty()) throw std::runtime_error("idempotency key is empty");
  const auto state = stringField(command, "state", "ISSUED");
  const auto issuedAt = intField(command, "issuedAt");
  const auto updatedAt = intField(command, "updatedAt", issuedAt);
  const auto payload = boost::json::serialize(command);
  std::lock_guard lock(mutex_);

  exec(db_, "BEGIN IMMEDIATE;");
  try {
    {
      Statement cleanup(db_, "DELETE FROM command_dedup WHERE expires_at<=?1;");
      sqlite3_bind_int64(cleanup.get(), 1, issuedAt);
      if (sqlite3_step(cleanup.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    {
      Statement existing(db_, "SELECT request_hash,command_id FROM command_dedup WHERE idempotency_key=?1;");
      sqlite3_bind_text(existing.get(), 1, idempotencyKey.c_str(), -1, SQLITE_TRANSIENT);
      const int rc = sqlite3_step(existing.get());
      if (rc == SQLITE_ROW) {
        const auto* hashText = reinterpret_cast<const char*>(sqlite3_column_text(existing.get(), 0));
        const auto* commandText = reinterpret_cast<const char*>(sqlite3_column_text(existing.get(), 1));
        const std::string existingHash = hashText ? hashText : "";
        const std::string existingCommand = commandText ? commandText : "";
        exec(db_, "COMMIT;");
        if (existingHash != requestHash) return CommandInsertOutcome::DedupConflict;
        return CommandInsertOutcome::Duplicate;
      }
      if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }

    if (maxRecords_ > 0) {
      Statement countStmt(db_, "SELECT COUNT(*) FROM commands;");
      if (sqlite3_step(countStmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
      auto count = static_cast<std::size_t>(sqlite3_column_int64(countStmt.get(), 0));
      if (count >= maxRecords_) {
        Statement pruneOne(db_, R"SQL(
          DELETE FROM commands WHERE command_id IN (
            SELECT command_id FROM commands
            WHERE state IN ('CONFIRMED','FAILED','REJECTED','EXPIRED')
            ORDER BY updated_at ASC LIMIT 1
          );
        )SQL");
        if (sqlite3_step(pruneOne.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
        if (sqlite3_changes(db_) == 0) {
          exec(db_, "ROLLBACK;");
          return CommandInsertOutcome::CapacityExhausted;
        }
      }
    }

    {
      Statement commandStmt(db_, "INSERT INTO commands(command_id,fingerprint,state,issued_at,updated_at,command_json) VALUES(?1,?2,?3,?4,?5,?6);");
      sqlite3_bind_text(commandStmt.get(), 1, commandId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(commandStmt.get(), 2, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(commandStmt.get(), 3, state.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(commandStmt.get(), 4, issuedAt);
      sqlite3_bind_int64(commandStmt.get(), 5, updatedAt);
      sqlite3_bind_text(commandStmt.get(), 6, payload.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(commandStmt.get()) != SQLITE_DONE) {
        if (sqlite3_extended_errcode(db_) == SQLITE_CONSTRAINT_PRIMARYKEY || sqlite3_extended_errcode(db_) == SQLITE_CONSTRAINT_UNIQUE) {
          exec(db_, "ROLLBACK;");
          return CommandInsertOutcome::Duplicate;
        }
        throw std::runtime_error(sqlite3_errmsg(db_));
      }
    }
    {
      Statement dedupStmt(db_, "INSERT INTO command_dedup(idempotency_key,request_hash,command_id,created_at,expires_at) VALUES(?1,?2,?3,?4,?5);");
      sqlite3_bind_text(dedupStmt.get(), 1, idempotencyKey.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(dedupStmt.get(), 2, requestHash.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(dedupStmt.get(), 3, commandId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(dedupStmt.get(), 4, issuedAt);
      sqlite3_bind_int64(dedupStmt.get(), 5, dedupExpiresAt);
      if (sqlite3_step(dedupStmt.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    exec(db_, "COMMIT;");
    return CommandInsertOutcome::Inserted;
  } catch (...) {
    try { exec(db_, "ROLLBACK;"); } catch (...) {}
    throw;
  }
}

std::optional<CommandDedupRecord> CommandLedger::findDedup(const std::string& idempotencyKey,
                                                            std::int64_t now) const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT idempotency_key,request_hash,command_id,created_at,expires_at FROM command_dedup WHERE idempotency_key=?1 AND expires_at>?2;");
  sqlite3_bind_text(stmt.get(), 1, idempotencyKey.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt.get(), 2, now);
  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) return std::nullopt;
  if (rc != SQLITE_ROW) throw std::runtime_error(std::string("sqlite command dedup lookup failed: ") + sqlite3_errmsg(db_));
  CommandDedupRecord record;
  record.idempotencyKey = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
  record.requestHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
  record.commandId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
  record.createdAt = sqlite3_column_int64(stmt.get(), 3);
  record.expiresAt = sqlite3_column_int64(stmt.get(), 4);
  return record;
}

void CommandLedger::appendReconciliation(const CommandReconciliationRecord& record) {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "INSERT INTO command_reconciliations(reconciliation_id,command_id,actor_id,conclusion,evidence,allow_retry,created_at) VALUES(?1,?2,?3,?4,?5,?6,?7);");
  sqlite3_bind_text(stmt.get(), 1, record.reconciliationId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, record.commandId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 3, record.actorId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 4, record.conclusion.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 5, record.evidence.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt.get(), 6, record.allowRetry ? 1 : 0);
  sqlite3_bind_int64(stmt.get(), 7, record.createdAt);
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw std::runtime_error(std::string("sqlite command reconciliation insert failed: ") + sqlite3_errmsg(db_));
}

std::vector<CommandReconciliationRecord> CommandLedger::reconciliations(const std::string& commandId) const {
  std::vector<CommandReconciliationRecord> out;
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT reconciliation_id,command_id,actor_id,conclusion,evidence,allow_retry,created_at FROM command_reconciliations WHERE command_id=?1 ORDER BY created_at ASC;");
  sqlite3_bind_text(stmt.get(), 1, commandId.c_str(), -1, SQLITE_TRANSIENT);
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(std::string("sqlite reconciliation query failed: ") + sqlite3_errmsg(db_));
    CommandReconciliationRecord r;
    r.reconciliationId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(),0));
    r.commandId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(),1));
    r.actorId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(),2));
    r.conclusion = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(),3));
    r.evidence = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(),4));
    r.allowRetry = sqlite3_column_int(stmt.get(),5) != 0;
    r.createdAt = sqlite3_column_int64(stmt.get(),6);
    out.push_back(std::move(r));
  }
  return out;
}


std::optional<std::string> CommandLedger::blockingUnknownFor(const std::string& deviceId,
                                                              const std::string& action) const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT command_id,command_json FROM commands WHERE state='OUTCOME_UNKNOWN' ORDER BY updated_at DESC;");
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    const auto* idText = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    const auto* jsonText = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
    const std::string commandId = idText ? idText : "";
    if (!jsonText || commandId.empty()) continue;
    boost::json::error_code ec;
    auto parsed = boost::json::parse(jsonText, ec);
    if (ec || !parsed.is_object()) continue;
    const auto& command = parsed.as_object();
    if (stringField(command,"deviceId") != deviceId || stringField(command,"action") != action) continue;
    Statement recon(db_, "SELECT allow_retry FROM command_reconciliations WHERE command_id=?1 ORDER BY created_at DESC LIMIT 1;");
    sqlite3_bind_text(recon.get(),1,commandId.c_str(),-1,SQLITE_TRANSIENT);
    const auto rc=sqlite3_step(recon.get());
    if (rc==SQLITE_ROW && sqlite3_column_int(recon.get(),0)!=0) continue;
    return commandId;
  }
  return std::nullopt;
}

void CommandLedger::update(const boost::json::object& command) {
  const auto commandId = stringField(command, "id");
  if (commandId.empty()) throw std::runtime_error("cannot update command ledger row without id");
  const auto state = stringField(command, "state", "UNKNOWN");
  const auto updatedAt = intField(command, "updatedAt");
  const auto payload = boost::json::serialize(command);
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "UPDATE commands SET state=?1,updated_at=?2,command_json=?3 WHERE command_id=?4;");
  sqlite3_bind_text(stmt.get(), 1, state.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt.get(), 2, updatedAt);
  sqlite3_bind_text(stmt.get(), 3, payload.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 4, commandId.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
    throw std::runtime_error(std::string("sqlite command update failed: ") + sqlite3_errmsg(db_));
  }
  if (sqlite3_changes(db_) != 1) throw std::runtime_error("command ledger update targeted a missing command: " + commandId);
}

std::vector<PersistedCommand> CommandLedger::recent(std::size_t limit) const {
  std::vector<PersistedCommand> out;
  if (limit == 0) return out;
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT command_id,fingerprint,command_json FROM commands ORDER BY updated_at DESC LIMIT ?1;");
  sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(limit));
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(std::string("sqlite recent command query failed: ") + sqlite3_errmsg(db_));
    out.push_back(rowToCommand(stmt.get()));
  }
  return out;
}

std::vector<PersistedCommand> CommandLedger::nonTerminal() const {
  std::vector<PersistedCommand> out;
  std::lock_guard lock(mutex_);
  Statement stmt(db_, R"SQL(
    SELECT command_id,fingerprint,command_json FROM commands
    WHERE state NOT IN ('CONFIRMED','FAILED','REJECTED','EXPIRED','OUTCOME_UNKNOWN')
    ORDER BY updated_at ASC;
  )SQL");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) throw std::runtime_error(std::string("sqlite non-terminal command query failed: ") + sqlite3_errmsg(db_));
    out.push_back(rowToCommand(stmt.get()));
  }
  return out;
}

std::size_t CommandLedger::recordCount() const {
  std::lock_guard lock(mutex_);
  Statement stmt(db_, "SELECT COUNT(*) FROM commands;");
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(std::string("sqlite command count failed: ") + sqlite3_errmsg(db_));
  return static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
}

void CommandLedger::pruneTerminalLocked() {
  if (maxRecords_ == 0) return;
  Statement countStmt(db_, "SELECT COUNT(*) FROM commands;");
  if (sqlite3_step(countStmt.get()) != SQLITE_ROW) return;
  const auto count = static_cast<std::size_t>(sqlite3_column_int64(countStmt.get(), 0));
  if (count <= maxRecords_) return;
  const auto excess = count - maxRecords_;
  Statement deleteStmt(db_, R"SQL(
    DELETE FROM commands WHERE command_id IN (
      SELECT command_id FROM commands
      WHERE state IN ('CONFIRMED','FAILED','REJECTED','EXPIRED')
      ORDER BY updated_at ASC LIMIT ?1
    );
  )SQL");
  sqlite3_bind_int64(deleteStmt.get(), 1, static_cast<sqlite3_int64>(excess));
  if (sqlite3_step(deleteStmt.get()) != SQLITE_DONE) {
    throw std::runtime_error(std::string("sqlite command prune failed: ") + sqlite3_errmsg(db_));
  }
}

}  // namespace smart_factory
