#include "smart_factory/security.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace smart_factory {
namespace {
constexpr std::array<std::uint32_t, 64> kSha256 = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
std::string upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
  return value;
}
std::size_t boundedLimit(std::size_t limit) { return std::max<std::size_t>(1, std::min<std::size_t>(limit, 10000)); }
}

std::string AuthService::sha256Hex(const std::string& text) {
  std::vector<std::uint8_t> bytes(text.begin(), text.end());
  const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;
  bytes.push_back(0x80);
  while ((bytes.size() % 64) != 56) bytes.push_back(0);
  for (int i = 7; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>((bitLength >> (i * 8)) & 0xffu));

  std::array<std::uint32_t, 8> h = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
      const auto j = offset + static_cast<std::size_t>(i * 4);
      w[i] = (static_cast<std::uint32_t>(bytes[j]) << 24) | (static_cast<std::uint32_t>(bytes[j+1]) << 16) |
             (static_cast<std::uint32_t>(bytes[j+2]) << 8) | static_cast<std::uint32_t>(bytes[j+3]);
    }
    for (int i = 16; i < 64; ++i) {
      const auto s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
      const auto s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i=0;i<64;++i) {
      const auto S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
      const auto ch = (e & f) ^ ((~e) & g);
      const auto temp1 = hh + S1 + ch + kSha256[i] + w[i];
      const auto S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
      const auto maj = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = S0 + maj;
      hh=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const auto value : h) out << std::setw(8) << value;
  return out.str();
}

AuthService::AuthService(bool enabled, std::string usersFile) : enabled_(enabled) {
  if (enabled_) loadUsers(usersFile);
}

void AuthService::loadUsers(const std::string& usersFile) {
  if (usersFile.empty()) throw std::runtime_error("auth_users_file is required when auth is enabled");
  std::ifstream in(usersFile);
  if (!in) throw std::runtime_error("Cannot open auth users file: " + usersFile);
  std::stringstream buffer; buffer << in.rdbuf();
  const auto parsed = boost::json::parse(buffer.str());
  if (!parsed.is_object()) throw std::runtime_error("Auth users file must contain a JSON object");
  const auto* users = parsed.as_object().if_contains("users");
  if (!users || !users->is_array()) throw std::runtime_error("Auth users file must contain users[]");
  for (const auto& value : users->as_array()) {
    if (!value.is_object()) continue;
    const auto& obj = value.as_object();
    const auto* id = obj.if_contains("id");
    const auto* displayName = obj.if_contains("displayName");
    const auto* roleValue = obj.if_contains("role");
    const auto* hashValue = obj.if_contains("tokenSha256");
    if (!id || !id->is_string() || !displayName || !displayName->is_string() || !roleValue || !roleValue->is_string() || !hashValue || !hashValue->is_string()) {
      throw std::runtime_error("Invalid auth user record");
    }
    const auto role = parseRole(std::string(roleValue->as_string()));
    if (!role) throw std::runtime_error("Invalid auth role for user " + std::string(id->as_string()));
    AuthenticatedUser user;
    user.principal = {std::string(id->as_string()), std::string(displayName->as_string()), *role, true};
    user.tokenSha256 = std::string(hashValue->as_string());
    if (const auto* enabled = obj.if_contains("enabled"); enabled && enabled->is_bool()) user.enabled = enabled->as_bool();
    if (user.principal.id.empty() || user.tokenSha256.size() != 64) throw std::runtime_error("Invalid auth user id/token hash");
    if (byId_.contains(user.principal.id) || tokenHashToId_.contains(user.tokenSha256)) throw std::runtime_error("Duplicate auth user or token hash");
    tokenHashToId_[user.tokenSha256] = user.principal.id;
    byId_[user.principal.id] = std::move(user);
  }
  if (byId_.empty()) throw std::runtime_error("Auth is enabled but no users are configured");
}

std::optional<SecurityPrincipal> AuthService::authenticate(const std::string& userId, const std::string& token) const {
  if (!enabled_) return SecurityPrincipal{"anonymous","Anonymous",UserRole::Administrator,false};
  const auto it = byId_.find(userId);
  if (it == byId_.end() || !it->second.enabled || sha256Hex(token) != it->second.tokenSha256) return std::nullopt;
  return it->second.principal;
}

