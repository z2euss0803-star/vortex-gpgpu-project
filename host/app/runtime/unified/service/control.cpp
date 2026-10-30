#include <vxunified/control.hpp>
#include "nnrt/package/json.hpp"
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <deque>
#include <time.h>
namespace vxunified {
namespace {
namespace json=vxnn::json;
// One base,128 sealed reviewed inputs,8 uploaded inputs; bounded RPC stays64KiB.
constexpr size_t input_inventory_limit=137;
bool id(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
// Only Runtime's explicit quota diagnostics describe atomic pre-execution
// budget rejection. ENOSPC from files, evidence, callbacks or device retirement
// does not carry this contract and remains fatal to the service owner.
bool local_quota(const vxnn::Error&e){return e.code==ENOSPC&&e.detail.rfind("quota=",0)==0&&(e.category=="RUNTIME_RESIDENT"||e.category=="RUNTIME_HOST_MEMORY"||e.category=="RUNTIME");}
bool permitted(AppKind a,uint32_t flags){return (flags==4&&(a==AppKind::NN||a==AppKind::P8||a==AppKind::IMAGE))||(flags==1&&(a==AppKind::NN||a==AppKind::IMAGE))||(flags==2&&a==AppKind::P8);}
bool same_key(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
std::string hex(const Digest&d){const char*h="0123456789abcdef";std::string s;for(auto b:d){s+=h[b>>4];s+=h[b&15];}return s;}
json::Ptr object(){auto*p=json_object_new_object();if(!p)throw std::bad_alloc();return json::Ptr(p,json_object_put);}
void put(json_object*o,const char*k,json_object*v){if(!v)throw std::bad_alloc();if(json_object_object_add(o,k,v)){json_object_put(v);throw std::bad_alloc();}}
void text(json_object*o,const char*k,const std::string&v){put(o,k,json_object_new_string_len(v.data(),int(v.size())));}
void number(json_object*o,const char*k,uint64_t v){put(o,k,json_object_new_uint64(v));}
void boolean(json_object*o,const char*k,bool v){put(o,k,json_object_new_boolean(v));}
const char*status_name(RequestStatus s){switch(s){case RequestStatus::CREATED:return "CREATED";case RequestStatus::VALIDATED:return "VALIDATED";case RequestStatus::QUEUED:return "QUEUED";case RequestStatus::RUNNING:return "RUNNING";case RequestStatus::CANCEL_PENDING:return "CANCEL_PENDING";case RequestStatus::COMPLETED:return "COMPLETED";case RequestStatus::FAILED:return "FAILED";case RequestStatus::CANCELLED:return "CANCELLED";}throw std::runtime_error("unknown runtime status");}
const char*numeric_name(vxnn::NumericClass n){switch(n){case vxnn::NumericClass::PASS:return "PASS";case vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION:return "KNOWN_NUMERIC_DEVIATION";case vxnn::NumericClass::NEW_MISMATCH:return "NEW_MISMATCH";case vxnn::NumericClass::NOT_EVALUATED:return "NOT_EVALUATED";}throw std::runtime_error("unknown numeric classification");}
std::string params(const std::string&s){
 json::check(s.size()<=65536,"parameter bytes");auto v=json::parse(s);json::check(json_object_is_type(v.get(),json_type_object)&&json_object_object_length(v.get())<=16,"parameter map limit16");
 json_object_object_foreach(v.get(),k,x){json::check(id(k),"parameter identifier");auto t=json_object_get_type(x);json::check(t==json_type_boolean||t==json_type_int||(t==json_type_string&&json::str(x).size()<=128),"bounded scalar parameter");}
 auto canonical=vxnn::canonical_metadata(s);json::check(bool(canonical),"parameter canonicalization failed");return *canonical.value;
}
Digest input_plan(const TrustedApplication&a,const RegisteredInput&i){return nonzero_digest(i.plan)?i.plan:a.plan;}
Result<RegisteredInput>resolve(const TrustedApplication&a,const std::string&name){
 auto found=std::find_if(a.inputs.begin(),a.inputs.end(),[&](const RegisteredInput&i){return i.id==name;});
 auto result=found!=a.inputs.end()?Result<RegisteredInput>::success(*found):a.resolve_input?a.resolve_input(name):Result<RegisteredInput>::failure({ENOENT,"CONTROL","input not registered"});
 if(result&&(result.value->id!=name||!id(name)||!nonzero_digest(result.value->sha)||!result.value->bytes||result.value->bytes>(64ULL<<20)||!nonzero_digest(input_plan(a,*result.value))))return Result<RegisteredInput>::failure({EPROTO,"CONTROL","trusted input resolver changed identity or bounds"});
 return result;
}
std::string input_json(const TrustedApplication&a,const RegisteredInput&i){auto o=object();text(o.get(),"input_id",i.id);text(o.get(),"sha256",hex(i.sha));number(o.get(),"bytes",i.bytes);text(o.get(),"plan",hex(input_plan(a,i)));return json::dump(o.get());}
std::string result_json(const PublishedResult&p){
 auto o=object();boolean(o.get(),"execution_complete",p.execution_complete);boolean(o.get(),"displayed",p.displayed);text(o.get(),"numeric_class",numeric_name(p.numeric));number(o.get(),"evidence_scope",uint32_t(p.lease.scope));text(o.get(),"raw_legacy_json",p.raw_legacy_json);
 if(p.gpu_raw_cycles)number(o.get(),"gpu_raw_cycles",*p.gpu_raw_cycles);else if(json_object_object_add(o.get(),"gpu_raw_cycles",nullptr))throw std::bad_alloc();
 text(o.get(),"hardware",hex(p.lease.hardware));text(o.get(),"model",hex(p.lease.model));text(o.get(),"plan",hex(p.lease.plan));
 json::Ptr a(json_object_new_array(),json_object_put);if(!a)throw std::bad_alloc();for(const auto&v:p.lease.artifacts){auto item=object();text(item.get(),"path",v.path);text(item.get(),"layout",v.layout);text(item.get(),"sha256",hex(v.sha));number(item.get(),"bytes",v.bytes);json::Ptr dims(json_object_new_array(),json_object_put);if(!dims)throw std::bad_alloc();for(auto d:v.shape){auto*x=json_object_new_uint64(d);if(!x)throw std::bad_alloc();if(json_object_array_add(dims.get(),x)){json_object_put(x);throw std::bad_alloc();}}put(item.get(),"shape",json_object_get(dims.get()));if(json_object_array_add(a.get(),json_object_get(item.get())))throw std::bad_alloc();}put(o.get(),"artifacts",json_object_get(a.get()));return json::dump(o.get());
}
}
struct ServiceControl::State{
 using Key=std::tuple<std::string,std::string,std::string>;
 struct Memo{std::string entry,input,parameters;bool admitted=false,reaped=false;std::optional<RpcRequest>pending={};std::shared_ptr<PreparationLease>reservation={};bool preparing=false,cancelled=false;std::optional<vxnn::Error>error={};uint64_t prepare_begin_ns=0,prepare_end_ns=0;RegisteredInput input_identity{};};
 std::mutex mutex,prepare_mutex;std::shared_ptr<Runtime> runtime;BackendIdentity identity;std::string boot;std::vector<TrustedApplication> applications;std::map<Key,Memo> requests;ResultControl display,export_result;bool asynchronous=false,closing=false;PersistPreparation persist;std::deque<Key>pending;
 static uint64_t clock(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
 Result<std::string> status(const RpcRequest&q){
  auto i=requests.find({q.request.boot_id,q.request.client_id,q.request.request_id});auto o=object();text(o.get(),"request_id",q.request.request_id);
  if(i!=requests.end()&&!i->second.admitted){auto&m=i->second;text(o.get(),"status",m.error?"FAILED":m.cancelled?(m.preparing?"CANCEL_PENDING":"CANCELLED"):"QUEUED");text(o.get(),"preparation",m.preparing?"ACTIVE":m.prepare_end_ns?"FINISHED":"QUEUED");if(m.error){number(o.get(),"preparation_error_code",uint64_t(m.error->code));text(o.get(),"preparation_error_category",m.error->category);text(o.get(),"preparation_error_detail",m.error->detail);}}
  else{auto r=runtime->diagnostic(q.request);if(!r)return Result<std::string>::failure(*r.error);const auto&d=*r.value;text(o.get(),"status",status_name(d.status));boolean(o.get(),"device_pending",d.device_pending);boolean(o.get(),"retirement_unknown",d.retirement_unknown);boolean(o.get(),"resources_retained",d.resources_retained);boolean(o.get(),"recovered",d.recovered);number(o.get(),"device_generation",d.device_generation);number(o.get(),"submitted_sequence",d.submitted_sequence);
   auto error=[&](const char*key,const std::optional<vxnn::Error>&e){if(!e)return;auto row=object();put(row.get(),"code",json_object_new_int(e->code));text(row.get(),"category",e->category);text(row.get(),"detail",e->detail);put(o.get(),key,json_object_get(row.get()));};error("execution_error",d.execution_error);error("evidence_error",d.evidence_error);error("recovery_error",d.recovery_error);}
  if(i!=requests.end()&&i->second.admitted){auto a=std::find_if(applications.begin(),applications.end(),[&](const TrustedApplication&a){return a.id==i->second.entry;});if(a!=applications.end()&&a->progress){auto progress=a->progress(q.request);if(!progress)return Result<std::string>::failure(*progress.error);if(progress.value->size()>4096)return Result<std::string>::failure({EFBIG,"CONTROL","bounded application progress"});auto raw=json::parse(*progress.value);json::check(json_object_is_type(raw.get(),json_type_object),"application progress object required");put(o.get(),"application_progress",json_object_get(raw.get()));}}
  if(i!=requests.end()&&asynchronous){number(o.get(),"preparation_begin_ns",i->second.prepare_begin_ns);number(o.get(),"preparation_end_ns",i->second.prepare_end_ns);}return Result<std::string>::success(json::dump(o.get()));
 }
};
ServiceControl::ServiceControl(std::unique_ptr<State>s):state_(std::move(s)){}
ServiceControl::~ServiceControl()=default;
Result<std::shared_ptr<ServiceControl>> ServiceControl::start(std::shared_ptr<Runtime>rt,BackendIdentity binding,std::string boot,std::vector<TrustedApplication>apps,ResultControl display,ResultControl export_result,bool async_preparation,PersistPreparation persist){
 try{
  if(!rt||!id(boot)||!binding.device_generation||!nonzero_digest(binding.hardware_contract)||!nonzero_digest(binding.bit)||!nonzero_digest(binding.xsa)||!nonzero_digest(binding.compatible)||(binding.provider_flags!=1&&binding.provider_flags!=2&&binding.provider_flags!=4)||apps.size()>64)return Result<std::shared_ptr<ServiceControl>>::failure({EINVAL,"CONTROL","bound runtime/boot and registry limit64 required"});
  auto actual=rt->identity();if(actual.hardware_contract!=binding.hardware_contract||actual.bit!=binding.bit||actual.xsa!=binding.xsa||actual.compatible!=binding.compatible||actual.device_generation!=binding.device_generation||actual.provider_flags!=binding.provider_flags||actual.board_identity_verified!=binding.board_identity_verified)return Result<std::shared_ptr<ServiceControl>>::failure({EXDEV,"CONTROL","control registry identity differs from Runtime startup probe"});
  std::set<std::string>ids;for(const auto&a:apps){if(!id(a.id)||!ids.insert(a.id).second||!a.prepare||a.inputs.empty()||a.inputs.size()>input_inventory_limit||!nonzero_digest(a.model)||!nonzero_digest(a.plan))return Result<std::shared_ptr<ServiceControl>>::failure({EINVAL,"CONTROL","invalid trusted application registry"});if(a.hardware!=binding.hardware_contract||!permitted(a.app,binding.provider_flags))return Result<std::shared_ptr<ServiceControl>>::failure({EXDEV,"CONTROL","registry belongs to another frozen provider"});std::set<std::string>inputs;for(const auto&i:a.inputs)if(!id(i.id)||!inputs.insert(i.id).second||!nonzero_digest(i.sha)||!i.bytes||i.bytes>(64ULL<<20))return Result<std::shared_ptr<ServiceControl>>::failure({EINVAL,"CONTROL","invalid registered input identity/bounds"});}
  auto s=std::make_unique<State>();s->runtime=std::move(rt);s->identity=binding;s->boot=std::move(boot);s->applications=std::move(apps);s->display=std::move(display);s->export_result=std::move(export_result);s->asynchronous=async_preparation;s->persist=std::move(persist);return Result<std::shared_ptr<ServiceControl>>::success(std::shared_ptr<ServiceControl>(new ServiceControl(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<ServiceControl>>::failure({ENOMEM,"CONTROL","registry allocation"});}catch(...){return Result<std::shared_ptr<ServiceControl>>::failure({EIO,"CONTROL","registry startup exception"});}
}
Result<std::string> ServiceControl::handle(const RpcRequest&q){
 return handle(q,true);
}
Result<std::string> ServiceControl::handle(const RpcRequest&q,bool admission_open){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);
 try{
  if(q.command=="models"||q.command=="scenes"||q.command=="effects"){
   AppKind kind=q.command=="models"?AppKind::NN:q.command=="scenes"?AppKind::P8:AppKind::IMAGE;auto o=object();json::Ptr a(json_object_new_array(),json_object_put);if(!a)throw std::bad_alloc();for(const auto&e:s.applications)if(e.app==kind){auto x=object();text(x.get(),"entry_id",e.id);text(x.get(),"model",hex(e.model));text(x.get(),"plan",hex(e.plan));if(json_object_array_add(a.get(),json_object_get(x.get())))throw std::bad_alloc();}put(o.get(),"entries",json_object_get(a.get()));return Result<std::string>::success(json::dump(o.get()));
  }
  if(q.request.boot_id!=s.boot){return Result<std::string>::failure({ESTALE,"CONTROL","boot epoch differs"});}
  if(q.command=="results"){
   auto entry=std::find_if(s.applications.begin(),s.applications.end(),[&](const TrustedApplication&e){return e.id==q.entry_id;});if(entry==s.applications.end())return Result<std::string>::failure({ENOENT,"CONTROL","application not registered"});
   auto o=object();json::Ptr rows(json_object_new_array(),json_object_put);if(!rows)throw std::bad_alloc();unsigned count=0;
   for(auto it=s.requests.rbegin();it!=s.requests.rend()&&count<32;++it){const auto&m=it->second;if(m.entry!=entry->id||!m.admitted||m.reaped)continue;RequestKey key{std::get<0>(it->first),std::get<1>(it->first),std::get<2>(it->first)};auto status=s.runtime->status(key);if(!status||*status.value!=RequestStatus::COMPLETED)continue;auto row=object();text(row.get(),"boot_id",key.boot_id);text(row.get(),"client_id",key.client_id);text(row.get(),"request_id",key.request_id);text(row.get(),"entry_id",m.entry);text(row.get(),"input_id",m.input);text(row.get(),"model",hex(entry->model));text(row.get(),"plan",hex(input_plan(*entry,m.input_identity)));if(json_object_array_add(rows.get(),json_object_get(row.get())))throw std::bad_alloc();++count;}
   put(o.get(),"results",json_object_get(rows.get()));return Result<std::string>::success(json::dump(o.get()));
  }
  if(q.command=="inputs"||q.command=="input-register"){
   auto entry=std::find_if(s.applications.begin(),s.applications.end(),[&](const TrustedApplication&e){return e.id==q.entry_id;});if(entry==s.applications.end())return Result<std::string>::failure({ENOENT,"CONTROL","application not registered"});
   if(q.command=="input-register"){
    if(s.closing||!admission_open||s.runtime->stats().phase!=RuntimePhase::READY)return Result<std::string>::failure({EBUSY,"CONTROL","input registration admission closed"});
    if(!id(q.input_id)||!entry->register_input)return Result<std::string>::failure({EOPNOTSUPP,"CONTROL","trusted input registration is unavailable"});
    auto admitted=q;admitted.parameters=params(q.parameters);auto saved=entry->register_input(admitted);if(!saved)return Result<std::string>::failure(*saved.error);auto checked=resolve(*entry,q.input_id);if(!checked)return Result<std::string>::failure(*checked.error);
    if(saved.value->sha!=checked.value->sha||saved.value->bytes!=checked.value->bytes||input_plan(*entry,*saved.value)!=input_plan(*entry,*checked.value))return Result<std::string>::failure({EPROTO,"CONTROL","registered input differs trusted lookup"});
    return Result<std::string>::success(input_json(*entry,*checked.value));
   }
   std::map<std::string,RegisteredInput>inputs;for(const auto&i:entry->inputs)inputs.emplace(i.id,i);
   if(entry->list_inputs){auto listed=entry->list_inputs();if(!listed)return Result<std::string>::failure(*listed.error);if(listed.value->size()>input_inventory_limit)return Result<std::string>::failure({EOVERFLOW,"CONTROL","input inventory limit137"});for(const auto&i:*listed.value){auto checked=resolve(*entry,i.id);if(!checked)return Result<std::string>::failure(*checked.error);if(i.sha!=checked.value->sha||i.bytes!=checked.value->bytes||input_plan(*entry,i)!=input_plan(*entry,*checked.value))return Result<std::string>::failure({ESTALE,"CONTROL","input list differs registered identity"});inputs[i.id]=*checked.value;}}
   if(inputs.size()>input_inventory_limit){return Result<std::string>::failure({EOVERFLOW,"CONTROL","input inventory limit137"});}
   auto o=object();text(o.get(),"entry_id",entry->id);json::Ptr rows(json_object_new_array(),json_object_put);if(!rows)throw std::bad_alloc();for(const auto&row:inputs){auto value=json::parse(input_json(*entry,row.second));if(json_object_array_add(rows.get(),json_object_get(value.get())))throw std::bad_alloc();}put(o.get(),"inputs",json_object_get(rows.get()));auto reply=json::dump(o.get());if(reply.size()>60000)return Result<std::string>::failure({EOVERFLOW,"CONTROL","input inventory exceeds bounded RPC payload"});return Result<std::string>::success(std::move(reply));
  }
  if(q.command=="mode"||q.command=="mode-status"){
   if(!s8_joint_platform(s.identity))return Result<std::string>::failure({EOPNOTSUPP,"MODE","explicit S8 joint platform required"});
   if(q.command=="mode"){
    if(s.closing||!admission_open)return Result<std::string>::failure({EBUSY,"MODE","faulted or closing service retains original mode"});
    AppKind app=q.entry_id=="nn"?AppKind::NN:q.entry_id=="image"?AppKind::IMAGE:AppKind::P8;
    if(q.entry_id!="nn"&&q.entry_id!="image"&&q.entry_id!="p8")return Result<std::string>::failure({EINVAL,"MODE","unknown mode"});
    auto requested=s.runtime->request_mode(app);if(!requested)return Result<std::string>::failure(*requested.error);
   }
   auto mode=s.runtime->mode_snapshot();auto name=[](AppKind app){return app==AppKind::NN?"nn":app==AppKind::IMAGE?"image":"p8";};auto o=object();text(o.get(),"current",name(mode.current));text(o.get(),"target",name(mode.target));boolean(o.get(),"switching",mode.switching);number(o.get(),"sequence",mode.sequence);number(o.get(),"requested_ns",mode.requested_ns);number(o.get(),"completed_ns",mode.completed_ns);
   number(o.get(),"handoff_ns",mode.completed_ns?mode.completed_ns-mode.requested_ns:0);text(o.get(),"clock_domain",s.runtime->evidence_scope()==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC");if(json_object_object_add(o.get(),"gpu_raw_cycles",nullptr))throw std::bad_alloc();return Result<std::string>::success(json::dump(o.get()));
  }
  if(!id(q.request.client_id)||!id(q.request.request_id)){return Result<std::string>::failure({EINVAL,"CONTROL","invalid request identity"});}
  State::Key key{q.request.boot_id,q.request.client_id,q.request.request_id};auto memo=s.requests.find(key);
  if(q.command=="submit"){
   auto parameters=params(q.parameters);
   if(memo!=s.requests.end()){auto&m=memo->second;if(m.entry!=q.entry_id||m.input!=q.input_id||m.parameters!=parameters)return Result<std::string>::failure({EEXIST,"CONTROL","request reused with changed registered payload"});if(m.reaped)return Result<std::string>::failure({EALREADY,"CONTROL","client result reaped; request identity is a tombstone"});return s.status(q);}
   if(s.closing||!admission_open||s.runtime->stats().phase!=RuntimePhase::READY)return Result<std::string>::failure({EBUSY,"CONTROL","execution admission closed; original request remains queryable"});
   auto entry=std::find_if(s.applications.begin(),s.applications.end(),[&](const TrustedApplication&e){return e.id==q.entry_id;});if(entry==s.applications.end())return Result<std::string>::failure({ENOENT,"CONTROL","application not registered for this provider"});auto input=resolve(*entry,q.input_id);if(!input)return Result<std::string>::failure(*input.error);if(s.requests.size()>=256)return Result<std::string>::failure({ENOSPC,"CONTROL","request identity/tombstone limit256"});
   if(!s.runtime->mode_allows(entry->app))return Result<std::string>::failure({EBUSY,"MODE","switch to application mode and wait for whole-request handoff"});
   auto inserted=s.requests.emplace(key,State::Memo{q.entry_id,q.input_id,parameters,false,false}).first;
   inserted->second.input_identity=*input.value;
   try{
    RpcRequest admitted=q;admitted.parameters=parameters;
    if(entry->validate){auto valid=entry->validate(admitted);if(!valid){s.requests.erase(inserted);return Result<std::string>::failure(*valid.error);}}
    if(s.asynchronous){auto slot=s.runtime->reserve_preparation(q.request,entry->app);if(!slot){s.requests.erase(inserted);return Result<std::string>::failure(*slot.error);}inserted->second.pending=admitted;inserted->second.reservation=*slot.value;s.pending.push_back(key);return s.status(q);}
    auto prepared=entry->prepare(admitted);if(!prepared){s.requests.erase(inserted);return Result<std::string>::failure(*prepared.error);}auto&r=*prepared.value;
    if(!same_key(r.request,q.request)||r.app!=entry->app||r.model!=entry->model||r.plan!=input_plan(*entry,*input.value)||r.input_sha!=input.value->sha||r.input_bytes!=input.value->bytes){s.requests.erase(inserted);return Result<std::string>::failure({EPROTO,"CONTROL","factory changed admitted request/model/plan/input identity"});}
    auto accepted=s.runtime->submit(r);if(!accepted){s.requests.erase(inserted);return Result<std::string>::failure(*accepted.error);}inserted->second.admitted=true;return s.status(q);
   }catch(...){auto retained=s.requests.find(key);if(retained!=s.requests.end()&&!retained->second.admitted)s.requests.erase(retained);throw;}
  }
  if(memo==s.requests.end()){return Result<std::string>::failure({ENOENT,"CONTROL","request not admitted by this service"});}
  if(memo->second.reaped){return Result<std::string>::failure({EALREADY,"CONTROL","client result already reaped"});}
  if(q.command=="status")return s.status(q);
  if(q.command=="recover"){
   auto diagnostic=s.runtime->diagnostic(q.request);if(!diagnostic)return Result<std::string>::failure(*diagnostic.error);if(!diagnostic.value->device_pending)return Result<std::string>::failure({EALREADY,"CONTROL","original request has no pending device completion"});auto recovered=s.runtime->recover_pending(100000000ULL);if(!recovered)return Result<std::string>::failure(*recovered.error);return s.status(q);
  }
  if(!memo->second.admitted){
   auto&m=memo->second;
   if(q.command=="cancel"){m.cancelled=true;if(!m.preparing){m.pending.reset();m.reservation.reset();auto it=std::find(s.pending.begin(),s.pending.end(),key);if(it!=s.pending.end())s.pending.erase(it);}return s.status(q);}
   if(q.command=="reap"&&(m.error||m.cancelled)&&!m.preparing){m.reaped=true;return Result<std::string>::success("{\"client_reaped\":true}");}
   if(m.error)return Result<std::string>::failure(*m.error);
   return Result<std::string>::failure({m.cancelled?ECANCELED:EAGAIN,"CONTROL_PREPARATION","original request preparation not submitted"});
  }
  if(q.command=="cancel"){auto r=s.runtime->cancel(q.request);if(!r)return Result<std::string>::failure(*r.error);return s.status(q);}
  if(q.command=="reap"){auto r=s.runtime->reap_result(q.request);if(!r)return Result<std::string>::failure(*r.error);memo->second.reaped=true;return Result<std::string>::success("{\"client_reaped\":true}");}
  if(q.command=="result"||q.command=="display"||q.command=="export"){
   auto r=s.runtime->take_result(q.request);if(!r)return Result<std::string>::failure(*r.error);if(q.command=="result")return Result<std::string>::success(result_json(*r.value));
   if(q.artifact>=r.value->lease.artifacts.size()){return Result<std::string>::failure({EINVAL,"CONTROL","artifact index outside retained result"});}
   if(q.command=="export"&&!id(q.destination_id)){return Result<std::string>::failure({EINVAL,"CONTROL","registered export destination required"});}
   auto&hook=q.command=="display"?s.display:s.export_result;if(!hook)return Result<std::string>::failure({EOPNOTSUPP,"CONTROL","result consumer is not registered"});return hook(*s.runtime,q,*r.value);
  }
  return Result<std::string>::failure({EOPNOTSUPP,"CONTROL","unknown command"});
 }catch(const std::bad_alloc&){return Result<std::string>::failure({ENOMEM,"CONTROL","control allocation; query original request identity"});}catch(const std::exception&e){return Result<std::string>::failure({EINVAL,"CONTROL",e.what()});}catch(...){return Result<std::string>::failure({EIO,"CONTROL","trusted control callback exception"});}
}
Result<bool>ServiceControl::prepare_one(){
 auto&s=*state_;std::unique_lock<std::mutex>worker(s.prepare_mutex,std::try_to_lock);if(!worker.owns_lock())return Result<bool>::failure({EBUSY,"CONTROL_PREPARATION","one preparation worker required"});
 try{
  RpcRequest q;TrustedApplication app;std::shared_ptr<PreparationLease>slot;State::Key k;
  {std::lock_guard<std::mutex>lock(s.mutex);if(s.closing||s.pending.empty())return Result<bool>::success(false);k=s.pending.front();auto i=s.requests.find(k);if(i==s.requests.end()||!i->second.pending||!i->second.reservation)return Result<bool>::failure({EPROTO,"CONTROL_PREPARATION","pending identity/reservation missing"});auto entry=std::find_if(s.applications.begin(),s.applications.end(),[&](const TrustedApplication&a){return a.id==i->second.entry;});if(entry==s.applications.end())return Result<bool>::failure({EPROTO,"CONTROL_PREPARATION","trusted entry disappeared"});q=*i->second.pending;app=*entry;slot=i->second.reservation;s.pending.pop_front();i->second.preparing=true;i->second.prepare_begin_ns=State::clock();}
  // No control/peer mutex across model I/O, hashing or lowering. This thread is
  // the existing service execution owner; never another fd or per-app pool.
  auto prepared=[&]()->Result<WorkRequest>{try{return app.prepare(q);}catch(const std::bad_alloc&){return Result<WorkRequest>::failure({ENOMEM,"CONTROL_PREPARATION","model preparation allocation"});}catch(const std::exception&e){return Result<WorkRequest>::failure({EIO,"CONTROL_PREPARATION",e.what()});}catch(...){return Result<WorkRequest>::failure({EIO,"CONTROL_PREPARATION","model preparation exception"});}}();
  std::lock_guard<std::mutex>lock(s.mutex);auto i=s.requests.find(k);if(i==s.requests.end())return Result<bool>::failure({EPROTO,"CONTROL_PREPARATION","active request disappeared"});auto&m=i->second;m.prepare_end_ns=State::clock();
  if(!prepared)m.error=*prepared.error;
  else{const auto&r=*prepared.value;const auto&input=m.input_identity;if(input.id!=q.input_id||!same_key(r.request,q.request)||r.app!=app.app||r.model!=app.model||r.plan!=input_plan(app,input)||r.input_sha!=input.sha||r.input_bytes!=input.bytes)m.error=vxnn::Error{EPROTO,"CONTROL_PREPARATION","factory changed admitted request/model/plan/input identity"};}
  Digest source{};if(prepared&&prepared.value->source){try{source=prepared.value->source->identity();}catch(const std::bad_alloc&){if(!m.error)m.error=vxnn::Error{ENOMEM,"CONTROL_PREPARATION","source identity allocation"};}catch(const std::exception&e){if(!m.error)m.error=vxnn::Error{EIO,"CONTROL_PREPARATION",e.what()};}catch(...){if(!m.error)m.error=vxnn::Error{EIO,"CONTROL_PREPARATION","source identity exception"};}}
  PreparationReceipt receipt;receipt.request=q.request;receipt.hardware=s.identity;receipt.model=app.model;receipt.plan=input_plan(app,m.input_identity);receipt.entry=app.id;receipt.input_id=q.input_id;receipt.input=m.input_identity.sha;receipt.begin_ns=m.prepare_begin_ns;receipt.end_ns=m.prepare_end_ns;receipt.cancelled=m.cancelled||s.closing;receipt.error=m.error;receipt.source=source;
  bool recorded=false;std::optional<vxnn::Error>journal_error;
  if(s.persist){auto saved=s.persist(receipt);if(saved)recorded=true;else{journal_error=*saved.error;if(!m.error)m.error=*saved.error;}}
  m.preparing=false;m.pending.reset();
  auto rejected=[&](const vxnn::Error&error,bool pure_quota)->Result<bool>{
   // Drop the exact unused factory source and both metadata-slot references
   // before considering another request. No work has been executed here.
   m.reservation.reset();slot.reset();
   // Result.value is immutable. Its scoped source destructor runs before
   // prepare_one returns and before the caller can process another request.
   if(pure_quota&&recorded&&!s.closing&&s.runtime->stats().phase==RuntimePhase::READY)return Result<bool>::success(true);
   return Result<bool>::failure(error);
  };
  // Losing the error journal must not be hidden by an earlier quota refusal.
  // Memo keeps the original request error; the worker sees the journal failure.
  if(journal_error)return rejected(*journal_error,false);
  if(m.error)return rejected(*m.error,!prepared&&local_quota(*m.error));
  if(m.cancelled||s.closing){m.cancelled=true;m.reservation.reset();return Result<bool>::success(true);}
  auto accepted=s.runtime->submit(*prepared.value,slot);m.reservation.reset();
  if(!accepted){
   m.error=*accepted.error;
   // Runtime admission may reject after a successful factory. Preserve the
   // first preparation event, then append a distinct error event before any
   // later work. EvidenceJournal is content-addressed; no event is overwritten.
   receipt.error=m.error;receipt.end_ns=State::clock();m.prepare_end_ns=receipt.end_ns;recorded=false;
   if(s.persist){auto saved=s.persist(receipt);if(!saved)return rejected(*saved.error,false);recorded=true;}
   return rejected(*m.error,local_quota(*m.error));
  }
  m.admitted=true;return Result<bool>::success(true);
 }catch(const std::bad_alloc&){return Result<bool>::failure({ENOMEM,"CONTROL_PREPARATION","preparation handoff allocation; original request retained"});}catch(const std::exception&e){return Result<bool>::failure({EIO,"CONTROL_PREPARATION",e.what()});}
}
Result<void>ServiceControl::quiesce_preparations(){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);s.closing=true;for(auto&entry:s.requests){auto&m=entry.second;if(!m.admitted){m.cancelled=true;if(!m.preparing){m.pending.reset();m.reservation.reset();}}}s.pending.clear();return Result<void>::success();}
}
