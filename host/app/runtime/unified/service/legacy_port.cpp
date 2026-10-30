#include <vxunified/service.hpp>
#include <vxl/core.h>
#include <vxl/hunet.h>
#include <mutex>
#include <time.h>
namespace vxunified {
namespace {
Result<void>denied(int code,const char*why){return Result<void>::failure({code,"LEGACY_PORT",why});}
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
Digest hash(const void*p,size_t n){Digest d{};vxl_hunet_hash(p,n,d.data());return d;}
}
struct LegacyExecutionPort::Impl {
 std::shared_ptr<DeviceOwner>owner;BackendIdentity declared;uint32_t profile;LegacyExecutionHooks hooks;TransportFactory factory;std::unique_ptr<IoctlTransport>io;std::mutex mutex;std::optional<WorkItem>active;Digest result_sha{};
 Impl(std::shared_ptr<DeviceOwner>o,BackendIdentity b,uint32_t p,LegacyExecutionHooks h,TransportFactory f):owner(std::move(o)),declared(b),profile(p),hooks(std::move(h)),factory(std::move(f)){}
 bool bound(int fd)const{return owner&&owner->fd()==fd&&owner->mode()==BackendMode::LEGACY_COMPAT;}
 Result<vxl_request>info(ExecutionReceipt*receipt=nullptr){
  vxl_request r{};r.abi_major=1;r.struct_size=176;int rc=io->call(VXL_GET_INFO,&r);if(receipt)receipt->legacy_detail=r;
  if(rc)return Result<vxl_request>::failure({rc,"LEGACY_PORT","original GET_INFO error; no retirement"});
  if(r.abi_major!=1||r.struct_size!=176||r.generation!=declared.device_generation||r.profile_id!=profile)return Result<vxl_request>::failure({-ESTALE,"LEGACY_PORT","fixed old profile or generation changed"});
  if(r.state!=VXL_READY||r.errors||r.pending_w||r.pending_r)return Result<vxl_request>::failure({-EBUSY,"LEGACY_PORT","old GPU or DMA not trusted quiescent"});
  return Result<vxl_request>::success(r);
 }
};
LegacyExecutionPort::LegacyExecutionPort(std::shared_ptr<DeviceOwner>o,BackendIdentity b,uint32_t p,LegacyExecutionHooks h,TransportFactory f):impl_(new Impl(std::move(o),b,p,std::move(h),std::move(f))){}
LegacyExecutionPort::~LegacyExecutionPort()=default;
Result<BackendIdentity>LegacyExecutionPort::probe(int fd,BackendMode mode){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(mode!=BackendMode::LEGACY_COMPAT||!s.bound(fd)||s.declared.provider_flags!=1||(s.profile!=4&&s.profile!=6)||!s.declared.device_generation||s.declared.device_generation>UINT32_MAX||!nonzero_digest(s.declared.hardware_contract)||!nonzero_digest(s.declared.bit)||!nonzero_digest(s.declared.xsa)||!nonzero_digest(s.declared.compatible)||!s.hooks.run||!s.hooks.retire_request)return Result<BackendIdentity>::failure({EINVAL,"LEGACY_PORT","fixed declared NN legacy binding and borrowed owner required"});
 if(!s.io){s.io=s.factory?s.factory(fd):std::unique_ptr<IoctlTransport>(new FdTransport(fd));if(!s.io)return Result<BackendIdentity>::failure({ENOMEM,"LEGACY_PORT","transport allocation"});}
 auto actual=s.info();if(!actual)return Result<BackendIdentity>::failure(*actual.error);auto declared=s.declared;declared.board_identity_verified=false;return Result<BackendIdentity>::success(declared);
}
Result<void>LegacyExecutionPort::execute(int fd,const WorkItem&w,vxnn::CpuPool*pool,ExecutionReceipt&r){
 auto&s=*impl_;std::unique_lock<std::mutex>lock(s.mutex,std::try_to_lock);if(!lock.owns_lock())return denied(EBUSY,"single aggregate execution");
 if(!s.bound(fd)||!s.io||s.active)return denied(EBUSY,"owner/probe missing or aggregate not retired");
 if(w.kind!=WorkKind::LEGACY_EXCLUSIVE||w.app!=AppKind::NN||w.work_index!=0)return denied(EOPNOTSUPP,"one fixed NN aggregate required; no v2 or partial lowering");
 s.active=w;s.result_sha={};r.device_generation=s.declared.device_generation;r.outcome.request=w.request;r.outcome.node=w.node;r.outcome.work_index=w.work_index;
 auto before=s.info(&r);if(!before)return Result<void>::failure(*before.error);
 r.legacy_call_begin_ns=now();auto executed=s.hooks.run(fd,w,pool,r);r.legacy_call_end_ns=now();if(!executed)return executed;
 if(!same(r.outcome.request,w.request)||r.outcome.node!=w.node||r.outcome.work_index!=w.work_index||r.outcome.status!=RequestStatus::COMPLETED||r.outcome.result||r.outcome.completion||r.outcome.legacy||r.outcome.acknowledged||r.outcome.retired||r.linux_detail||!r.output||!r.output->execution_complete||r.output->raw_legacy_json.empty()||r.output->raw_legacy_json.size()>65536)return denied(EPROTO,"old aggregate callback identity/terminal result differs");
 auto after=s.info(&r);if(!after)return Result<void>::failure(*after.error);s.result_sha=hash(r.output->raw_legacy_json.data(),r.output->raw_legacy_json.size());
 r.outcome.legacy.emplace();auto&p=*r.outcome.legacy;p.device_generation=s.declared.device_generation;p.profile_id=s.profile;p.aggregate_complete=true;p.quiescent=true;p.result_sha=s.result_sha;p.info_sha=hash(&*after.value,sizeof(vxl_request));
 // v1 completes its internal per-job ACKs. Persist aggregate raw evidence
 // before the final read-only retirement check; do not invent a v2 ACK.
 return Result<void>::success();
}
Result<void>LegacyExecutionPort::retire_completed(int fd,const WorkItem&w,ExecutionReceipt&r){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.bound(fd)||!s.io||!s.active||!same(s.active->request,w.request)||s.active->work_index!=w.work_index||s.active->node!=w.node||!r.outcome.legacy||r.outcome.completion||r.outcome.acknowledged||r.outcome.legacy->device_generation!=s.declared.device_generation||r.outcome.legacy->profile_id!=s.profile||r.outcome.legacy->result_sha!=s.result_sha||!r.output||hash(r.output->raw_legacy_json.data(),r.output->raw_legacy_json.size())!=s.result_sha)return denied(ESTALE,"aggregate retirement binding differs");
 auto after=s.info(&r);if(!after)return Result<void>::failure(*after.error);r.outcome.legacy->info_sha=hash(&*after.value,sizeof(vxl_request));r.outcome.retired=true;s.active.reset();return Result<void>::success();
}
Result<void>LegacyExecutionPort::retire_request(int fd,RequestKey key){auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);if(!s.bound(fd)||!s.io||s.active)return denied(EBUSY,"unretired aggregate backing");auto quiet=s.info();if(!quiet)return Result<void>::failure(*quiet.error);return s.hooks.retire_request(fd,std::move(key));}
}