std::optional<SecurityPrincipal> AuthService::authenticateToken(const std::string& token) const {
  if (!enabled_) return SecurityPrincipal{"anonymous","Anonymous",UserRole::Administrator,false};
  const auto hash = sha256Hex(token);
  const auto id = tokenHashToId_.find(hash);
  if (id == tokenHashToId_.end()) return std::nullopt;
  const auto it = byId_.find(id->second);
  if (it == byId_.end() || !it->second.enabled) return std::nullopt;
  return it->second.principal;
}

std::string AuthService::roleName(UserRole role) {
  switch (role) {
    case UserRole::Observer: return "OBSERVER";
    case UserRole::Operator: return "OPERATOR";
    case UserRole::Engineer: return "ENGINEER";
    case UserRole::Administrator: return "ADMINISTRATOR";
  }
  return "OBSERVER";
}

std::optional<UserRole> AuthService::parseRole(const std::string& role) {
  const auto r = upper(role);
  if (r == "OBSERVER") return UserRole::Observer;
  if (r == "OPERATOR") return UserRole::Operator;
  if (r == "ENGINEER") return UserRole::Engineer;
  if (r == "ADMIN" || r == "ADMINISTRATOR") return UserRole::Administrator;
  return std::nullopt;
}

bool AuthService::authorize(const SecurityPrincipal& principal, Permission permission) const {
  if (!enabled_) return true;
  if (!principal.authenticated) return false;
  const int rank = principal.role == UserRole::Observer ? 0 : principal.role == UserRole::Operator ? 1 : principal.role == UserRole::Engineer ? 2 : 3;
  switch (permission) {
    case Permission::View: return rank >= 0;
    case Permission::AlarmAcknowledge: return rank >= 1;
    case Permission::CommandIssue: return rank >= 1;
    case Permission::AuditView: return rank >= 2;
    case Permission::RuntimeInspect: return rank >= 2;
    case Permission::ConfigEdit: return rank >= 2;
    case Permission::ConfigPublish: return rank >= 2;
    case Permission::RuleManage: return rank >= 2;
    case Permission::IncidentAssign: return rank >= 1;
    case Permission::IncidentResolve: return rank >= 1;
    case Permission::IncidentClose: return rank >= 2;
    case Permission::CommandEmergency: return rank >= 3;
    case Permission::CommandReconcile: return rank >= 2;
    case Permission::ReportExport: return rank >= 1;
  }
  return false;
}

boost::json::object AuthService::principalJson(const SecurityPrincipal& principal) const {
  boost::json::array permissions;
  for (const auto& [name, permission] : std::initializer_list<std::pair<const char*, Permission>>{
      {"view",Permission::View},{"alarm.ack",Permission::AlarmAcknowledge},{"command.issue",Permission::CommandIssue},{"command.emergency",Permission::CommandEmergency},{"command.reconcile",Permission::CommandReconcile},{"audit.view",Permission::AuditView},{"runtime.inspect",Permission::RuntimeInspect},{"config.edit",Permission::ConfigEdit},{"config.publish",Permission::ConfigPublish},{"rule.manage",Permission::RuleManage},{"incident.assign",Permission::IncidentAssign},{"incident.resolve",Permission::IncidentResolve},{"incident.close",Permission::IncidentClose},{"report.export",Permission::ReportExport}}) {
    if (authorize(principal, permission)) permissions.emplace_back(name);
  }
  return {{"id",principal.id},{"displayName",principal.displayName},{"role",roleName(principal.role)},{"authenticated",principal.authenticated},{"permissions",std::move(permissions)}};
}

