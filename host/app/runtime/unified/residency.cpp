#include <vxunified/residency.hpp>
#include <vxl/v2/jobs.h>
#include <mutex>
namespace vxunified {
namespace {
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
bool valid(const RequestKey&r){auto id=[](const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});};return id(r.boot_id)&&id(r.client_id)&&id(r.request_id);}
bool same(const ProgramRef&a,const ProgramRef&b){return a.handle==b.handle&&a.program_generation==b.program_generation&&a.device_generation==b.device_generation&&a.program_sha==b.program_sha;}
bool same(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags;}
}
struct ProgramResidency::Impl {
 mutable std::mutex mutex;std::shared_ptr<const InstalledCatalog>catalog;std::shared_ptr<UnifiedBackend>backend;
 std::optional<ResidencyLease>owner;std::optional<ProgramRef>cached;ResidencyStats receipt;uint64_t serial=0,last_switch_sequence=0;
 vxnn::Error fault(vxnn::Error e){receipt.faulted=true;if(!receipt.first_error)receipt.first_error=std::move(e);return *receipt.first_error;}
 Result<CatalogBinding>binding(const RequestKey&r,const ProgramKey&k)const{
  if(receipt.first_error)return Result<CatalogBinding>::failure(*receipt.first_error);
  if(receipt.closed)return Result<CatalogBinding>::failure({ESHUTDOWN,"RESIDENCY","closed owner"});
  if(!valid(r))return Result<CatalogBinding>::failure({EINVAL,"RESIDENCY","bounded request identity"});
  return catalog->resolve(k);
 }
 Result<void>evict(){
  if(!cached)return Result<void>::success();
  auto old=*cached;++receipt.evictions;auto released=backend->release_program(old);
  if(!released)return Result<void>::failure(fault(*released.error));
  cached.reset();receipt.cached=false;return Result<void>::success();
 }
};
ProgramResidency::ProgramResidency(std::unique_ptr<Impl>p):impl_(std::move(p)){}
// No implicit backend release/retry. The service retains its sole fd and all
// uncertain backing on failure; destruction is not a retirement proof.
ProgramResidency::~ProgramResidency()=default;
Result<bool>ProgramResidency::ready_switch(const ResidencyLease&lease,const ProgramKey&key)const{
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto bound=s.binding(lease.request,key);if(!bound)return Result<bool>::failure(*bound.error);
 if(!s.owner||!s.cached||s.owner->serial!=lease.serial||!same(s.owner->request,lease.request)||!same(s.owner->program,lease.program)||!same(*s.cached,lease.program))return Result<bool>::failure({ESTALE,"RESIDENCY_SWITCH","live owner/program/serial differs"});
 return Result<bool>::success(true);
}
Result<ResidencyLease>ProgramResidency::switch_after_ack(const ResidencyLease&lease,const ProgramKey&key,const WorkOutcome&o,const vxl_v2_completion&d){
 using R=Result<ResidencyLease>;auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto bound=s.binding(lease.request,key);if(!bound)return R::failure(*bound.error);
 if(!s.owner||!s.cached||s.owner->serial!=lease.serial||!same(s.owner->request,lease.request)||!same(s.owner->program,lease.program)||!same(*s.cached,lease.program))return R::failure({ESTALE,"RESIDENCY_SWITCH","exact live request lease required"});
 if(!same(o.request,lease.request)||o.status!=RequestStatus::COMPLETED||o.result||!o.retired||!o.acknowledged||o.legacy||!o.completion)return R::failure({EBUSY,"RESIDENCY_SWITCH","matching successful GPU ACK required"});
 const auto&c=*o.completion;
 if(!d.sequence||!d.cookie||d.sequence<=s.last_switch_sequence||c.sequence!=d.sequence||!c.cookie||!c.wire_cookie||c.wire_cookie!=d.cookie||c.status!=VXL_V2_OK||c.result||d.state!=VXL_V2_OK||d.result||d.device_generation!=lease.program.device_generation||d.program_handle!=lease.program.handle||d.program_generation!=lease.program.program_generation||d.errors||d.pending_w||d.pending_r||d.core_mask!=15||!(d.status&2u)||(d.status&4u))return R::failure({ESTALE,"RESIDENCY_SWITCH","original completion identity/status/drain differs"});
 if(!d.start_ns||d.start_ns<d.submit_ns||d.done_seen_ns<d.start_ns||d.ack_ns<d.done_seen_ns||d.complete_ns<d.ack_ns||!d.complete_ns||c.cycles_valid!=bool(d.cycles_valid)||(c.cycles_valid?(!c.raw_cycles||*c.raw_cycles!=d.elapsed_raw||!d.elapsed_raw):bool(c.raw_cycles)))return R::failure({EPROTO,"RESIDENCY_SWITCH","original completion clocks/counter classification differs"});
 if(s.cached->program_sha==bound.value->entry_id)return R::failure({EALREADY,"RESIDENCY_SWITCH","same program needs no replacement"});
 if(s.serial==UINT64_MAX)return R::failure({EOVERFLOW,"RESIDENCY_SWITCH","lease serial exhausted"});
 try{
  // Allocate request string copies BEFORE loader side effects. DATA is never
  // released; only the exact retired program's private reservations change.
  ResidencyLease replacement=lease;replacement.serial=s.serial+1;replacement.program={};replacement.program.program_sha=bound.value->entry_id;
  auto evicted=s.evict();if(!evicted)return R::failure(*evicted.error); // kernel independently checks active/pins
  s.owner=std::move(replacement);++s.serial;s.last_switch_sequence=d.sequence;
  ++s.receipt.loads;auto loaded=s.backend->program_load(bound.value->entry_id);
  if(loaded){s.cached=*loaded.value;s.owner->program=*loaded.value;s.receipt.cached=true;}
  if(!loaded)return R::failure(s.fault(*loaded.error));
  const auto&p=*loaded.value;
  if(!p.handle||!p.program_generation||p.device_generation!=bound.value->device_generation||p.program_sha!=bound.value->entry_id)return R::failure(s.fault({ESTALE,"RESIDENCY_SWITCH","new program identity/epoch differs; ownership retained"}));
  return R::success(*s.owner);
 }catch(const std::bad_alloc&){return R::failure(s.fault({ENOMEM,"RESIDENCY_SWITCH","metadata allocation; ownership retained"}));}
 catch(const std::exception&e){return R::failure(s.fault({EIO,"RESIDENCY_SWITCH",e.what()}));}
 catch(...){return R::failure(s.fault({EIO,"RESIDENCY_SWITCH","unknown loader exception; ownership retained"}));}
}
Result<std::shared_ptr<ProgramResidency>>ProgramResidency::create(std::shared_ptr<const InstalledCatalog>c,std::shared_ptr<UnifiedBackend>b){
 using R=Result<std::shared_ptr<ProgramResidency>>;
 if(!c||!b)return R::failure({EINVAL,"RESIDENCY","trusted catalog and borrowed owner backend required"});
 try{
  auto id=b->identity();if(!id)return R::failure(*id.error);
  if(!same(*id.value,c->identity()))return R::failure({EXDEV,"RESIDENCY","backend/catalog identity mismatch"});
  auto caps=b->capabilities();if(!caps)return R::failure(*caps.error);
  if(caps.value->contexts!=1||caps.value->preempt||!caps.value->max_programs)return R::failure({EOPNOTSUPP,"RESIDENCY","single nonpreemptive context required"});
  auto p=std::make_unique<Impl>();p->catalog=std::move(c);p->backend=std::move(b);
  return R::success(std::shared_ptr<ProgramResidency>(new ProgramResidency(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"RESIDENCY","metadata allocation"});}
}
Result<bool>ProgramResidency::ready(const RequestKey&r,const ProgramKey&k)const{
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto b=s.binding(r,k);if(!b)return Result<bool>::failure(*b.error);
 return Result<bool>::success(!s.owner||(same(s.owner->request,r)&&s.owner->program.program_sha==b.value->entry_id));
}
Result<ResidencyLease>ProgramResidency::acquire(const RequestKey&r,const ProgramKey&k){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto b=s.binding(r,k);if(!b)return Result<ResidencyLease>::failure(*b.error);
 if(s.owner){if(same(s.owner->request,r)&&s.owner->program.program_sha==b.value->entry_id)return Result<ResidencyLease>::success(*s.owner);return Result<ResidencyLease>::failure({EAGAIN,"RESIDENCY","another live request owns fixed program and DATA reservations"});}
 if(s.serial==UINT64_MAX)return Result<ResidencyLease>::failure({EOVERFLOW,"RESIDENCY","lease serial exhausted"});
 try{
  // Copy all allocating request metadata before loader side effects. Retain
  // owner on any ambiguous operation or post-load validation failure.
  ResidencyLease lease;lease.request=r;lease.serial=++s.serial;lease.program.program_sha=b.value->entry_id;s.owner=std::move(lease);s.receipt.owned=true;
  if(s.cached&&s.cached->program_sha!=b.value->entry_id){auto evicted=s.evict();if(!evicted)return Result<ResidencyLease>::failure(*evicted.error);}
  if(!s.cached){
   ++s.receipt.loads;auto loaded=s.backend->program_load(b.value->entry_id);
   if(loaded){s.cached=*loaded.value;s.owner->program=*loaded.value;s.receipt.cached=true;}
   if(!loaded)return Result<ResidencyLease>::failure(s.fault(*loaded.error));
   const auto&p=*loaded.value;
   if(!p.handle||!p.program_generation||p.device_generation!=b.value->device_generation||p.program_sha!=b.value->entry_id)return Result<ResidencyLease>::failure(s.fault({ESTALE,"RESIDENCY","returned program identity/epoch differs; backing retained"}));
  }
  s.owner->program=*s.cached;return Result<ResidencyLease>::success(*s.owner);
 }catch(const std::bad_alloc&){return Result<ResidencyLease>::failure(s.fault({ENOMEM,"RESIDENCY","allocation; ownership retained"}));}
 catch(const std::exception&e){return Result<ResidencyLease>::failure(s.fault({EIO,"RESIDENCY",e.what()}));}
 catch(...){return Result<ResidencyLease>::failure(s.fault({EIO,"RESIDENCY","unknown loader exception"}));}
}
Result<void>ProgramResidency::release(const ResidencyLease&lease,bool retired){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);
 if(!s.owner||s.owner->serial!=lease.serial||!same(s.owner->request,lease.request)||!same(s.owner->program,lease.program))return Result<void>::failure({ESTALE,"RESIDENCY","lease identity differs"});
 if(!retired)return Result<void>::failure({EBUSY,"RESIDENCY","device backing/consumers not retired"});
 s.owner.reset();s.receipt.owned=false;return Result<void>::success();
}
Result<void>ProgramResidency::shutdown(){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);
 if(s.owner)return Result<void>::failure({EBUSY,"RESIDENCY","live DATA owner retained"});
 if(s.receipt.closed)return Result<void>::success();
 try{auto evicted=s.evict();if(!evicted)return evicted;s.receipt.closed=true;return Result<void>::success();}
 catch(const std::bad_alloc&){return Result<void>::failure(s.fault({ENOMEM,"RESIDENCY","shutdown metadata"}));}
 catch(...){return Result<void>::failure(s.fault({EIO,"RESIDENCY","release exception; retained"}));}
}
const BackendIdentity&ProgramResidency::identity()const{return impl_->catalog->identity();}
std::shared_ptr<const InstalledCatalog>ProgramResidency::catalog()const{return impl_->catalog;}
std::shared_ptr<UnifiedBackend>ProgramResidency::borrowed_backend()const{return impl_->backend;}
ResidencyStats ProgramResidency::stats()const{auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);return s.receipt;}
}
