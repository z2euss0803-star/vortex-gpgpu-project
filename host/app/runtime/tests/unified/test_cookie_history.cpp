#include "nnrt/backend/logical_cookie_history.hpp"
#include <cassert>
#include <set>
#include <cstdio>
using vxunified::detail::LogicalCookieHistory;
int main(){
 {LogicalCookieHistory h(3);assert(!h.record_attempt(9)&&!h.record_attempt(7)&&!h.record_attempt(8)&&h.intervals()==1);for(auto c:{7u,8u,9u})assert(h.contains(c)&&h.record_attempt(c)==-EALREADY);assert(!h.contains(6)&&!h.contains(10));}
 {LogicalCookieHistory h(2);assert(!h.record_attempt(1)&&!h.record_attempt(3)&&h.record_attempt(5)==-ENOSPC&&!h.contains(5));assert(!h.record_attempt(2)&&h.intervals()==1);assert(!h.record_attempt(5)&&h.intervals()==2);assert(!h.record_attempt(4)&&h.intervals()==1);for(uint64_t c=1;c<=5;++c)assert(h.contains(c)&&h.record_attempt(c)==-EALREADY);}
 {LogicalCookieHistory h(3);assert(h.record_attempt(0)==-EINVAL&&!h.contains(0));assert(!h.record_attempt(UINT64_MAX)&&!h.record_attempt(UINT64_MAX-2)&&!h.record_attempt(UINT64_MAX-1)&&h.intervals()==1);assert(h.contains(UINT64_MAX)&&!h.contains(1));assert(!h.record_attempt(1)&&h.intervals()==2);}
 {LogicalCookieHistory h(4096);std::set<uint64_t>reference;uint64_t state=17;for(unsigned i=0;i<5000;++i){state=state*6364136223846793005ULL+1;auto value=1+(state>>32)%2000;const bool fresh=reference.insert(value).second;assert(h.record_attempt(value)==(fresh?0:-EALREADY));for(auto query:{uint64_t(1),value,uint64_t(2000),uint64_t(2001)})assert(h.contains(query)==bool(reference.count(query)));}}
 puts("U25_LOGICAL_COOKIE_LIFETIME_HISTORY_REPLAY_COMPRESSION_CAPACITY_AND_UINT64_BOUNDARIES_PASS_HOST_ONLY gpu_raw_cycles=null");
}
