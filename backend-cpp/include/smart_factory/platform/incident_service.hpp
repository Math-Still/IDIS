#pragma once

#include "smart_factory/security.hpp"
#include <boost/json.hpp>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace smart_factory {

class IncidentService {
 public:
  explicit IncidentService(std::string directory);
  ~IncidentService();
  IncidentService(const IncidentService&) = delete;
  IncidentService& operator=(const IncidentService&) = delete;

  boost::json::object create(const boost::json::object& request, const SecurityContext& actor);
  boost::json::object transition(const std::string& incidentId, const boost::json::object& request, const SecurityContext& actor);
  boost::json::array list() const;
  std::optional<boost::json::object> get(const std::string& id) const;
  boost::json::array events(std::int64_t fromTs = 0, std::int64_t toTs = INT64_MAX) const;

 private:
  void init();
  static std::int64_t nowMs();
  static std::string str(const boost::json::object& obj, const char* key, const std::string& fallback = {});
  static std::int64_t integer(const boost::json::object& obj, const char* key, std::int64_t fallback = 0);
  boost::json::object readLocked(const std::string& id) const;
  void appendEventLocked(const boost::json::object& incident, const std::string& type, const std::string& comment);

  std::string directory_;
  std::string path_;
  mutable std::mutex mutex_;
  sqlite3* db_{nullptr};
};

}  // namespace smart_factory
