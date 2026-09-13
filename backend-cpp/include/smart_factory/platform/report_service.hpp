#pragma once
#include <boost/json.hpp>
#include <mutex>
#include <string>
struct sqlite3;
namespace smart_factory {
class ReportService {
 public:
  explicit ReportService(std::string directory); ~ReportService();
  boost::json::object saveCompleted(const boost::json::object& parameters,const boost::json::object& summary,const std::string& html,const std::string& csv);
  boost::json::object get(const std::string& jobId) const;
  static std::string csvCell(std::string value);
 private:
  void init(); static std::int64_t nowMs(); std::string directory_,path_; mutable std::mutex mutex_; sqlite3* db_{nullptr};
};
}
