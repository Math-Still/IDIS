#pragma once

#include <boost/json.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace smart_factory {

enum class UserRole { Observer, Operator, Engineer, Administrator };

enum class Permission {
  View,
  AlarmAcknowledge,
  CommandIssue,
  CommandEmergency,
  CommandReconcile,
  AuditView,
  RuntimeInspect,
  ConfigEdit,
  ConfigPublish,
  RuleManage,
  IncidentAssign,
  IncidentResolve,
  IncidentClose,
  ReportExport
};

struct SecurityPrincipal {
  std::string id;
  std::string displayName;
  UserRole role{UserRole::Observer};
  bool authenticated{false};
};

struct SecurityContext {
  SecurityPrincipal principal;
  std::string remoteAddress;
};

struct AuthenticatedUser {
  SecurityPrincipal principal;
  std::string tokenSha256;
  bool enabled{true};
};

class AuthService {
 public:
  AuthService(bool enabled, std::string usersFile);

  bool enabled() const { return enabled_; }
  std::optional<SecurityPrincipal> authenticate(const std::string& userId, const std::string& token) const;
  std::optional<SecurityPrincipal> authenticateToken(const std::string& token) const;
  bool authorize(const SecurityPrincipal& principal, Permission permission) const;
  boost::json::object principalJson(const SecurityPrincipal& principal) const;
  static std::string roleName(UserRole role);
  static std::optional<UserRole> parseRole(const std::string& role);
  static std::string sha256Hex(const std::string& text);

 private:
  void loadUsers(const std::string& usersFile);
  bool enabled_{false};
  std::unordered_map<std::string, AuthenticatedUser> byId_;
  std::unordered_map<std::string, std::string> tokenHashToId_;
};

struct AuditQuery {
  std::string actorId;
  std::string action;
  std::string resourceType;
  std::int64_t fromTs{0};
  std::int64_t toTs{INT64_MAX};
  std::size_t limit{1000};
};

class AuditStore {
 public:
  AuditStore(std::string directory, std::size_t maxRecords);
  ~AuditStore();
  AuditStore(const AuditStore&) = delete;
  AuditStore& operator=(const AuditStore&) = delete;

  void append(boost::json::object event);
  boost::json::object query(const AuditQuery& query) const;
  std::size_t recordCount() const;
  std::string directory() const { return directory_; }
  std::string databasePath() const { return databasePath_; }

 private:
  static std::int64_t intField(const boost::json::object& obj, const char* key, std::int64_t fallback = 0);
  static std::string stringField(const boost::json::object& obj, const char* key);
  void initializeLocked();
  void migrateLegacyJsonlLocked();
  void insertLocked(const boost::json::object& event);
  void pruneLocked();

  std::string directory_;
  std::string databasePath_;
  std::size_t maxRecords_;
  mutable std::mutex mutex_;
  sqlite3* db_{nullptr};
};
}  // namespace smart_factory
