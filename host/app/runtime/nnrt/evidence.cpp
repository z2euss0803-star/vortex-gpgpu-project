#include <vxnn/evidence.hpp>
#include <json-c/json.h>
#include <algorithm>
#include <cerrno>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <unistd.h>
namespace vxnn {
namespace {
Result<void> check(const Event &e){
 if(!e.event_id)return Result<void>::failure({-EINVAL,"EVIDENCE","event id required"});
 if(e.cycles_valid!=e.raw_cycles.has_value()||(e.scope!=EvidenceScope::REAL_BOARD&&(e.cycles_valid||e.raw_cycles)))return Result<void>::failure({-EINVAL,"EVIDENCE","offline/invalid GPU cycles must be null"});
 if(e.start_ns.has_value()!=e.end_ns.has_value()||(e.start_ns&&*e.end_ns<*e.start_ns))return Result<void>::failure({-EINVAL,"EVIDENCE","incomplete/reversed time interval"});
 return Result<void>::success();
}
using Interval=std::pair<uint64_t,uint64_t>;
Result<uint64_t> union_length(std::vector<Interval> intervals){
 if(intervals.empty())return Result<uint64_t>::success(0);
 std::sort(intervals.begin(),intervals.end());uint64_t total=0,first=intervals[0].first,last=intervals[0].second;
 for(size_t i=1;i<intervals.size();++i){if(intervals[i].first<=last)last=std::max(last,intervals[i].second);else{if(total>UINT64_MAX-(last-first))return Result<uint64_t>::failure({-EOVERFLOW,"TIMING","union overflow"});total+=last-first;first=intervals[i].first;last=intervals[i].second;}}
 if(total>UINT64_MAX-(last-first))return Result<uint64_t>::failure({-EOVERFLOW,"TIMING","union overflow"});
 return Result<uint64_t>::success(total+last-first);
}
}
Result<void> FileEvidenceSink::emit(const Event &e){
 auto valid=check(e);if(!valid)return valid;
 std::lock_guard<std::mutex> lock(mutex_);
 std::unique_ptr<json_object,decltype(&json_object_put)> object(json_object_new_object(),json_object_put);
 if(!object)return Result<void>::failure({-ENOMEM,"EVIDENCE","JSON allocation failed"});
 auto str=[&](const char *key,const std::string &value){json_object_object_add(object.get(),key,json_object_new_string_len(value.data(),int(value.size())));};
 auto num=[&](const char *key,uint64_t value){json_object_object_add(object.get(),key,json_object_new_uint64(value));};
 auto optional=[&](const char *key,std::optional<uint64_t> value){json_object_object_add(object.get(),key,value?json_object_new_uint64(*value):nullptr);};
 str("run_id",e.run_id);str("boot_id",e.boot_id);str("request_id",e.request_id);str("model_id",e.model_id);str("plan_id",e.plan_id);str("operator_id",e.operator_id);str("job_id",e.job_id);str("source_id",e.source_id);str("host",e.host);
 num("event_id",e.event_id);num("device_generation",e.device_generation);num("buffer_generation",e.buffer_generation);num("bytes",e.bytes);optional("parent_event",e.parent_event);optional("start_ns",e.start_ns);optional("end_ns",e.end_ns);optional("raw_cycles",e.raw_cycles);
 const char *clocks[]={"LINUX_MONOTONIC","HOST_MONOTONIC","B_MONOTONIC","GPU_COUNTER"};
 const char *scopes[]={"HOST_PROTOCOL_TEST","OFFLINE","CROSS_COMPILE","IMAGE","REAL_BOARD"};
 auto clock=unsigned(e.clock_domain),scope=unsigned(e.scope);if(clock>=4||scope>=5)return Result<void>::failure({-EINVAL,"EVIDENCE","unknown clock/scope"});
 str("clock_domain",clocks[clock]);str("scope",scopes[scope]);json_object_object_add(object.get(),"cycles_valid",json_object_new_boolean(e.cycles_valid));json_object_object_add(object.get(),"status",json_object_new_int(e.status));
 std::string line=json_object_to_json_string_ext(object.get(),JSON_C_TO_STRING_PLAIN);line+='\n';
 size_t offset=0;while(offset<line.size()){ssize_t n=write(borrowed_fd_,line.data()+offset,line.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)return Result<void>::failure({n<0?-errno:-EIO,"EVIDENCE","write failed; partial bytes retained"});offset+=size_t(n);}
 return Result<void>::success();
}
Result<TimingSummary> summarize_timing(const std::vector<Event> &events){
 auto fail=[](const std::string &why){return Result<TimingSummary>::failure({-EINVAL,"TIMING",why});};
 if(events.empty())return Result<TimingSummary>::success({});
 std::map<uint64_t,const Event*> index;std::map<uint64_t,std::vector<Interval>> children;std::vector<Interval> roots;
 for(const auto &e:events){auto valid=check(e);if(!valid)return fail(valid.error->detail);if(!e.start_ns||e.host.empty()||e.clock_domain==ClockDomain::GPU_COUNTER)return fail("monotonic host interval required");
  if(e.host!=events[0].host||e.clock_domain!=events[0].clock_domain)return fail("cross-host/clock subtraction forbidden; summarize each domain separately");
  if(!index.emplace(e.event_id,&e).second)return fail("duplicate event id");}
 for(const auto &e:events){if(!e.parent_event){roots.emplace_back(*e.start_ns,*e.end_ns);continue;}
  auto parent=index.find(*e.parent_event);if(parent==index.end())return fail("missing parent event");
  if(*e.start_ns<*parent->second->start_ns||*e.end_ns>*parent->second->end_ns)return fail("child interval outside parent");
  children[*e.parent_event].emplace_back(*e.start_ns,*e.end_ns);
  std::set<uint64_t> seen{e.event_id};const Event *node=&e;
  while(node->parent_event){if(!seen.insert(*node->parent_event).second)return fail("parent cycle");auto p=index.find(*node->parent_event);if(p==index.end())return fail("missing ancestor");node=p->second;}
 }
 TimingSummary summary;
 for(const auto &e:events){auto nested=union_length(children[e.event_id]);if(!nested)return fail(nested.error->detail);uint64_t span=*e.end_ns-*e.start_ns;if(*nested.value>span)return fail("nested time exceeds parent");uint64_t exclusive=span-*nested.value;if(summary.exclusive_total_ns>UINT64_MAX-exclusive)return fail("exclusive time overflow");summary.exclusive_total_ns+=exclusive;}
 auto wall=union_length(roots);if(!wall)return fail(wall.error->detail);summary.wall_ns=*wall.value;return Result<TimingSummary>::success(summary);
}
}
