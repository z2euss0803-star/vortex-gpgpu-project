#pragma once
#include <vxunified/backend.hpp>
#include <vxunified/exit_retention.hpp>
#include <mutex>
namespace vxunified {
struct P8SessionKey {uint64_t total_bytes=0,mode_sequence=0;Digest layout{},statics{};};
struct P8SessionLease {BufferRef root{};uint64_t serial=0;bool root_reused=false,static_reuse=false;};
struct P8SessionProof {uint64_t completed_gpu=0;bool acknowledged=false,guards=false,durable_output=false,presented_retired=false,program_released=false;};
struct P8SessionStats {uint64_t allocations=0,root_reuses=0,static_reuses=0,full_initializations=0,releases=0;bool busy=false,fault=false;};
// Internal FAST-only single-root owner. No GPU work, ACK, guards or presentation
// is synthesized here: request.cpp derives the proof from the original frame.
class P8FrameSession {
 struct State {
  mutable std::mutex mutex;detail::ExitRetention retention;
  std::shared_ptr<UnifiedBackend>backend;BackendIdentity hardware;
  std::optional<BufferRef>root;P8SessionKey key{};RequestKey owner;
  uint64_t serial=0;bool valid=false,initialized=false,static_reuse=false;
  P8SessionStats counts;std::optional<vxnn::Error>fault;
 };
 std::shared_ptr<State>s_;
 static bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
 static bool identity(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags;}
 static vxnn::Error error(int n,const char*m){return {n,"P8_SESSION",m};}
 static Result<void>failed(State&s,vxnn::Error e){if(!s.fault)s.fault=std::move(e);s.counts.fault=true;return Result<void>::failure(*s.fault);}
 static Result<void>live(State&s){
  if(s.fault)return Result<void>::failure(*s.fault);
  if(!s.backend||!s.hardware.device_generation||!s.backend->fast_runtime())return failed(s,error(EOPNOTSUPP,"FAST-only immutable integrity policy required"));
  auto actual=s.backend->identity();if(!actual)return failed(s,*actual.error);
  if(!identity(*actual.value,s.hardware))return failed(s,error(ESTALE,"backend hardware/device epoch changed; root retained"));
  return Result<void>::success();
 }
 static Result<void>ticket(State&s,uint64_t serial){if(s.fault)return Result<void>::failure(*s.fault);if(!s.counts.busy||!serial||serial!=s.serial)return failed(s,error(ESTALE,"exact active frame lease required"));return Result<void>::success();}
public:
 P8FrameSession(std::shared_ptr<UnifiedBackend>backend,BackendIdentity hardware):s_(std::make_shared<State>()){s_->backend=std::move(backend);s_->hardware=std::move(hardware);}
 ~P8FrameSession(){try{if(!close())s_->retention.retain(s_);}catch(...){s_->retention.retain(s_);}}
 P8FrameSession(const P8FrameSession&)=delete;P8FrameSession&operator=(const P8FrameSession&)=delete;
 Result<bool>ready(const RequestKey&owner)const{auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);if(s.fault)return Result<bool>::failure(*s.fault);return Result<bool>::success(!s.counts.busy||same(owner,s.owner));}
 Result<P8SessionLease>acquire(const RequestKey&owner,const P8SessionKey&key){
  auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);if(s.fault)return Result<P8SessionLease>::failure(*s.fault);
  if(s.counts.busy)return Result<P8SessionLease>::failure(error(EBUSY,"inflight frame owns cached root"));
  if(owner.boot_id.empty()||owner.client_id.empty()||owner.request_id.empty()||!key.total_bytes||key.total_bytes>(64ULL<<20)||!nonzero_digest(key.layout)||!nonzero_digest(key.statics))return Result<P8SessionLease>::failure(error(EINVAL,"bounded physical layout and actual static content identity required"));
  auto checked=live(s);if(!checked)return Result<P8SessionLease>::failure(*checked.error);
  if(s.serial==UINT64_MAX){auto e=failed(s,error(EOVERFLOW,"frame lease serial never wraps"));return Result<P8SessionLease>::failure(*e.error);}
  RequestKey copy=owner;bool reused=bool(s.root);
  if(s.root&&s.root->bytes!=key.total_bytes){auto released=s.backend->release_buffer(*s.root);if(!released){failed(s,*released.error);return Result<P8SessionLease>::failure(*s.fault);}s.root.reset();s.valid=false;reused=false;++s.counts.releases;}
  if(!s.root){auto allocated=s.backend->allocate(key.total_bytes,vxnn::AddressDomain::PL,3);if(!allocated){failed(s,*allocated.error);return Result<P8SessionLease>::failure(*s.fault);}s.root=*allocated.value;++s.counts.allocations;
   if(!s.root->handle||!s.root->object_generation||s.root->offset||s.root->bytes!=key.total_bytes||s.root->access!=3||s.root->device_generation!=s.hardware.device_generation){failed(s,error(EPROTO,"allocated whole DATA root differs"));return Result<P8SessionLease>::failure(*s.fault);}
  }
  const bool statics=reused&&s.valid&&s.key.total_bytes==key.total_bytes&&s.key.mode_sequence==key.mode_sequence&&s.key.layout==key.layout&&s.key.statics==key.statics;
  s.owner=std::move(copy);s.key=key;s.static_reuse=statics;s.initialized=false;s.counts.busy=true;++s.serial;if(reused)++s.counts.root_reuses;
  return Result<P8SessionLease>::success({*s.root,s.serial,reused,statics});
 }
 Result<void>initialized(uint64_t serial){auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);auto t=ticket(s,serial);if(!t)return t;if(s.initialized)return failed(s,error(ESTALE,"materialization cannot commit twice"));s.initialized=true;s.valid=true;if(s.static_reuse)++s.counts.static_reuses;else ++s.counts.full_initializations;return Result<void>::success();}
 Result<void>retire(uint64_t serial,const P8SessionProof&p){auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);auto t=ticket(s,serial);if(!t)return t;
  if(!s.initialized||p.completed_gpu!=2||!p.acknowledged||!p.guards||!p.durable_output||!p.presented_retired||!p.program_released)return failed(s,error(EPROTO,"original two ACKs guards durable output presentation and program retirement required"));
  s.counts.busy=false;s.owner={};return Result<void>::success();
 }
 // Cancellation may release a proven inactive/ACKed root, never cache it as a
 // successfully displayed frame. Ambiguous failure remains sticky and pinned.
 Result<void>discard(uint64_t serial,bool acknowledged_inactive){auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);auto t=ticket(s,serial);if(!t)return t;if(!acknowledged_inactive)return failed(s,error(EBUSY,"cancelled root retirement unproven"));auto r=s.backend->release_buffer(*s.root);if(!r)return failed(s,*r.error);s.root.reset();s.valid=false;s.counts.busy=false;s.owner={};++s.counts.releases;return Result<void>::success();}
 Result<void>poison(uint64_t serial,vxnn::Error e){auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);if(s.fault)return Result<void>::failure(*s.fault);if(serial&&serial!=s.serial)return failed(s,error(ESTALE,"fault lease identity differs"));return failed(s,std::move(e));}
 Result<void>close(){auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);if(s.fault)return Result<void>::failure(*s.fault);if(s.counts.busy)return Result<void>::failure(error(EBUSY,"inflight cached root retained"));if(!s.root)return Result<void>::success();auto checked=live(s);if(!checked)return checked;auto r=s.backend->release_buffer(*s.root);if(!r)return failed(s,*r.error);s.root.reset();s.valid=false;++s.counts.releases;return Result<void>::success();}
 P8SessionStats stats()const{auto&s=*s_;std::lock_guard<std::mutex>lock(s.mutex);return s.counts;}
};
}
