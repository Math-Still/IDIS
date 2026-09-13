#pragma once

#include "smart_factory/historian.hpp"
#include "smart_factory/platform/config_service.hpp"
#include <boost/json.hpp>
#include <mutex>
#include <string>

struct sqlite3;

namespace smart_factory {

class ReplayService {
 public:
  ReplayService(std::string directory, HistorianStore* historian, ConfigService* config);
  ~ReplayService();
  boost::json::object create(const std::string& instanceId, std::int64_t fromTs, std::int64_t toTs, std::size_t limit, const std::string& configRevision = {});
  boost::json::object get(const std::string& sessionId) const;
 private:
  void init();
  static std::int64_t nowMs();
  std::string directory_, path_;
  HistorianStore* historian_{nullptr}; ConfigService* config_{nullptr};
  mutable std::mutex mutex_; sqlite3* db_{nullptr};
};

}  // namespace smart_factory
