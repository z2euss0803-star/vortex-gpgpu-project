#include <vxunified/service.hpp>
#include <vxunified/exit_retention.hpp>
#include <vxl/v2/jobs.h>
#include <map>
#include <mutex>
#include <tuple>
#include <time.h>
#include <type_traits>
#include <thread>
namespace vxunified {
namespace {
using Key=std::tuple<std::string,std::string,std::string>;
Key key(const RequestKey&k){return {k.boot_id,k.client_id,k.request_id};}
bool terminal(RequestStatus s){return s==RequestStatus::COMPLETED||s==RequestStatus::FAILED||s==RequestStatus::CANCELLED;}
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
constexpr uint64_t input_limit=64ULL<<20,cpu_limit=1ULL<<30,overhead=128ULL<<20;
struct BoundSource:WorkSource {
 std::shared_ptr<const WorkSource> source;uint64_t epoch;BackendMode mode;
 BoundSource(std::shared_ptr<const WorkSource>s,uint64_t e,BackendMode m):source(std::move(s)),epoch(e),mode(m){}
 uint64_t size()const override{return source->size();}
 Digest identity()const override{return source->identity();}
 Result<bool> ready(uint64_t n)const override{return source->ready(n);}
 Result<WorkItem> get(uint64_t n)const override{
  auto r=source->get(n);if(!r)return r;
  if(mode==BackendMode::LEGACY_COMPAT){if(source->size()!=1||r.value->kind!=WorkKind::LEGACY_EXCLUSIVE)return Result<WorkItem>::failure({EOPNOTSUPP,"RUNTIME","legacy is one opaque whole-graph work item"});}
  else if(r.value->kind==WorkKind::LEGACY_EXCLUSIVE)return Result<WorkItem>::failure({EOPNOTSUPP,"RUNTIME","legacy aggregate cannot enter a compute-v2 session"});
  if(r.value->kind==WorkKind::GPU&&gpu_work_epoch(*r.value)!=epoch)return Result<WorkItem>::failure({ESTALE,"RUNTIME","lowered device epoch differs"});
  return r;
 }
};
struct ResidentBudget {
 struct Model {uint64_t bytes=0;uint32_t users=0,pins=0;AppKind app=AppKind::NN;};
 std::mutex mutex;std::map<Digest,Model>models;uint64_t cpu=overhead;uint32_t leases=0,host_leases=0;
};
bool role_allowed(uint32_t provider,AppKind app){return(provider==4&&(app==AppKind::NN||app==AppKind::P8||app==AppKind::IMAGE))||(provider==1&&(app==AppKind::NN||app==AppKind::IMAGE))||(provider==2&&app==AppKind::P8);}
size_t nn_models(const ResidentBudget&b){return size_t(std::count_if(b.models.begin(),b.models.end(),[](const auto&entry){return entry.second.app==AppKind::NN;}));}
struct PreparationBudget {
 std::mutex mutex;std::map<Key,uint64_t>slots;uint64_t serial=0;std::string boot;
};
struct Backing {
 struct Entry {WorkRequest request;Digest source_id{};uint64_t count=0;RequestStatus visible=RequestStatus::QUEUED;bool released=false;std::optional<RequestDiagnostic>fault;};
 std::mutex mutex;std::shared_ptr<DeviceOwner>owner;std::shared_ptr<ExecutionPort>port;BackendIdentity identity;vxnn::EvidenceScope scope;
 std::shared_ptr<UnifiedBackend>native_backend; // immutable startup alias, never a callback-time getter
 std::shared_ptr<ResidentBudget>budget=std::make_shared<ResidentBudget>();
 std::shared_ptr<PreparationBudget>preparations=std::make_shared<PreparationBudget>();
 std::map<Key,Entry> entries;uint64_t inputs=0;RuntimePhase phase=RuntimePhase::READY;std::optional<vxnn::Error>first_error;
 Result<void> retire_locked(std::map<Key,Entry>::iterator it){
  auto&e=it->second;if(e.released)return Result<void>::success();
  if(!terminal(e.visible))return Result<void>::failure({EBUSY,"RUNTIME","request not visibly terminal"});
  if(e.fault&&e.fault->retirement_unknown)return Result<void>::failure({EBUSY,"RUNTIME","operation failed; device/adapter retirement remains unknown"});
  {std::lock_guard<std::mutex>lock(budget->mutex);auto m=budget->models.find(e.request.model);
   if(m==budget->models.end()||!m->second.users)return Result<void>::failure({EPROTO,"RUNTIME","resident accounting lost"});}
  // No resident-budget mutex across adapter retirement or source destruction.
  // Both may release the last idle session lease without re-entering Runtime.
  auto r=port->retire_request(owner->fd(),e.request.request);if(!r)return r;
  {std::lock_guard<std::mutex>lock(budget->mutex);auto m=budget->models.find(e.request.model);budget->cpu-=e.request.cpu_bytes;
   if(!--m->second.users&&!m->second.pins){budget->cpu-=m->second.bytes;budget->models.erase(m);}}
  inputs-=e.request.input_bytes;e.released=true;e.request.source.reset();return Result<void>::success();
 }
 Result<void> retire(RequestKey k){std::lock_guard<std::mutex>lock(mutex);auto it=entries.find(key(k));if(it==entries.end())return Result<void>::failure({ESTALE,"RUNTIME","retained backing missing"});return retire_locked(it);}
};
// Called with Backing locked; non-null budget additionally holds its mutex.
// Four model/request associations keep the diagnostic safely below the RPC cap.
vxnn::Error quota_error(const Backing&b,const ResidentBudget*budget,const char*category,const char*dimension,uint64_t used,uint64_t limit,uint64_t requested,Digest model={}){
 std::string out=std::string("quota=")+dimension+" used="+std::to_string(used)+" limit="+std::to_string(limit)+" requested_bytes="+std::to_string(requested);
 auto hex=[](const Digest&d){const char*h="0123456789abcdef";std::string s;s.reserve(64);for(auto v:d){s+=h[v>>4];s+=h[v&15];}return s;};
 if(nonzero_digest(model))out+=" requested_model="+hex(model);
 if(budget){unsigned shown=0;for(const auto&m:budget->models){if(shown++==4){out+=" models_truncated=1";break;}out+=" model="+hex(m.first)+":users="+std::to_string(m.second.users)+":pins="+std::to_string(m.second.pins);}}
 unsigned shown=0;for(const auto&entry:b.entries){if(entry.second.released)continue;if(shown++==4){out+=" holders_truncated=1";break;}const auto&r=entry.second.request.request;out+=" holder="+r.client_id+"/"+r.request_id+":model="+hex(entry.second.request.model);}
 return {ENOSPC,category,std::move(out)};
}
bool same(const WorkRequest&a,const WorkRequest&b){return a.app==b.app&&a.model==b.model&&a.plan==b.plan&&a.input_sha==b.input_sha&&a.input_bytes==b.input_bytes&&a.queue_deadline_ns==b.queue_deadline_ns&&a.cpu_bytes==b.cpu_bytes&&a.resident_bytes==b.resident_bytes;}
}
struct PreparationLease::State {
 std::shared_ptr<PreparationBudget>budget;Key key;uint64_t serial=0,mode_sequence=0;std::optional<AppKind>app;
 ~State(){if(!serial)return;std::lock_guard<std::mutex>lock(budget->mutex);auto i=budget->slots.find(key);if(i==budget->slots.end()||i->second!=serial)std::terminate();budget->slots.erase(i);}
};
PreparationLease::PreparationLease(std::shared_ptr<State>s):state_(std::move(s)){}
PreparationLease::~PreparationLease()=default;
struct ResidentLease::State {
 std::shared_ptr<ResidentBudget>budget;Digest model{};uint64_t bytes=0;AppKind app=AppKind::NN;bool active=false;
 ~State(){
  if(!active)return;
  auto&b=*budget;std::lock_guard<std::mutex>lock(b.mutex);auto it=b.models.find(model);
  // A live private lease is created only after this exact slot is charged.
  // This CPU lifetime release never calls a device or guesses GPU retirement.
  if(it==b.models.end()||!it->second.pins||it->second.bytes!=bytes||it->second.app!=app)std::terminate();
  --it->second.pins;--b.leases;
  if(!it->second.pins&&!it->second.users){b.cpu-=it->second.bytes;b.models.erase(it);}
 }
};
ResidentLease::ResidentLease(std::shared_ptr<State>s):state_(std::move(s)){}
ResidentLease::~ResidentLease()=default;
Digest ResidentLease::model()const{return state_->model;}
uint64_t ResidentLease::bytes()const{return state_->bytes;}
AppKind ResidentLease::app()const{return state_->app;}
struct HostMemoryLease::State {
 std::shared_ptr<ResidentBudget>budget;uint64_t bytes=0;bool active=false;
 ~State(){if(!active)return;auto&b=*budget;std::lock_guard<std::mutex>lock(b.mutex);if(!b.host_leases||b.cpu<bytes+overhead)std::terminate();--b.host_leases;b.cpu-=bytes;}
};
HostMemoryLease::HostMemoryLease(std::shared_ptr<State>s):state_(std::move(s)){}
HostMemoryLease::~HostMemoryLease()=default;
uint64_t HostMemoryLease::bytes()const{std::lock_guard<std::mutex>lock(state_->budget->mutex);return state_->bytes;}
Result<void>HostMemoryLease::absorb(HostMemoryLease&other){
 auto&a=*state_;auto&b=*other.state_;
 if(&a==&b||a.budget!=b.budget)return Result<void>::failure({EXDEV,"RUNTIME_HOST_MEMORY","only distinct same-budget file credits can merge"});
 std::lock_guard<std::mutex>lock(a.budget->mutex);
 if(!a.active||!b.active||a.budget->host_leases<2||b.bytes>cpu_limit-a.bytes)return Result<void>::failure({EPROTO,"RUNTIME_HOST_MEMORY","admitted file credit accounting differs"});
 a.bytes+=b.bytes;b.bytes=0;b.active=false;--a.budget->host_leases;
 return Result<void>::success();
}
void HostMemoryLease::release_bytes(uint64_t bytes){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.budget->mutex);
 if(!s.active||bytes>s.bytes||bytes>s.budget->cpu-overhead)std::terminate();
 s.bytes-=bytes;s.budget->cpu-=bytes;
}
struct Runtime::State {
 detail::ExitRetention exit_retention;
 std::shared_ptr<Backing>backing;std::shared_ptr<PendingBudget>budget;Scheduler scheduler;std::unique_ptr<vxnn::CpuPool>pool;PersistWork persist;std::shared_ptr<ResultStore>results;std::mutex run_mutex;
 bool callback_active=false;std::thread::id callback_thread;
 SequentialModeGate modes;
 struct Pending {WorkItem work;ExecutionReceipt receipt;bool live=false,raw=false,retirement=false,ack_evidence=false,ack_saved=false,complete=false,consumed=false,state=false,published=false,control=false,fault=false,fault_saved=false,recovery=false,recovery_cancelled=false,recovery_release_attempted=false;}pending;
 explicit State(std::shared_ptr<Backing>b):backing(std::move(b)),budget(process_pending_budget()),scheduler(budget,{},backing->identity.provider_flags==4),modes(backing->identity.provider_flags==4){}
 Result<void> fail(vxnn::Error e,RuntimePhase phase){std::lock_guard<std::mutex>lock(backing->mutex);backing->phase=phase;if(!backing->first_error)backing->first_error=e;return Result<void>::failure(std::move(e));}
 void fault_diagnostic(bool recovered=false){
  auto&b=*backing;auto&r=pending.receipt;std::lock_guard<std::mutex>lock(b.mutex);auto i=b.entries.find(key(pending.work.request));if(i==b.entries.end())return;
  auto&e=i->second;RequestDiagnostic d;d.status=RequestStatus::FAILED;d.execution_error=r.execution_error;d.evidence_error=r.evidence_error;d.recovery_error=r.recovery_error;d.device_pending=pending.work.kind==WorkKind::GPU&&(!r.outcome.retired||!r.outcome.acknowledged);d.retirement_unknown=!recovered;d.recovered=recovered;d.resources_retained=!e.released;d.submitted_sequence=r.submitted_sequence;d.submitted_cookie=r.submitted_cookie;d.submitted_wire_cookie=r.submitted_wire_cookie;d.device_generation=r.device_generation;e.fault=std::move(d);e.visible=RequestStatus::FAILED;
 }
 Result<void> blocked(vxnn::Error e){if(!pending.receipt.evidence_error)pending.receipt.evidence_error=e;if(pending.fault)fault_diagnostic();return fail(std::move(e),RuntimePhase::DELIVERY_BLOCKED);}
 Result<void> deliver_fault(){
  auto&p=pending;
  if(!p.fault_saved){
   try{auto saved=persist(p.receipt,DeliveryStage::EXECUTION_ERROR);if(saved)p.fault_saved=true;else p.receipt.evidence_error=*saved.error;}
   catch(const std::bad_alloc&){p.receipt.evidence_error=vxnn::Error{ENOMEM,"EVIDENCE","failure receipt allocation"};}
   catch(...){p.receipt.evidence_error=vxnn::Error{EIO,"EVIDENCE","failure receipt persistence exception"};}
  }
  // Recording an error is never proof of retirement. Keep the receipt, owner,
  // scheduler reservation and adapter backing pinned even after durable save.
  fault_diagnostic();return fail(*p.receipt.execution_error,p.fault_saved?RuntimePhase::FAULTED:RuntimePhase::DELIVERY_BLOCKED);
 }
 Result<void> record_fault(vxnn::Error e){
  pending.fault=true;if(!pending.receipt.execution_error)pending.receipt.execution_error=e;
  {std::lock_guard<std::mutex>lock(backing->mutex);backing->phase=RuntimePhase::FAULTED;if(!backing->first_error)backing->first_error=e;}
  return deliver_fault();
 }
 Result<void> recovery_failure(vxnn::Error e){
  pending.receipt.recovery_error=e;pending.fault_saved=false;
  try{auto saved=persist(pending.receipt,DeliveryStage::EXECUTION_ERROR);if(saved)pending.fault_saved=true;else pending.receipt.evidence_error=*saved.error;}
  catch(...){pending.receipt.evidence_error=vxnn::Error{EIO,"EVIDENCE","late recovery error persistence exception"};}
  fault_diagnostic();return fail(std::move(e),pending.fault_saved?RuntimePhase::FAULTED:RuntimePhase::DELIVERY_BLOCKED);
 }
 void stamp(ExecutionReceipt&r){r.scope=backing->scope;r.clock_domain=backing->scope==vxnn::EvidenceScope::REAL_BOARD?vxnn::ClockDomain::LINUX_MONOTONIC:vxnn::ClockDomain::HOST_MONOTONIC;}
 void bind(ExecutionReceipt&r,const Backing::Entry&e){
  auto&i=r.binding;const auto&b=backing->identity;i.app=e.request.app;i.hardware=b.hardware_contract;i.bit=b.bit;i.xsa=b.xsa;i.compatible=b.compatible;i.device_generation=b.device_generation;i.provider_flags=b.provider_flags;
  i.model=e.request.model;i.plan=e.request.plan;i.input=e.request.input_sha;i.source=e.source_id;i.input_bytes=e.request.input_bytes;
 }
 Result<void> check_output(){
  if(!pending.receipt.output)return Result<void>::success();
  auto&b=*backing;std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(pending.work.request));auto&out=*pending.receipt.output;
  if(it==b.entries.end()||pending.work.work_index+1!=it->second.count||key(out.lease.producer)!=key(pending.work.request)||out.lease.app!=it->second.request.app||out.lease.hardware!=b.identity.hardware_contract||out.lease.model!=it->second.request.model||out.lease.plan!=it->second.request.plan||out.lease.scope!=b.scope)return Result<void>::failure({EPROTO,"RUNTIME","post-retirement output identity/scope differs; backing retained"});
  if(out.binding&&!(*out.binding==pending.receipt.binding))return Result<void>::failure({EPROTO,"RUNTIME","result binding differs from admitted context"});
  out.binding=pending.receipt.binding;
  return Result<void>::success();
 }
 Result<bool> queued_terminal(){
  auto&b=*backing;std::lock_guard<std::mutex>lock(b.mutex);
  for(auto&entry:b.entries){auto&e=entry.second;if(e.visible!=RequestStatus::QUEUED)continue;auto status=scheduler.status(e.request.request);if(!status)return Result<bool>::failure(*status.error);if(!terminal(*status.value))continue;
   auto error=scheduler.request_error(e.request.request);if(!error)return Result<bool>::failure(*error.error);
   Pending prepared;prepared.control=true;prepared.live=true;prepared.raw=true;prepared.complete=true;prepared.consumed=true;prepared.work.request=e.request.request;prepared.receipt.outcome.request=e.request.request;prepared.receipt.outcome.status=*status.value;prepared.receipt.outcome.result=*error.value;prepared.receipt.delivered_status=*status.value;stamp(prepared.receipt);bind(prepared.receipt,e);prepared.receipt.dispatch_begin_ns=now();prepared.receipt.dispatch_end_ns=prepared.receipt.dispatch_begin_ns;pending=std::move(prepared);return Result<bool>::success(true);
  }return Result<bool>::success(false);
 }
 Result<void> deliver(){
  auto&p=pending;auto&b=*backing;
  try{
   if(!p.raw){auto r=persist(p.receipt,DeliveryStage::RAW_RETIREMENT);if(!r)return blocked(*r.error);p.raw=true;}
   if(!p.retirement&&!p.control){
    auto&o=p.receipt.outcome;bool hardware=p.work.kind==WorkKind::GPU;
    if(!o.retired||(hardware&&!o.acknowledged)){
     p.receipt.retire_begin_ns=now();auto r=[&]()->Result<void>{
      try{return b.port->retire_completed(b.owner->fd(),p.work,p.receipt);}
      catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"RETIREMENT_EXCEPTION","retirement adapter allocation"});}
      catch(const std::exception&e){return Result<void>::failure({EIO,"RETIREMENT_EXCEPTION",e.what()});}
      catch(...){return Result<void>::failure({EIO,"RETIREMENT_EXCEPTION","non-standard retirement adapter exception"});}
     }();p.receipt.retire_end_ns=now();
     if(!r)return record_fault(*r.error);
     if(!o.retired||(hardware&&!o.acknowledged))return record_fault({EPROTO,"RUNTIME","retirement adapter supplied no proof"});
     p.ack_evidence=true;
    }p.retirement=true;
   }
   auto valid_output=check_output();if(!valid_output)return record_fault(*valid_output.error);
   if(p.ack_evidence&&!p.ack_saved){auto r=persist(p.receipt,DeliveryStage::ACK_RETIREMENT);if(!r)return blocked(*r.error);p.ack_saved=true;}
   if(!p.complete){auto r=scheduler.complete(p.receipt.outcome);if(!r)return fail(*r.error,r.error->code==ENOMEM?RuntimePhase::DELIVERY_BLOCKED:RuntimePhase::FAULTED);p.complete=true;}
   if(!p.consumed){auto r=scheduler.take_completion();if(!r)return fail(*r.error,RuntimePhase::DELIVERY_BLOCKED);if(key(r.value->request)!=key(p.work.request)||r.value->work_index!=p.work.work_index)return fail({EPROTO,"RUNTIME","unexpected completion; raw receipt retained"},RuntimePhase::FAULTED);p.consumed=true;}
   auto status=scheduler.status(p.work.request);if(!status)return fail(*status.error,RuntimePhase::FAULTED);p.receipt.delivered_status=*status.value;
   if(!p.state){auto r=persist(p.receipt,DeliveryStage::REQUEST_STATE);if(!r)return blocked(*r.error);p.state=true;}
   if(*status.value==RequestStatus::COMPLETED&&!p.published){
    if(!p.receipt.output)return fail({EPROTO,"RUNTIME","final work supplied no result"},RuntimePhase::FAULTED);
    auto r=results->publish(*p.receipt.output);if(!r)return blocked(*r.error);p.published=true;
   }
   {std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(p.work.request));if(it==b.entries.end()){b.phase=RuntimePhase::FAULTED;return Result<void>::failure({EPROTO,"RUNTIME","request backing missing"});}it->second.visible=*status.value;b.phase=*status.value==RequestStatus::FAILED&&!p.control?RuntimePhase::FAULTED:RuntimePhase::READY;}
   p=Pending{};return Result<void>::success();
  }catch(const std::bad_alloc&){return fail({ENOMEM,"RUNTIME","delivery allocation; never repeat execution"},RuntimePhase::DELIVERY_BLOCKED);}catch(...){return fail({EIO,"RUNTIME","evidence exception; original receipt retained"},RuntimePhase::DELIVERY_BLOCKED);}
 }
 Result<void>deliver_recovery(){
  auto&p=pending;auto&b=*backing;
  try{
   if(!p.raw){auto saved=persist(p.receipt,DeliveryStage::RAW_RETIREMENT);if(!saved)return blocked(*saved.error);p.raw=true;}
   if(!p.retirement){
    p.receipt.retire_begin_ns=now();auto retired=b.port->retire_completed(b.owner->fd(),p.work,p.receipt);p.receipt.retire_end_ns=now();
    if(!retired)return recovery_failure(*retired.error);
    if(!p.receipt.outcome.retired||!p.receipt.outcome.acknowledged)return recovery_failure({EPROTO,"RUNTIME_RECOVERY","late completion adapter supplied no ACK/retirement proof"});
    p.retirement=true;
   }
   if(!p.ack_saved){auto saved=persist(p.receipt,DeliveryStage::ACK_RETIREMENT);if(!saved)return blocked(*saved.error);p.ack_saved=true;}
   // The device's original OK/result0 proof stays intact. Retire that work in
   // the scheduler, then abandon any unexecuted remainder of this failed host
   // request. Application status remains FAILED and no result is published.
   if(!p.complete){auto done=scheduler.complete(p.receipt.outcome);if(!done)return recovery_failure(*done.error);p.complete=true;}
   if(!p.consumed){auto done=scheduler.take_completion();if(!done)return recovery_failure(*done.error);if(key(done.value->request)!=key(p.work.request)||done.value->work_index!=p.work.work_index)return recovery_failure({EPROTO,"RUNTIME_RECOVERY","late scheduler completion identity differs"});p.consumed=true;}
   if(!p.recovery_cancelled){auto cancelled=scheduler.cancel(p.work.request);if(!cancelled)return recovery_failure(*cancelled.error);p.recovery_cancelled=true;}
   p.receipt.delivered_status=RequestStatus::FAILED;
   if(!p.state){auto saved=persist(p.receipt,DeliveryStage::REQUEST_STATE);if(!saved)return blocked(*saved.error);p.state=true;}
   if(p.recovery_release_attempted)return recovery_failure({EALREADY,"RUNTIME_RECOVERY","failed adapter resource release is retained; never repeat an uncertain callback"});
   p.recovery_release_attempted=true;
   auto released=[&](){
    std::lock_guard<std::mutex>lock(b.mutex);auto i=b.entries.find(key(p.work.request));if(i==b.entries.end()||!i->second.fault)return Result<void>::failure({ESTALE,"RUNTIME_RECOVERY","failed request backing missing"});
    // Keep the pin and its release in one Backing critical section; a client
    // reap must never race between exposing proof and retiring this lifetime.
    auto&d=*i->second.fault;d.retirement_unknown=false;d.device_pending=false;d.recovered=true;
    auto retired=b.retire_locked(i);if(retired)d.resources_retained=false;else{d.retirement_unknown=true;d.recovered=false;}return retired;
   }();
   if(!released)return recovery_failure(*released.error);
   {std::lock_guard<std::mutex>lock(b.mutex);b.phase=RuntimePhase::FAULTED;}
   p=Pending{};return Result<void>::success();
  }catch(const std::bad_alloc&){return recovery_failure({ENOMEM,"RUNTIME_RECOVERY","late cleanup allocation; backing retained"});}
   catch(const std::exception&e){return recovery_failure({EIO,"RUNTIME_RECOVERY",e.what()});}
   catch(...){return recovery_failure({EIO,"RUNTIME_RECOVERY","late cleanup exception; backing retained"});}
 }
};
Result<std::shared_ptr<PreparationLease>>Runtime::reserve_preparation(const RequestKey&r,std::optional<AppKind> app){
 using R=Result<std::shared_ptr<PreparationLease>>;try{
  auto valid=[](const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});};if(!valid(r.boot_id)||!valid(r.client_id)||!valid(r.request_id))return R::failure({EINVAL,"RUNTIME_PREPARATION","bounded complete request identity required"});
  // All owning allocations precede locking; an unarmed failure cannot re-enter.
  auto hold=std::make_shared<PreparationLease::State>();auto&b=*state_->backing;hold->budget=b.preparations;hold->key=key(r);auto lease=std::shared_ptr<PreparationLease>(new PreparationLease(hold));std::string boot=r.boot_id;
  std::lock_guard<std::mutex>backing(b.mutex);std::lock_guard<std::mutex>slots(b.preparations->mutex);auto&p=*b.preparations;
  if(b.phase!=RuntimePhase::READY)return R::failure({ESHUTDOWN,"RUNTIME_PREPARATION","admission is closing or faulted"});
  if(b.identity.provider_flags==4){
   if(!app||!state_->modes.accepts(*app))return R::failure({EBUSY,"MODE","typed preparation must precede closure of its current mode"});
   hold->app=app;hold->mode_sequence=state_->modes.snapshot().sequence;
  }
  if(!p.boot.empty()&&p.boot!=r.boot_id)return R::failure({ESTALE,"RUNTIME_PREPARATION","boot epoch differs"});
  if(b.entries.count(hold->key)||p.slots.count(hold->key))return R::failure({EEXIST,"RUNTIME_PREPARATION","request already admitted/reserved"});
  if(state_->scheduler.stats().requests+p.slots.size()>=8)return R::failure({EAGAIN,"RUNTIME_PREPARATION","one shared preparation/execution queue8"});
  if(p.serial==UINT64_MAX)return R::failure({EOVERFLOW,"RUNTIME_PREPARATION","reservation serial exhausted"});
  auto next=p.serial+1;p.slots.emplace(hold->key,next);hold->serial=next;p.serial=next;if(p.boot.empty())p.boot=std::move(boot);return R::success(std::move(lease));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"RUNTIME_PREPARATION","reservation allocation; no slot committed"});}
}
Runtime::Runtime(std::shared_ptr<State>s):state_(std::move(s)){}
Runtime::~Runtime(){try{if(shutdown())return;}catch(...){/* Error construction can itself exhaust memory. */}state_->exit_retention.retain(state_);}
Result<std::shared_ptr<Runtime>> Runtime::start(std::shared_ptr<DeviceOwner>owner,const BackendIdentity&expected,vxnn::EvidenceScope scope,std::shared_ptr<ExecutionPort>port,PersistWork persist,PersistResult result){
 try{
  if(!owner||owner->fd()<0||!port||!persist||!result||!expected.device_generation||!nonzero_digest(expected.hardware_contract)||!nonzero_digest(expected.bit)||!nonzero_digest(expected.xsa)||!nonzero_digest(expected.compatible)||(expected.provider_flags!=1&&expected.provider_flags!=2&&expected.provider_flags!=4)||static_cast<unsigned>(scope)>static_cast<unsigned>(vxnn::EvidenceScope::REAL_BOARD))return Result<std::shared_ptr<Runtime>>::failure({EINVAL,"RUNTIME","bound startup owner/identity/callbacks required"});
  if(owner->mode()==BackendMode::LEGACY_COMPAT&&expected.provider_flags!=1)return Result<std::shared_ptr<Runtime>>::failure({EOPNOTSUPP,"RUNTIME","legacy compatibility is fixed NN only"});
  if(expected.provider_flags==4&&!s8_joint_platform(expected))return Result<std::shared_ptr<Runtime>>::failure({EXDEV,"RUNTIME","exact versioned S8 joint identity required"});
  auto actual=port->probe(owner->fd(),owner->mode());if(!actual)return Result<std::shared_ptr<Runtime>>::failure(*actual.error);auto&a=*actual.value;
  if(a.device_generation!=expected.device_generation||a.hardware_contract!=expected.hardware_contract||a.bit!=expected.bit||a.xsa!=expected.xsa||a.compatible!=expected.compatible||a.provider_flags!=expected.provider_flags)return Result<std::shared_ptr<Runtime>>::failure({EXDEV,"RUNTIME","actual read-only probe identity differs"});
  auto b=std::make_shared<Backing>();b->owner=std::move(owner);b->port=std::move(port);b->identity=a;b->scope=scope;
  auto backend=b->port->native_backend_snapshot();if(!backend)return Result<std::shared_ptr<Runtime>>::failure(*backend.error);b->native_backend=*backend.value;
  auto s=std::make_shared<State>(b);s->persist=std::move(persist);if(b->owner->mode()==BackendMode::COMPUTE_V2){s->pool=std::make_unique<vxnn::CpuPool>(4,32,s->budget);auto ready=s->pool->ready();if(!ready)return Result<std::shared_ptr<Runtime>>::failure(*ready.error);}
  s->results=std::make_shared<ResultStore>(std::move(result),[b](const ResultLease&l){return b->retire(l.producer);});
  return Result<std::shared_ptr<Runtime>>::success(std::shared_ptr<Runtime>(new Runtime(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<Runtime>>::failure({ENOMEM,"RUNTIME","startup allocation"});}catch(...){return Result<std::shared_ptr<Runtime>>::failure({EIO,"RUNTIME","startup/probe exception"});}
}
Result<RequestKey> Runtime::submit(const WorkRequest&r,const std::shared_ptr<PreparationLease>&preparation){
 auto&s=*state_;auto&b=*s.backing;std::lock_guard<std::mutex>lock(b.mutex);
 try{
  if(!r.source){return Result<RequestKey>::failure({EINVAL,"RUNTIME","trusted source required"});}
  auto k=key(r.request);auto source=r.source->identity();auto count=r.source->size();auto&p=*b.preparations;std::lock_guard<std::mutex>slots(p.mutex);
  if(preparation){auto h=preparation->state_;auto found=p.slots.find(k);if(!h||h->budget!=b.preparations||h->key!=k||!h->serial||found==p.slots.end()||found->second!=h->serial)return Result<RequestKey>::failure({ESTALE,"RUNTIME_PREPARATION","exact live owner/request reservation required"});}
  else if(p.slots.count(k))return Result<RequestKey>::failure({EBUSY,"RUNTIME_PREPARATION","request still reserved for preparation"});
  auto it=b.entries.find(k);
  if(it!=b.entries.end()){if(!same(it->second.request,r)||it->second.source_id!=source||it->second.count!=count)return Result<RequestKey>::failure({EEXIST,"RUNTIME","request identity/footprint changed"});return Result<RequestKey>::success(r.request);}
  if(b.phase!=RuntimePhase::READY)return Result<RequestKey>::failure({b.phase==RuntimePhase::CLOSING?ESHUTDOWN:EBUSY,"RUNTIME","admission blocked"});
  if(b.identity.provider_flags==4){
   if(!s.modes.accepts(r.app,bool(preparation)))return Result<RequestKey>::failure({EBUSY,"MODE","mode handoff closes new old-mode work; explicit target mode required"});
   if(preparation){const auto&h=*preparation->state_;const auto mode=s.modes.snapshot();const auto epoch=mode.sequence-(mode.switching?1u:0u);
    if(!h.app||*h.app!=r.app||h.mode_sequence!=epoch)return Result<RequestKey>::failure({ESTALE,"MODE","exact application and pre-closure mode reservation required"});
   }
  }
  if(!p.boot.empty()&&p.boot!=r.request.boot_id)return Result<RequestKey>::failure({ESTALE,"RUNTIME_PREPARATION","boot epoch differs"});
  if(s.scheduler.stats().requests+p.slots.size()-(preparation?1:0)>=8)return Result<RequestKey>::failure({EAGAIN,"RUNTIME_PREPARATION","one shared preparation/execution queue8"});
  if(!role_allowed(b.identity.provider_flags,r.app))return Result<RequestKey>::failure({EOPNOTSUPP,"RUNTIME","application belongs to another frozen provider"});
  if(b.owner->mode()==BackendMode::LEGACY_COMPAT&&(r.app!=AppKind::NN||count!=1))return Result<RequestKey>::failure({EOPNOTSUPP,"RUNTIME","legacy compatibility requires one NN aggregate"});
  if(r.cpu_bytes<r.input_bytes||r.cpu_bytes>cpu_limit||r.resident_bytes>cpu_limit)return Result<RequestKey>::failure({ENOSPC,"RUNTIME","invalid input/CPU/resident footprint bounds"});
  if(r.input_bytes>input_limit-b.inputs)return Result<RequestKey>::failure(quota_error(b,nullptr,"RUNTIME","input_bytes",b.inputs,input_limit,r.input_bytes,r.model));
  auto&budget=*b.budget;std::unique_lock<std::mutex>budget_lock(budget.mutex);
  auto m=budget.models.find(r.model);bool new_model=m==budget.models.end();
  if(!new_model&&(m->second.bytes!=r.resident_bytes||m->second.app!=r.app))return Result<RequestKey>::failure({ESTALE,"RUNTIME","resident footprint or application role differs"});
  if(new_model&&budget.models.size()>=64)return Result<RequestKey>::failure(quota_error(b,&budget,"RUNTIME","resident_groups",budget.models.size(),64,r.resident_bytes,r.model));
  if(new_model&&r.app==AppKind::NN&&nn_models(budget)>=2)return Result<RequestKey>::failure(quota_error(b,&budget,"RUNTIME","resident_nn",nn_models(budget),2,r.resident_bytes,r.model));
  uint64_t model_bytes=new_model?r.resident_bytes:0;if(r.cpu_bytes>cpu_limit-budget.cpu||model_bytes>cpu_limit-budget.cpu-r.cpu_bytes)return Result<RequestKey>::failure(quota_error(b,&budget,"RUNTIME","cpu_bytes",budget.cpu,cpu_limit,r.cpu_bytes+model_bytes,r.model));
  if(b.entries.size()>=256)return Result<RequestKey>::failure(quota_error(b,&budget,"RUNTIME","retained_requests",b.entries.size(),256,0,r.model));
  RequestKey response=r.request;std::string boot=r.request.boot_id;Backing::Entry e;e.request=r;e.source_id=source;e.count=count;WorkRequest admitted=r;admitted.source=std::make_shared<BoundSource>(r.source,b.identity.device_generation,b.owner->mode());
  if(new_model)m=budget.models.emplace(r.model,ResidentBudget::Model{r.resident_bytes,0,0,r.app}).first;
  try{b.entries.emplace(k,std::move(e));}catch(...){if(new_model)budget.models.erase(m);throw;}
  // Reserve atomically before metadata enters Scheduler. Source callbacks may
  // inspect/release already-admitted host storage, so never carry this budget
  // lock across Scheduler or source/adapter code. The provisional user also
  // prevents the last resident pin from removing this exact model meanwhile.
  ++m->second.users;budget.cpu+=r.cpu_bytes+model_bytes;budget_lock.unlock();
  auto rollback=[&]()noexcept{
   b.entries.erase(k); // source destruction also occurs OUTSIDE budget mutex
   std::lock_guard<std::mutex>restore(budget.mutex);auto found=budget.models.find(r.model);
   if(found==budget.models.end()||!found->second.users||budget.cpu<r.cpu_bytes+overhead)std::terminate();
   budget.cpu-=r.cpu_bytes;if(!--found->second.users&&!found->second.pins){budget.cpu-=found->second.bytes;budget.models.erase(found);}
  };
  struct Reservation{decltype(rollback)&undo;bool committed=false;~Reservation(){if(!committed)undo();}}reservation{rollback};
  auto accepted=s.scheduler.submit(admitted);if(!accepted)return Result<RequestKey>::failure(*accepted.error);
  if(preparation){p.slots.erase(k);preparation->state_->serial=0;}if(p.boot.empty())p.boot=std::move(boot);b.inputs+=r.input_bytes;reservation.committed=true;return Result<RequestKey>::success(std::move(response));
 }catch(const std::bad_alloc&){return Result<RequestKey>::failure({ENOMEM,"RUNTIME","admission allocation"});}catch(...){return Result<RequestKey>::failure({EIO,"RUNTIME","admission source exception"});}
}
Result<std::shared_ptr<ResidentLease>>Runtime::reserve_resident(Digest model,uint64_t bytes){
 return reserve_resident(model,bytes,AppKind::NN);
}
Result<std::shared_ptr<ResidentLease>>Runtime::reserve_resident(Digest model,uint64_t bytes,AppKind app){
 try{
  if(!nonzero_digest(model)||!bytes||bytes>cpu_limit)return Result<std::shared_ptr<ResidentLease>>::failure({EINVAL,"RUNTIME_RESIDENT","nonzero model and bounded lifetime bytes required"});
  // All owning/control-block allocation precedes accounting and the mutex.
  // An unarmed failure cannot run a recursively locking rollback destructor.
  auto s=std::make_shared<ResidentLease::State>();s->budget=state_->backing->budget;s->model=model;s->bytes=bytes;s->app=app;
  auto hold=std::shared_ptr<ResidentLease>(new ResidentLease(s));auto&b=*state_->backing;std::lock_guard<std::mutex>lock(b.mutex);
  if(b.phase!=RuntimePhase::READY)return Result<std::shared_ptr<ResidentLease>>::failure({b.phase==RuntimePhase::CLOSING?ESHUTDOWN:EBUSY,"RUNTIME_RESIDENT","session allocation admission closed"});
  if(!role_allowed(b.identity.provider_flags,app))return Result<std::shared_ptr<ResidentLease>>::failure({EOPNOTSUPP,"RUNTIME_RESIDENT","resident role belongs to another frozen provider"});
  auto&budget=*s->budget;std::lock_guard<std::mutex>budget_lock(budget.mutex);auto it=budget.models.find(model);bool fresh=it==budget.models.end();
  if(!fresh&&(it->second.bytes!=bytes||it->second.app!=app))return Result<std::shared_ptr<ResidentLease>>::failure({ESTALE,"RUNTIME_RESIDENT","model lifetime footprint or role differs"});
  if(budget.leases>=64)return Result<std::shared_ptr<ResidentLease>>::failure(quota_error(b,&budget,"RUNTIME_RESIDENT","resident_leases",budget.leases,64,bytes,model));
  if(fresh&&budget.models.size()>=64)return Result<std::shared_ptr<ResidentLease>>::failure(quota_error(b,&budget,"RUNTIME_RESIDENT","resident_groups",budget.models.size(),64,bytes,model));
  if(fresh&&app==AppKind::NN&&nn_models(budget)>=2)return Result<std::shared_ptr<ResidentLease>>::failure(quota_error(b,&budget,"RUNTIME_RESIDENT","resident_nn",nn_models(budget),2,bytes,model));
  if(fresh&&bytes>cpu_limit-budget.cpu)return Result<std::shared_ptr<ResidentLease>>::failure(quota_error(b,&budget,"RUNTIME_RESIDENT","cpu_bytes",budget.cpu,cpu_limit,bytes,model));
  if(fresh)it=budget.models.emplace(model,ResidentBudget::Model{bytes,0,0,app}).first;
  ++it->second.pins;++budget.leases;if(fresh)budget.cpu+=bytes;s->active=true;
  return Result<std::shared_ptr<ResidentLease>>::success(std::move(hold));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<ResidentLease>>::failure({ENOMEM,"RUNTIME_RESIDENT","session lifetime reservation allocation"});}
}
Result<void>Runtime::stage_display(PublishedResult p,PersistResult journal){
 try{
  auto&s=*state_;auto&b=*s.backing;{
   std::lock_guard<std::mutex>lock(b.mutex);
   if(!s.callback_active||s.callback_thread!=std::this_thread::get_id()||!s.pending.live||b.phase!=RuntimePhase::READY)return Result<void>::failure({EPERM,"RUNTIME_STAGE","active trusted owner callback required"});
   auto it=b.entries.find(key(s.pending.work.request));const auto&w=s.pending.work;
   if(it==b.entries.end()||w.kind==WorkKind::GPU||w.kind==WorkKind::LEGACY_EXCLUSIVE||w.work_index+1!=it->second.count)return Result<void>::failure({EPROTO,"RUNTIME_STAGE","only final checked non-GPU output may enter display stage"});
   const auto&binding=s.pending.receipt.binding;const auto&l=p.lease;
   if(key(l.producer)!=key(w.request)||l.app!=binding.app||l.hardware!=binding.hardware||l.model!=binding.model||l.plan!=binding.plan||l.scope!=b.scope||!l.lease_tokens.empty()||(p.binding&&!(*p.binding==binding)))return Result<void>::failure({EPROTO,"RUNTIME_STAGE","exact admitted identity/scope and private output backing required"});
   p.binding=binding;
  }
  // ResultStore callbacks may later retire through Backing. Do not enter the
  // store with the Backing mutex held (store -> backing is the release order).
  return s.results->stage_display(p,std::move(journal));
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"RUNTIME_STAGE","checked output staging allocation"});}
}
Result<std::shared_ptr<HostMemoryLease>>Runtime::reserve_host_memory(uint64_t bytes){
 using R=Result<std::shared_ptr<HostMemoryLease>>;try{
  if(!bytes||bytes>cpu_limit)return R::failure({EINVAL,"RUNTIME_HOST_MEMORY","nonzero bounded host bytes required"});
  // Owning allocations happen before accounting. An inactive rollback cannot
  // recursively lock the same budget, including control-block allocation OOM.
  auto state=std::make_shared<HostMemoryLease::State>();state->budget=state_->backing->budget;state->bytes=bytes;auto hold=std::shared_ptr<HostMemoryLease>(new HostMemoryLease(state));
  auto&b=*state_->backing;std::lock_guard<std::mutex>lock(b.mutex);if(b.phase!=RuntimePhase::READY)return R::failure({b.phase==RuntimePhase::CLOSING?ESHUTDOWN:EBUSY,"RUNTIME_HOST_MEMORY","common allocation admission closed"});
  auto&budget=*state->budget;std::lock_guard<std::mutex>budget_lock(budget.mutex);if(budget.host_leases>=64)return R::failure(quota_error(b,&budget,"RUNTIME_HOST_MEMORY","host_leases",budget.host_leases,64,bytes));
  if(bytes>cpu_limit-budget.cpu)return R::failure(quota_error(b,&budget,"RUNTIME_HOST_MEMORY","cpu_bytes",budget.cpu,cpu_limit,bytes));
  ++budget.host_leases;budget.cpu+=bytes;state->active=true;return R::success(std::move(hold));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"RUNTIME_HOST_MEMORY","host lifetime allocation"});}
}
bool Runtime::owns_host_memory(const std::shared_ptr<HostMemoryLease>&p)const{return p&&p->state_->active&&p->state_->budget==state_->backing->budget;}
std::shared_ptr<ResultStore>Runtime::trusted_results()const{return state_->results;}
bool Runtime::owns_resident(const std::shared_ptr<ResidentLease>&p)const{return p&&p->state_->active&&p->state_->budget==state_->backing->budget;}
bool Runtime::owns_native_backend(int fd,const UnifiedBackend*b)const{
 // Immutable startup roots only; retirement already holds Backing::mutex.
 // Do not re-enter it or probe a new device/epoch here.
 auto&root=*state_->backing;return b&&root.owner->fd()==fd&&root.native_backend.get()==b;
}
vxnn::EvidenceScope Runtime::evidence_scope()const{return state_->backing->scope;}
Result<RequestStatus> Runtime::status(RequestKey k)const{auto&b=*state_->backing;std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(k));if(it==b.entries.end())return Result<RequestStatus>::failure({ENOENT,"RUNTIME","unknown request"});return Result<RequestStatus>::success(it->second.visible);}
Result<RequestDiagnostic>Runtime::diagnostic(RequestKey k)const{
 auto&b=*state_->backing;std::lock_guard<std::mutex>lock(b.mutex);auto i=b.entries.find(key(k));if(i==b.entries.end())return Result<RequestDiagnostic>::failure({ENOENT,"RUNTIME","unknown request"});
 if(i->second.fault)return Result<RequestDiagnostic>::success(*i->second.fault);
 RequestDiagnostic d;d.status=i->second.visible;d.resources_retained=!i->second.released;d.device_generation=b.identity.device_generation;return Result<RequestDiagnostic>::success(std::move(d));
}
Result<void> Runtime::cancel(RequestKey k){auto&s=*state_;auto&b=*s.backing;std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(k));if(it==b.entries.end())return Result<void>::failure({ENOENT,"RUNTIME","unknown request"});auto r=s.scheduler.cancel(k);if(!r)return r;auto status=s.scheduler.status(k);if(!status)return Result<void>::failure(*status.error);if(!terminal(it->second.visible)){if(*status.value==RequestStatus::CANCEL_PENDING)it->second.visible=RequestStatus::CANCEL_PENDING;else if(it->second.visible==RequestStatus::QUEUED&&*status.value==RequestStatus::CANCELLED)it->second.visible=RequestStatus::CANCELLED;}return Result<void>::success();}
Result<void> Runtime::run_one(){
 auto&s=*state_;auto&b=*s.backing;std::unique_lock<std::mutex>pump(s.run_mutex,std::try_to_lock);if(!pump.owns_lock())return Result<void>::failure({EBUSY,"RUNTIME","single active execution pump"});
 {std::lock_guard<std::mutex>lock(b.mutex);if(b.phase!=RuntimePhase::READY||s.pending.live)return Result<void>::failure({EBUSY,"RUNTIME","execution blocked"});}
 try{
  static_assert(std::is_nothrow_move_assignable<ExecutionReceipt>::value,"prepared receipt handoff must not allocate");
  uint64_t dispatch_begin=now();auto w=[&](){
   // Match submit/cancel lock order: backing then scheduler. The prepare hook
   // must not acquire backing in the opposite order or call the scheduler.
   std::lock_guard<std::mutex>lock(b.mutex);Backing::Entry*selected=nullptr;
   auto ready=s.scheduler.dispatch_into(s.pending.work,[&](const WorkItem&item){
    auto it=b.entries.find(key(item.request));if(it==b.entries.end())return Result<void>::failure({ESTALE,"RUNTIME","lost backing before activation"});
    ExecutionReceipt prepared;prepared.outcome.request=item.request;prepared.outcome.node=item.node;prepared.outcome.work_index=item.work_index;prepared.dispatch_begin_ns=dispatch_begin;s.stamp(prepared);s.bind(prepared,it->second);s.pending.receipt=std::move(prepared);selected=&it->second;return Result<void>::success();
   });
   if(ready){s.pending.live=true;if(selected->visible!=RequestStatus::CANCEL_PENDING)selected->visible=RequestStatus::RUNNING;}return ready;
  }();
  if(!w){auto control=s.queued_terminal();if(!control)return Result<void>::failure(*control.error);if(*control.value)return s.deliver();return Result<void>::failure(*w.error);}auto&p=s.pending;p.receipt.dispatch_end_ns=now();
  p.receipt.execute_begin_ns=now();auto executed=[&](){
   struct CallbackGuard {
    State&s;explicit CallbackGuard(State&state):s(state){std::lock_guard<std::mutex>lock(s.backing->mutex);s.callback_thread=std::this_thread::get_id();s.callback_active=true;}
    ~CallbackGuard(){std::lock_guard<std::mutex>lock(s.backing->mutex);s.callback_active=false;s.callback_thread={};}
   }guard(s);
   return b.port->execute(b.owner->fd(),p.work,s.pool.get(),p.receipt);
  }();p.receipt.execute_end_ns=now();if(!executed)return s.record_fault(*executed.error);
  auto&o=p.receipt.outcome;if(key(o.request)!=key(p.work.request)||o.node!=p.work.node||o.work_index!=p.work.work_index)return s.record_fault({EPROTO,"RUNTIME","execution receipt identity differs"});
  if(b.scope!=vxnn::EvidenceScope::REAL_BOARD&&((o.completion&&(o.completion->cycles_valid||o.completion->raw_cycles))||(p.receipt.linux_detail&&p.receipt.linux_detail->cycles_valid)))return s.record_fault({EPROTO,"RUNTIME","offline receipt cannot claim physical GPU cycles"});
  if(p.receipt.output){bool invalid;{std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(p.work.request));auto&out=*p.receipt.output;invalid=it==b.entries.end()||p.work.work_index+1!=it->second.count||key(out.lease.producer)!=key(p.work.request)||out.lease.app!=it->second.request.app||out.lease.hardware!=b.identity.hardware_contract||out.lease.model!=it->second.request.model||out.lease.plan!=it->second.request.plan||out.lease.scope!=b.scope;}if(invalid)return s.record_fault({EPROTO,"RUNTIME","final output identity/scope differs; backing retained"});}
  return s.deliver();
 }catch(const std::bad_alloc&){if(s.pending.live)return s.record_fault({ENOMEM,"RUNTIME","execution allocation; owner/active backing retained"});return Result<void>::failure({ENOMEM,"RUNTIME","unexecuted dispatch allocation; safe to retry"});}
 catch(const std::exception&e){if(s.pending.live){if(s.pending.receipt.execute_begin_ns&&!s.pending.receipt.execute_end_ns)s.pending.receipt.execute_end_ns=now();return s.record_fault({EIO,"EXECUTION_EXCEPTION",e.what()});}return s.fail({EIO,"EXECUTION_EXCEPTION",e.what()},RuntimePhase::FAULTED);}
 catch(...){if(s.pending.live)return s.record_fault({EIO,"EXECUTION_EXCEPTION","non-standard exception; never resubmit"});return s.fail({EIO,"EXECUTION_EXCEPTION","dispatch non-standard exception"},RuntimePhase::FAULTED);}
}
Result<void> Runtime::retry_delivery(){auto&s=*state_;std::unique_lock<std::mutex>pump(s.run_mutex,std::try_to_lock);if(!pump.owns_lock())return Result<void>::failure({EBUSY,"RUNTIME","execution pump busy"});{std::lock_guard<std::mutex>lock(s.backing->mutex);if(s.backing->phase!=RuntimePhase::DELIVERY_BLOCKED||!s.pending.live)return Result<void>::failure({EALREADY,"RUNTIME","no pending evidence delivery"});}return s.pending.fault&&!s.pending.fault_saved?s.deliver_fault():s.pending.recovery?s.deliver_recovery():s.pending.fault?s.deliver_fault():s.deliver();}
Result<void>Runtime::recover_pending(uint64_t timeout){
 auto&s=*state_;auto&b=*s.backing;std::unique_lock<std::mutex>pump(s.run_mutex,std::try_to_lock);if(!pump.owns_lock())return Result<void>::failure({EBUSY,"RUNTIME_RECOVERY","execution pump busy"});
 if(!timeout||timeout>1000000000ULL)return Result<void>::failure({EINVAL,"RUNTIME_RECOVERY","late observation limit is (0,1s]"});
 if(!s.pending.live||!s.pending.fault||!s.pending.receipt.execution_error||s.pending.work.kind!=WorkKind::GPU)return Result<void>::failure({EALREADY,"RUNTIME_RECOVERY","no original failed GPU submission retained"});
 auto&p=s.pending;
 if(!p.fault_saved){auto saved=s.deliver_fault();if(!p.fault_saved)return saved;}
 if(!p.recovery){
  if(p.receipt.recovery_attempts==8)return Result<void>::failure({EOVERFLOW,"RUNTIME_RECOVERY","bounded late observation attempts exhausted; backing retained"});
  ++p.receipt.recovery_attempts;p.receipt.recovery_begin_ns=now();auto observed=b.port->recover_completion(b.owner->fd(),p.work,p.receipt,timeout);p.receipt.recovery_end_ns=now();
  if(!observed)return s.recovery_failure(*observed.error);
  if(!matching_submission(p.receipt,p.receipt.submitted_cookie)||!p.receipt.linux_detail||p.receipt.linux_detail->state!=VXL_V2_OK||p.receipt.linux_detail->result||p.receipt.outcome.status!=RequestStatus::COMPLETED||p.receipt.outcome.result)return s.recovery_failure({EPROTO,"RUNTIME_RECOVERY","late observation lacks original successful completion proof"});
  if(b.scope!=vxnn::EvidenceScope::REAL_BOARD&&((p.receipt.outcome.completion&&(p.receipt.outcome.completion->cycles_valid||p.receipt.outcome.completion->raw_cycles))||p.receipt.linux_detail->cycles_valid))return s.recovery_failure({EPROTO,"RUNTIME_RECOVERY","offline late receipt cannot claim physical GPU cycles"});
  p.recovery=true;p.raw=false;p.retirement=false;p.ack_saved=false;p.complete=false;p.consumed=false;p.state=false;
 }
 return s.deliver_recovery();
}
Result<PublishedResult> Runtime::take_result(RequestKey k)const{return state_->results->take_result(std::move(k));}
Result<ConsumerLease> Runtime::retain_result(RequestKey k,Consumer c){return state_->results->retain_result(std::move(k),c);}
Result<void> Runtime::release_result(const ConsumerLease&l,bool retired){return state_->results->release_result(l,retired);}
Result<void> Runtime::reap_result(RequestKey k){
 auto&s=*state_;auto&b=*s.backing;bool has_output=false;
 {std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(k));if(it==b.entries.end())return Result<void>::failure({ENOENT,"RUNTIME","unknown request"});if(!terminal(it->second.visible))return Result<void>::failure({EBUSY,"RUNTIME","request active or delivery not durable"});has_output=it->second.visible==RequestStatus::COMPLETED;}
 if(has_output){auto r=s.results->reap_result(k);if(!r&&r.error->code!=ENOENT)return r;}
 std::lock_guard<std::mutex>lock(b.mutex);auto it=b.entries.find(key(k));if(it==b.entries.end())return Result<void>::failure({ESTALE,"RUNTIME","request already forgotten"});if(!has_output&&!it->second.released){auto r=b.retire_locked(it);if(!r)return r;}if(!it->second.released)return Result<void>::success();auto forgotten=s.scheduler.forget(k);if(!forgotten)return forgotten;b.entries.erase(it);return Result<void>::success();
}
Result<void> Runtime::shutdown(){
 auto&s=*state_;std::unique_lock<std::mutex>pump(s.run_mutex,std::try_to_lock);if(!pump.owns_lock())return Result<void>::failure({EBUSY,"RUNTIME","execution still active"});if(s.pending.live)return Result<void>::failure({EBUSY,"RUNTIME","undelivered or uncertain execution retained"});{auto&b=*s.backing;std::lock_guard<std::mutex>lock(b.mutex);std::lock_guard<std::mutex>slots(b.preparations->mutex);if(!b.preparations->slots.empty())return Result<void>::failure({EBUSY,"RUNTIME_PREPARATION","preparation owner has not quiesced"});}auto stopped=s.scheduler.shutdown();if(!stopped)return stopped;
 {auto&b=*s.backing;std::lock_guard<std::mutex>lock(b.mutex);b.phase=RuntimePhase::CLOSING;for(auto it=b.entries.begin();it!=b.entries.end();++it){if(!terminal(it->second.visible))it->second.visible=RequestStatus::CANCELLED;if(it->second.visible!=RequestStatus::COMPLETED){auto r=b.retire_locked(it);if(!r)return r;}}}
 return s.pool?s.pool->shutdown():Result<void>::success();
}
Result<ModeSnapshot>Runtime::request_mode(AppKind app){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.backing->mutex);
 if(s.backing->phase!=RuntimePhase::READY)return Result<ModeSnapshot>::failure({EBUSY,"MODE","faulted or closing execution retains original mode"});
 return s.modes.request(app,now());
}
Result<bool>Runtime::advance_mode(){
 auto&s=*state_;auto&b=*s.backing;std::unique_lock<std::mutex>pump(s.run_mutex,std::try_to_lock);
 if(!pump.owns_lock())return Result<bool>::success(false);
 std::lock_guard<std::mutex>lock(b.mutex);std::lock_guard<std::mutex>slots(b.preparations->mutex);
 if(b.phase!=RuntimePhase::READY||s.pending.live)return Result<bool>::success(false);
 const auto count=s.scheduler.stats();bool drained=b.preparations->slots.empty()&&!count.requests&&!count.active_gpu&&!count.active_cpu&&!count.pending&&!count.completions&&!count.reserved_completions;
 for(const auto&entry:b.entries)if(!terminal(entry.second.visible))drained=false;
 if(!count.active_gpu&&!count.active_cpu&&!count.completions&&!count.reserved_completions&&s.modes.snapshot().switching){
  // Cancelled/failed sources may still own DATA and the fixed CODE window.
  // Their terminal label is insufficient: invoke original adapter retirement
  // before waiting for queued successors: their ready() may need this owner's
  // CODE lease released. DP-backed completed results stay alive.
  for(auto it=b.entries.begin();it!=b.entries.end();++it)if(terminal(it->second.visible)&&it->second.visible!=RequestStatus::COMPLETED&&!it->second.released){auto r=b.retire_locked(it);if(!r)return Result<bool>::failure(*r.error);}
 }
 // ResultStore's DP front lease deliberately remains alive. GPU source
 // retirement/ACK and program release already precede durable COMPLETED.
 return s.modes.advance(drained,now());
}
bool Runtime::mode_allows(AppKind app)const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.backing->mutex);return s.modes.accepts(app);}
ModeSnapshot Runtime::mode_snapshot()const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.backing->mutex);return s.modes.snapshot();}
RuntimeStats Runtime::stats()const{
 auto&s=*state_;auto&b=*s.backing;std::lock_guard<std::mutex>lock(b.mutex);std::lock_guard<std::mutex>slots(b.preparations->mutex);
 RuntimeStats out;out.phase=b.phase;out.input_bytes=b.inputs;out.preparing_requests=uint32_t(b.preparations->slots.size());out.scheduler=s.scheduler.stats();
 // Scheduler may enter source readiness -> evidence/file -> host budget.
 // Snapshot each domain without introducing the reverse budget -> Scheduler.
 {auto&budget=*b.budget;std::lock_guard<std::mutex>budget_lock(budget.mutex);out.cpu_bytes=budget.cpu;out.resident_models=budget.models.size();out.resident_nn_models=uint32_t(nn_models(budget));out.resident_leases=budget.leases;}
 return out;
}
Result<void>Runtime::drain_shutdown_results(){
 auto&s=*state_;auto&b=*s.backing;std::unique_lock<std::mutex>pump(s.run_mutex,std::try_to_lock);
 if(!pump.owns_lock())return Result<void>::failure({EBUSY,"RUNTIME_DRAIN","execution still active"});
 try{
  std::vector<RequestKey>keys;
  {std::lock_guard<std::mutex>lock(b.mutex);if(b.phase!=RuntimePhase::CLOSING||s.pending.live)return Result<void>::failure({EBUSY,"RUNTIME_DRAIN","successful Runtime shutdown required"});for(const auto&entry:b.entries)keys.push_back(entry.second.request.request);}
  auto held=s.results->stats();if(held.consumer_leases||held.staged_records||held.pinned_retire_failures)return Result<void>::failure({EBUSY,"RUNTIME_DRAIN","consumer, private stage or failed retirement still held"});
  // No Backing mutex across ResultStore callbacks: collection calls Backing.
  for(const auto&k:keys){auto r=reap_result(k);if(!r)return r;}
  held=s.results->stats();std::lock_guard<std::mutex>lock(b.mutex);
  if(!b.entries.empty()||held.records||held.consumer_leases||held.staged_records)return Result<void>::failure({EBUSY,"RUNTIME_DRAIN","result backing remains retained"});
  return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"RUNTIME_DRAIN","close snapshot allocation; backing retained"});}
}
BackendIdentity Runtime::identity()const{auto&b=*state_->backing;std::lock_guard<std::mutex>lock(b.mutex);return b.identity;}
Result<vxnn::Error> Runtime::first_error()const{auto&b=*state_->backing;std::lock_guard<std::mutex>lock(b.mutex);if(!b.first_error)return Result<vxnn::Error>::failure({ENOENT,"RUNTIME","no first error"});return Result<vxnn::Error>::success(*b.first_error);}
}