AuditStore::AuditStore(std::string directory, std::size_t maxRecords)
    : directory_(std::move(directory)), maxRecords_(std::max<std::size_t>(1000, maxRecords)) {
  std::filesystem::create_directories(directory_);
  databasePath_ = (std::filesystem::path(directory_) / "audit.sqlite3").string();
  if (sqlite3_open_v2(databasePath_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown sqlite open error";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("cannot open audit store: " + message);
  }
  std::lock_guard lock(mutex_);
  initializeLocked();
  migrateLegacyJsonlLocked();
  pruneLocked();
}

AuditStore::~AuditStore() {
  std::lock_guard lock(mutex_);
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

std::int64_t AuditStore::intField(const boost::json::object& obj, const char* key, std::int64_t fallback) {
  if (const auto* v=obj.if_contains(key); v && v->is_int64()) return v->as_int64();
  return fallback;
}
std::string AuditStore::stringField(const boost::json::object& obj, const char* key) {
  if (const auto* v=obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return {};
}

void AuditStore::initializeLocked() {
  auto execSql=[this](const char* sql){
    char* error=nullptr;
    if(sqlite3_exec(db_,sql,nullptr,nullptr,&error)!=SQLITE_OK){
      std::string message=error?error:sqlite3_errmsg(db_); sqlite3_free(error);
      throw std::runtime_error("audit sqlite exec failed: "+message);
    }
  };
  execSql("PRAGMA journal_mode=WAL;");
  execSql("PRAGMA synchronous=FULL;");
  execSql("PRAGMA busy_timeout=3000;");
  execSql(R"SQL(
    CREATE TABLE IF NOT EXISTS audit_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      timestamp INTEGER NOT NULL,
      actor_id TEXT NOT NULL,
      action TEXT NOT NULL,
      resource_type TEXT NOT NULL,
      event_json TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON audit_events(timestamp);
    CREATE INDEX IF NOT EXISTS idx_audit_actor_time ON audit_events(actor_id,timestamp);
    CREATE INDEX IF NOT EXISTS idx_audit_action_time ON audit_events(action,timestamp);
  )SQL");
}

void AuditStore::insertLocked(const boost::json::object& event) {
  const auto payload=boost::json::serialize(event);
  sqlite3_stmt* stmt=nullptr;
  const char* sql="INSERT INTO audit_events(timestamp,actor_id,action,resource_type,event_json) VALUES(?1,?2,?3,?4,?5);";
  if(sqlite3_prepare_v2(db_,sql,-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  const auto actor=stringField(event,"actorId"), action=stringField(event,"action"), resource=stringField(event,"resourceType");
  sqlite3_bind_int64(stmt,1,intField(event,"timestamp"));
  sqlite3_bind_text(stmt,2,actor.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,3,action.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,4,resource.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,5,payload.c_str(),-1,SQLITE_TRANSIENT);
  const int rc=sqlite3_step(stmt); sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) throw std::runtime_error(std::string("audit sqlite insert failed: ")+sqlite3_errmsg(db_));
}

void AuditStore::pruneLocked() {
  sqlite3_stmt* count=nullptr;
  if(sqlite3_prepare_v2(db_,"SELECT COUNT(*) FROM audit_events;",-1,&count,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  const int rc=sqlite3_step(count);
  const auto n=rc==SQLITE_ROW?static_cast<std::size_t>(sqlite3_column_int64(count,0)):0;
  sqlite3_finalize(count);
  if(n<=maxRecords_) return;
  const auto excess=n-maxRecords_;
  sqlite3_stmt* del=nullptr;
  if(sqlite3_prepare_v2(db_,"DELETE FROM audit_events WHERE id IN (SELECT id FROM audit_events ORDER BY id ASC LIMIT ?1);",-1,&del,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  sqlite3_bind_int64(del,1,static_cast<sqlite3_int64>(excess));
  const int drc=sqlite3_step(del); sqlite3_finalize(del);
  if(drc!=SQLITE_DONE) throw std::runtime_error(std::string("audit sqlite prune failed: ")+sqlite3_errmsg(db_));
}

void AuditStore::migrateLegacyJsonlLocked() {
  sqlite3_stmt* count=nullptr;
  if(sqlite3_prepare_v2(db_,"SELECT COUNT(*) FROM audit_events;",-1,&count,nullptr)!=SQLITE_OK) return;
  const int rc=sqlite3_step(count); const auto existing=rc==SQLITE_ROW?sqlite3_column_int64(count,0):1; sqlite3_finalize(count);
  if(existing!=0) return;
  const auto legacy=(std::filesystem::path(directory_)/"audit.jsonl").string();
  std::ifstream in(legacy); if(!in) return;
  std::string line;
  while(std::getline(in,line)){
    if(line.empty()) continue;
    try { auto v=boost::json::parse(line); if(v.is_object()) insertLocked(v.as_object()); } catch(...) {}
  }
  in.close();
  std::error_code ec; std::filesystem::rename(legacy,legacy+".migrated",ec);
}

void AuditStore::append(boost::json::object event) {
  std::lock_guard lock(mutex_);
  insertLocked(event);
  pruneLocked();
}

boost::json::object AuditStore::query(const AuditQuery& query) const {
  std::lock_guard lock(mutex_);
  const auto limit=boundedLimit(query.limit);
  std::string where=" WHERE timestamp>=?1 AND timestamp<=?2";
  if(!query.actorId.empty()) where+=" AND actor_id=?3";
  if(!query.action.empty()) where+=query.actorId.empty()?" AND action=?3":" AND action=?4";
  int next=3+(!query.actorId.empty()?1:0)+(!query.action.empty()?1:0);
  if(!query.resourceType.empty()) where+=" AND resource_type=?"+std::to_string(next++);

  auto bindCommon=[&](sqlite3_stmt* stmt){
    sqlite3_bind_int64(stmt,1,query.fromTs); sqlite3_bind_int64(stmt,2,query.toTs); int b=3;
    if(!query.actorId.empty()) sqlite3_bind_text(stmt,b++,query.actorId.c_str(),-1,SQLITE_TRANSIENT);
    if(!query.action.empty()) sqlite3_bind_text(stmt,b++,query.action.c_str(),-1,SQLITE_TRANSIENT);
    if(!query.resourceType.empty()) sqlite3_bind_text(stmt,b++,query.resourceType.c_str(),-1,SQLITE_TRANSIENT);
    return b;
  };

  std::size_t total=0;
  sqlite3_stmt* count=nullptr; const auto countSql="SELECT COUNT(*) FROM audit_events"+where+";";
  if(sqlite3_prepare_v2(db_,countSql.c_str(),-1,&count,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  bindCommon(count); if(sqlite3_step(count)!=SQLITE_ROW){sqlite3_finalize(count);throw std::runtime_error(sqlite3_errmsg(db_));}
  total=static_cast<std::size_t>(sqlite3_column_int64(count,0)); sqlite3_finalize(count);

  const int limitIndex=next;
  const auto sql="SELECT event_json FROM audit_events"+where+" ORDER BY timestamp DESC,id DESC LIMIT ?"+std::to_string(limitIndex)+";";
  sqlite3_stmt* rows=nullptr; if(sqlite3_prepare_v2(db_,sql.c_str(),-1,&rows,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  const int b=bindCommon(rows); sqlite3_bind_int64(rows,b,static_cast<sqlite3_int64>(limit));
  boost::json::array matches;
  while(true){
    const int r=sqlite3_step(rows); if(r==SQLITE_DONE) break; if(r!=SQLITE_ROW){sqlite3_finalize(rows);throw std::runtime_error(sqlite3_errmsg(db_));}
    const auto* text=reinterpret_cast<const char*>(sqlite3_column_text(rows,0)); if(!text) continue;
    auto v=boost::json::parse(text); if(v.is_object()) matches.emplace_back(v.as_object());
  }
  sqlite3_finalize(rows); std::reverse(matches.begin(),matches.end());
  return {{"items",std::move(matches)},{"totalMatched",static_cast<std::int64_t>(total)},{"limit",static_cast<std::int64_t>(limit)},
          {"from",query.fromTs},{"to",query.toTs},{"truncated",total>limit}};
}

std::size_t AuditStore::recordCount() const {
  std::lock_guard lock(mutex_);
  sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db_,"SELECT COUNT(*) FROM audit_events;",-1,&stmt,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
  if(sqlite3_step(stmt)!=SQLITE_ROW){sqlite3_finalize(stmt);throw std::runtime_error(sqlite3_errmsg(db_));}
  const auto n=static_cast<std::size_t>(sqlite3_column_int64(stmt,0)); sqlite3_finalize(stmt); return n;
}

}  // namespace smart_factory
