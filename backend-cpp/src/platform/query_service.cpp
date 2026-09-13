#include "smart_factory/platform/query_service.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smart_factory {
namespace {
std::int64_t integer(const boost::json::object& obj, const char* key, std::int64_t fallback = 0) {
  const auto* v=obj.if_contains(key); if(!v)return fallback;
  if(v->is_int64())return v->as_int64(); if(v->is_uint64())return static_cast<std::int64_t>(v->as_uint64());
  return fallback;
}
double number(const boost::json::value& v) {
  if(v.is_double())return v.as_double(); if(v.is_int64())return static_cast<double>(v.as_int64());
  if(v.is_uint64())return static_cast<double>(v.as_uint64()); return 0.0;
}
std::string str(const boost::json::object& obj,const char* key){const auto* v=obj.if_contains(key);return v&&v->is_string()?std::string(v->as_string()):std::string();}
}

boost::json::object QueryService::coverage(const boost::json::array& items, std::int64_t fromTs,
                                           std::int64_t toTs, std::int64_t samplePeriodMs) {
  if (samplePeriodMs <= 0 || toTs < fromTs) return {{"expectedSamples",0},{"receivedSamples",static_cast<std::int64_t>(items.size())},{"goodSamples",0},{"coverage",nullptr}};
  const auto expected = std::max<std::int64_t>(1, ((toTs-fromTs)/samplePeriodMs)+1);
  std::int64_t good=0;
  for(const auto& v:items) if(v.is_object() && str(v.as_object(),"quality")=="GOOD") ++good;
  const double ratio=std::min(1.0, static_cast<double>(items.size())/static_cast<double>(expected));
  return {{"expectedSamples",expected},{"receivedSamples",static_cast<std::int64_t>(items.size())},{"goodSamples",good},{"coverage",ratio}};
}

boost::json::array QueryService::counterSegments(const boost::json::array& items, std::int64_t maxGapMs) {
  std::vector<boost::json::object> samples;
  for(const auto& v:items) if(v.is_object()) {
    const auto& o=v.as_object(); const auto* value=o.if_contains("value");
    if(value && (value->is_double()||value->is_int64()||value->is_uint64())) samples.push_back(o);
  }
  std::sort(samples.begin(),samples.end(),[](const auto&a,const auto&b){return integer(a,"sampleTs",integer(a,"receiveTs"))<integer(b,"sampleTs",integer(b,"receiveTs"));});
  boost::json::array out; if(samples.empty()) return out;
  std::size_t start=0; int gaps=0;
  auto flush=[&](std::size_t end){
    const auto& a=samples[start]; const auto& b=samples[end]; const auto av=number(*a.if_contains("value")); const auto bv=number(*b.if_contains("value"));
    out.emplace_back(boost::json::object{{"startTs",integer(a,"sampleTs",integer(a,"receiveTs"))},{"endTs",integer(b,"sampleTs",integer(b,"receiveTs"))},{"startValue",av},{"endValue",bv},{"delta",std::max(0.0,bv-av)},{"sampleCount",static_cast<std::int64_t>(end-start+1)},{"gapCount",gaps}});
  };
  for(std::size_t i=1;i<samples.size();++i){
    const auto prev=number(*samples[i-1].if_contains("value")); const auto cur=number(*samples[i].if_contains("value"));
    const auto prevTs=integer(samples[i-1],"sampleTs",integer(samples[i-1],"receiveTs")); const auto curTs=integer(samples[i],"sampleTs",integer(samples[i],"receiveTs"));
    if(maxGapMs>0 && curTs-prevTs>maxGapMs) ++gaps;
    if(cur<prev){flush(i-1);start=i;gaps=0;}
  }
  flush(samples.size()-1); return out;
}

}  // namespace smart_factory
