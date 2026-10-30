#include <vxunified/evidence.hpp>
#include <vxunified/exit_retention.hpp>
#include <json-c/json.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <stdexcept>
namespace vxunified {
namespace {
void check(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
bool id(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
std::string hex(const Digest&d){const char*h="0123456789abcdef";std::string s;s.reserve(64);for(auto b:d){s+=h[b>>4];s+=h[b&15];}return s;}
void context_valid(const EventContext&c){
 check(bool(admit_program(c.hardware,c.program)),"complete hardware/program capability binding required");
 check((c.app==AppKind::NN&&c.program.namespace_id==1)||(c.app==AppKind::P8&&c.program.namespace_id==2)||(c.app==AppKind::IMAGE&&c.program.namespace_id==3),"application namespace differs");
 check(nonzero_digest(c.model)&&nonzero_digest(c.plan)&&nonzero_digest(c.source)&&nonzero_digest(c.input),"model/plan/source/input identities required");
 check(id(c.run_id)&&id(c.request.boot_id)&&id(c.request.client_id)&&id(c.request.request_id)&&timing_phase_name(c.phase)&&unsigned(c.counter)<=unsigned(CounterKind::ZERO_JOB_CACHE),"bounded event context required");
 if(c.execution_error)check(c.execution_error->code&&!c.execution_error->category.empty()&&c.execution_error->category.size()<=256&&c.execution_error->detail.size()<=65536,"original execution error malformed");
}
void event_valid(const BoundEvent&b){
 const auto&e=b.event;const auto&c=b.context;context_valid(c);
 check(e.event_id&&id(e.host)&&id(e.operator_id)&&id(e.job_id)&&e.run_id==c.run_id&&e.boot_id==c.request.boot_id&&e.request_id==c.request.request_id,"event/request identity differs");
 check(e.device_generation==c.hardware.device_generation&&e.model_id==hex(c.model)&&e.plan_id==hex(c.plan)&&e.source_id==hex(c.source),"event model/plan/source/epoch differs");
 check(e.start_ns.has_value()&&e.end_ns.has_value()&&*e.end_ns>=*e.start_ns&&unsigned(e.clock_domain)<unsigned(vxnn::ClockDomain::GPU_COUNTER)&&unsigned(e.scope)<=unsigned(vxnn::EvidenceScope::REAL_BOARD),"complete monotonic interval and scope required");
 if(e.parent_event)check(*e.parent_event!=0,"nonzero parent event required");
 check(bool(e.status)==bool(c.execution_error)&&(!e.status||e.status==c.execution_error->code),"original error/status identity required");
 const bool real=e.scope==vxnn::EvidenceScope::REAL_BOARD;
 check(!real||e.clock_domain==vxnn::ClockDomain::LINUX_MONOTONIC,"real device event needs Linux monotonic provenance");
 check(e.cycles_valid==e.raw_cycles.has_value()&&(!e.raw_cycles||*e.raw_cycles<=UINT32_MAX),"raw GPU counter validity/32-bit extent differs");
 check(real||(!e.cycles_valid&&!e.raw_cycles),"offline GPU counter must be null");
 if(c.counter==CounterKind::NONE)check(!e.cycles_valid&&!e.raw_cycles,"non-GPU event cannot duplicate GPU counter");
 else if(real&&c.counter==CounterKind::ZERO_JOB_CACHE)check(e.cycles_valid&&e.raw_cycles&&!*e.raw_cycles,"real zero-job cache must retain valid zero");
 else if(real){check(e.status||e.cycles_valid,"successful real GPU job missing raw counter");if(e.raw_cycles)check(*e.raw_cycles!=0,"GPU job cannot forge zero-job cache");}
}
using J=std::unique_ptr<json_object,decltype(&json_object_put)>;
J own(json_object*p){if(!p)throw std::bad_alloc();return J(p,json_object_put);}
void put(json_object*p,const char*k,J v){if(json_object_object_add(p,k,v.get()))throw std::bad_alloc();v.release();}
void str(json_object*p,const char*k,const std::string&s){put(p,k,own(json_object_new_string_len(s.data(),int(s.size()))));}
void num(json_object*p,const char*k,uint64_t n){put(p,k,own(json_object_new_uint64(n)));}
void boolean(json_object*p,const char*k,bool b){put(p,k,own(json_object_new_boolean(b)));}
void optional(json_object*p,const char*k,std::optional<uint64_t>n){if(n)num(p,k,*n);else if(json_object_object_add(p,k,nullptr))throw std::bad_alloc();}
bool same_request_context(const EventContext&a,const EventContext&b){const auto&x=a.hardware;const auto&y=b.hardware;return a.app==b.app&&a.run_id==b.run_id&&a.request.boot_id==b.request.boot_id&&a.request.client_id==b.request.client_id&&a.request.request_id==b.request.request_id&&a.model==b.model&&a.plan==b.plan&&a.source==b.source&&a.input==b.input&&x.hardware_contract==y.hardware_contract&&x.bit==y.bit&&x.xsa==y.xsa&&x.compatible==y.compatible&&x.device_generation==y.device_generation&&x.provider_flags==y.provider_flags;}
uint64_t union_length(std::vector<std::pair<uint64_t,uint64_t>>v){if(v.empty())return 0;std::sort(v.begin(),v.end());uint64_t first=v[0].first,last=v[0].second,total=0;for(size_t i=1;i<v.size();i++){if(v[i].first<=last)last=std::max(last,v[i].second);else{check(total<=UINT64_MAX-(last-first),"interval sum overflow");total+=last-first;first=v[i].first;last=v[i].second;}}check(total<=UINT64_MAX-(last-first),"interval sum overflow");return total+last-first;}
}
const char*timing_phase_name(TimingPhase p){static const char*names[]={"queue","cpu_prepare","cpu_gelu","cpu_bin","program_load","weight_load","texture_load","input_load","h2d","d2h","guard","hash","submit","lock_wait","active_poll","sleep","ack","reap","convert","kms_copy","kms_flip","kms_retire","end_to_end"};auto n=unsigned(p);return n<unsigned(TimingPhase::COUNT)?names[n]:nullptr;}
Result<std::string>serialize_event(const BoundEvent&b){
 try{
  event_valid(b);const auto&e=b.event;const auto&c=b.context;auto o=own(json_object_new_object());auto*p=o.get();
  str(p,"format","vxgpu-common-event-v1");str(p,"hardware_identity_source","BOOT_MANIFEST_DECLARED");str(p,"run_id",e.run_id);str(p,"boot_id",e.boot_id);str(p,"client_id",c.request.client_id);str(p,"request_id",e.request_id);str(p,"operator_id",e.operator_id);str(p,"job_id",e.job_id);str(p,"host",e.host);str(p,"phase",timing_phase_name(c.phase));
  const char*clocks[]={"LINUX_MONOTONIC","HOST_MONOTONIC","B_MONOTONIC"};const char*scopes[]={"HOST_PROTOCOL_TEST","OFFLINE","CROSS_COMPILE","IMAGE","REAL_BOARD"};const char*counters[]={"NONE","GPU_JOB","ZERO_JOB_CACHE"};
  str(p,"clock_domain",clocks[unsigned(e.clock_domain)]);str(p,"scope",scopes[unsigned(e.scope)]);str(p,"counter_kind",counters[unsigned(c.counter)]);
  num(p,"app",unsigned(c.app));num(p,"provider_flags",c.hardware.provider_flags);num(p,"device_generation",e.device_generation);num(p,"buffer_generation",e.buffer_generation);num(p,"event_id",e.event_id);num(p,"bytes",e.bytes);num(p,"namespace_id",c.program.namespace_id);num(p,"profile_id",c.program.logical_profile);num(p,"program_id",c.program.logical_program);num(p,"raw_counter_bits",32);
  optional(p,"parent_event",e.parent_event);optional(p,"start_ns",e.start_ns);optional(p,"end_ns",e.end_ns);optional(p,"gpu_raw_cycles",e.raw_cycles);boolean(p,"cycles_valid",e.cycles_valid);
  const char*keys[]={"hardware_sha","bit_sha","xsa_sha","compatible_sha","program_code_sha","program_schema_sha","model_sha","plan_sha","source_sha","input_sha"};const Digest*values[]={&c.hardware.hardware_contract,&c.hardware.bit,&c.hardware.xsa,&c.hardware.compatible,&c.program.code,&c.program.schema,&c.model,&c.plan,&c.source,&c.input};for(unsigned i=0;i<10;i++){str(p,keys[i],hex(*values[i]));}
  put(p,"status",own(json_object_new_int(e.status)));
  if(c.execution_error){auto x=own(json_object_new_object());put(x.get(),"code",own(json_object_new_int(c.execution_error->code)));str(x.get(),"category",c.execution_error->category);str(x.get(),"detail",c.execution_error->detail);put(p,"execution_error",std::move(x));}else if(json_object_object_add(p,"execution_error",nullptr))throw std::bad_alloc();
  auto*text=json_object_to_json_string_ext(p,JSON_C_TO_STRING_PLAIN);if(!text)throw std::bad_alloc();std::string line(text);line+='\n';check(line.size()<=(1u<<20),"event record exceeds bound");return Result<std::string>::success(std::move(line));
 }catch(const std::bad_alloc&){return Result<std::string>::failure({ENOMEM,"COMMON_EVENT","serialization allocation"});}catch(const std::exception&e){return Result<std::string>::failure({EINVAL,"COMMON_EVENT",e.what()});}
}
Result<TimingSummary>summarize_timing(const std::vector<BoundEvent>&events){
 try{
  check(events.size()<=65536,"bounded event batch required");std::vector<Event>base;base.reserve(events.size());bool seen_counter=false,complete=true;uint64_t cycles=0;std::set<std::string>counter_jobs;
  std::map<uint64_t,std::vector<std::pair<uint64_t,uint64_t>>>children;
  for(const auto&b:events){event_valid(b);check(same_request_context(events[0].context,b.context)&&events[0].event.scope==b.event.scope,"request/provider/scope differs; summarize separately");base.push_back(b.event);if(b.event.parent_event)children[*b.event.parent_event].emplace_back(*b.event.start_ns,*b.event.end_ns);if(b.context.counter!=CounterKind::NONE){check(counter_jobs.insert(b.event.job_id).second,"duplicate GPU job counter attribution");seen_counter=true;if(b.event.scope!=vxnn::EvidenceScope::REAL_BOARD||!b.event.cycles_valid)complete=false;else{check(cycles<=UINT64_MAX-*b.event.raw_cycles,"counter total overflow");cycles+=*b.event.raw_cycles;}}}
  auto original=vxnn::summarize_timing(base);if(!original)return Result<TimingSummary>::failure(*original.error);TimingSummary out;out.wall_ns=original.value->wall_ns;out.exclusive_work_ns=original.value->exclusive_total_ns;
  for(const auto&b:events){auto span=*b.event.end_ns-*b.event.start_ns;auto nested=union_length(children[b.event.event_id]);check(nested<=span,"nested interval exceeds parent");auto&phase=out.phases[b.context.phase];check(phase.inclusive_ns<=UINT64_MAX-span&&phase.exclusive_ns<=UINT64_MAX-(span-nested),"phase sum overflow");phase.inclusive_ns+=span;phase.exclusive_ns+=span-nested;++phase.count;}
  out.gpu_cycles_complete=seen_counter&&complete;if(out.gpu_cycles_complete)out.gpu_raw_cycles=cycles;return Result<TimingSummary>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<TimingSummary>::failure({ENOMEM,"COMMON_TIMING","summary allocation"});}catch(const std::exception&e){return Result<TimingSummary>::failure({EINVAL,"COMMON_TIMING",e.what()});}
}
struct UnifiedEvidenceSink::Impl {
 detail::ExitRetention exit;EventContext context;PersistEvent persist;mutable std::mutex mutex;std::set<uint64_t>ids;std::string pending;EventSinkStats receipt;
 Result<void>save(){
  try{auto r=persist(pending);if(!r){if(!receipt.first_error)receipt.first_error=*r.error;return Result<void>::failure(*r.error);}pending.clear();receipt.pending=false;++receipt.emitted;return Result<void>::success();}
  catch(const std::bad_alloc&){vxnn::Error e{ENOMEM,"COMMON_EVENT","evidence callback allocation; exact bytes retained"};if(!receipt.first_error)receipt.first_error=e;return Result<void>::failure(std::move(e));}
  catch(...){vxnn::Error e{EIO,"COMMON_EVENT","evidence callback exception; exact bytes retained"};if(!receipt.first_error)receipt.first_error=e;return Result<void>::failure(std::move(e));}
 }
};
UnifiedEvidenceSink::UnifiedEvidenceSink(std::shared_ptr<Impl>p):impl_(std::move(p)){}
UnifiedEvidenceSink::~UnifiedEvidenceSink(){std::lock_guard<std::mutex>lock(impl_->mutex);if(impl_->receipt.pending)impl_->exit.retain(impl_);}
Result<std::shared_ptr<UnifiedEvidenceSink>>UnifiedEvidenceSink::create(EventContext c,PersistEvent p){
 using R=Result<std::shared_ptr<UnifiedEvidenceSink>>;try{context_valid(c);check(bool(p),"trusted atomic evidence writer required");auto s=std::make_shared<Impl>();s->context=std::move(c);s->persist=std::move(p);return R::success(std::shared_ptr<UnifiedEvidenceSink>(new UnifiedEvidenceSink(std::move(s))));}catch(const std::bad_alloc&){return R::failure({ENOMEM,"COMMON_EVENT","sink allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"COMMON_EVENT",e.what()});}
}
Result<void>UnifiedEvidenceSink::emit(const Event&e){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);if(s.receipt.pending)return Result<void>::failure({EBUSY,"COMMON_EVENT","pending exact evidence requires explicit evidence-only retry"});
 try{if(s.ids.count(e.event_id))return Result<void>::failure({EALREADY,"COMMON_EVENT","event ID already reserved or delivered"});if(s.ids.size()>=65536)return Result<void>::failure({ENOSPC,"COMMON_EVENT","request event limit reached"});auto line=serialize_event({e,s.context});if(!line)return Result<void>::failure(*line.error);std::string bytes=*line.value;s.ids.insert(e.event_id);s.pending=std::move(bytes);s.receipt.pending=true;return s.save();}catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"COMMON_EVENT","event admission allocation"});}
}
Result<void>UnifiedEvidenceSink::retry_evidence(){auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);if(!s.receipt.pending)return Result<void>::failure({EALREADY,"COMMON_EVENT","no pending evidence"});return s.save();}
EventSinkStats UnifiedEvidenceSink::stats()const{std::lock_guard<std::mutex>lock(impl_->mutex);return impl_->receipt;}
}
