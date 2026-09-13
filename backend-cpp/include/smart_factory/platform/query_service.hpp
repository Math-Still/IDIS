#pragma once

#include <boost/json.hpp>
#include <cstdint>

namespace smart_factory {

class QueryService {
 public:
  static boost::json::object coverage(const boost::json::array& items, std::int64_t fromTs,
                                      std::int64_t toTs, std::int64_t samplePeriodMs);
  static boost::json::array counterSegments(const boost::json::array& items,
                                            std::int64_t maxGapMs = 0);
};

}  // namespace smart_factory
