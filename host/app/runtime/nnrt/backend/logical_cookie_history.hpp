#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
namespace vxunified {namespace detail {
// Epoch/fd-lifetime tombstones. Consecutive jobs compress to one interval; a
// bounded reserve avoids allocating after an uncertain accepted submission.
// Never erase on ACK. Exhaustion is explicit, fail-closed, and never resets HW.
class LogicalCookieHistory {
 using Range=std::pair<uint64_t,uint64_t>;
 std::vector<Range>ranges_;size_t limit_;
 size_t position(uint64_t cookie)const{
  return size_t(std::lower_bound(ranges_.begin(),ranges_.end(),cookie,[](const Range&r,uint64_t value){return r.second<value;})-ranges_.begin());
 }
public:
 static constexpr size_t maximum_ranges=1u<<20; // exactly16MiB pair storage
 explicit LogicalCookieHistory(size_t limit=maximum_ranges):limit_(limit){if(!limit||limit>maximum_ranges)throw std::invalid_argument("bounded logical identity history");ranges_.reserve(limit);}
 bool contains(uint64_t cookie)const{const auto i=position(cookie);return cookie&&i<ranges_.size()&&ranges_[i].first<=cookie;}
 int record_attempt(uint64_t cookie){
  if(!cookie)return -EINVAL;
  const auto i=position(cookie);if(i<ranges_.size()&&ranges_[i].first<=cookie)return -EALREADY;
  const bool left=i&&ranges_[i-1].second!=UINT64_MAX&&ranges_[i-1].second+1==cookie;
  const bool right=i<ranges_.size()&&cookie!=UINT64_MAX&&cookie+1==ranges_[i].first;
  if(left&&right){ranges_[i-1].second=ranges_[i].second;ranges_.erase(ranges_.begin()+i);}
  else if(left)ranges_[i-1].second=cookie;
  else if(right)ranges_[i].first=cookie;
  else{if(ranges_.size()==limit_)return -ENOSPC;ranges_.insert(ranges_.begin()+i,Range{cookie,cookie});}
  return 0;
 }
 size_t intervals()const{return ranges_.size();}
};
}}
