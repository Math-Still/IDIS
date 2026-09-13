#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace smart_factory {

class RuleService {
 public:
  RuleService() = default;

  void configure(const boost::json::object& snapshot, const std::string& configRevision);
  boost::json::array rules() const;
  boost::json::array evaluate(const boost::json::array& telemetry, std::int64_t evaluationTs);

 private:
  struct Rule {
    std::string ruleId;
    std::string version;
    std::string deviceId;
    std::string pointId;
    std::string direction{"HIGH"};
    double triggerThreshold{0};
    double recoveryThreshold{0};
    std::int64_t durationMs{0};
    std::string minimumQuality{"GOOD"};
    std::string severity{"WARNING"};
    std::string title;
    std::string message;
    bool enabled{true};
  };
  struct State {
    std::string conditionState{"NORMAL"};
    std::int64_t pendingSince{0};
    std::int64_t lastEvaluationTs{0};
  };

  static std::string stringField(const boost::json::object& obj, const char* key, const std::string& fallback = {});
  static std::int64_t intField(const boost::json::object& obj, const char* key, std::int64_t fallback = 0);
  static double numberField(const boost::json::object& obj, const char* key, double fallback = 0);
  static bool boolField(const boost::json::object& obj, const char* key, bool fallback = false);
  static bool numericValue(const boost::json::object& sample, double& out);
  static int qualityRank(const std::string& quality);
  static boost::json::object transitionPayload(const Rule& rule, const State& state,
                                               const std::string& conditionState,
                                               std::int64_t ts,
                                               const std::string& reason,
                                               const std::string& configRevision);

  mutable std::mutex mutex_;
  std::vector<Rule> rules_;
  std::unordered_map<std::string, State> states_;
  std::string configRevision_;
};

}  // namespace smart_factory
