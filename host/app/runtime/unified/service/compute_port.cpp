#include <vxunified/service.hpp>
#include <vxl/v2/jobs.h>
#include <mutex>
#include <time.h>
namespace vxunified {
namespace {
bool same_request(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
Result<void> denied(int e,const char*why){return Result<void>::failure({e,"COMPUTE_PORT",why});}
uint64_t monotonic_ns(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
Result<void>capture_completion(ExecutionReceipt&r,const GPUJobDesc&job,const vxl_v2_completion&d){
 r.linux_detail=d;
 if(d.cookie!=r.submitted_wire_cookie||d.sequence!=r.submitted_sequence||d.device_generation!=r.device_generation)return denied(ESTALE,"raw completion differs from retained submission binding");
 r.outcome.completion.emplace();auto&c=*r.outcome.completion;c.cookie=job.cookie;c.wire_cookie=d.cookie;c.sequence=d.sequence;c.status=d.state;c.result=d.result;c.cycles_valid=d.cycles_valid;if(c.cycles_valid)c.raw_cycles=d.elapsed_raw;c.timing_events={d.submit_ns,d.start_ns,d.done_seen_ns,d.ack_ns,d.complete_ns};
 if(d.state!=VXL_V2_OK||d.result)return denied(d.result?d.result:EPROTO,"GPU completion failed or not terminal OK; snapshot retained");
 r.outcome.status=RequestStatus::COMPLETED;r.outcome.result=0;return Result<void>::success();
}
}
struct ComputeExecutionPort::Impl {
 struct Session {
  std::shared_ptr<DeviceOwner>owner;std::unique_ptr<IoctlTransport>io;ComputeV2Backend backend;
  Session(std::shared_ptr<DeviceOwner>o,std::unique_ptr<IoctlTransport>i):owner(std::move(o)),io(std::move(i)),backend(*io){}
 };
 std::shared_ptr<DeviceOwner>owner;ExecutionHooks hooks;TransportFactory factory;std::shared_ptr<Session>session;std::mutex mutex;
 std::optional<WorkItem>active,non_gpu_active;std::optional<GPUJobDesc>resolved_job;uint64_t sequence=0;bool ack_attempted=false;
 bool bound(int fd)const{return owner&&owner->fd()==fd&&owner->mode()==BackendMode::COMPUTE_V2;}
};
ComputeExecutionPort::ComputeExecutionPort(std::shared_ptr<DeviceOwner>o,ExecutionHooks h,TransportFactory f):impl_(new Impl){impl_->owner=std::move(o);impl_->hooks=std::move(h);impl_->factory=std::move(f);}
ComputeExecutionPort::~ComputeExecutionPort()=default;
Result<BackendIdentity> ComputeExecutionPort::probe(int fd,BackendMode mode){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(mode!=BackendMode::COMPUTE_V2||!s.bound(fd))return Result<BackendIdentity>::failure({EXDEV,"COMPUTE_PORT","explicit compute-v2 owner fd required"});
 if(!s.hooks.finish_gpu||!s.hooks.retire_request)return Result<BackendIdentity>::failure({EINVAL,"COMPUTE_PORT","trusted result/retirement adapters required"});
 if(!s.session){auto io=s.factory?s.factory(fd):std::unique_ptr<IoctlTransport>(new FdTransport(fd));if(!io)return Result<BackendIdentity>::failure({ENOMEM,"COMPUTE_PORT","transport factory failed"});s.session=std::make_shared<Impl::Session>(s.owner,std::move(io));}
 // CAPS never changes mode or submits work.
 return s.session->backend.identity();
}
Result<std::shared_ptr<UnifiedBackend>> ComputeExecutionPort::adapter_backend(){auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);if(!s.session)return Result<std::shared_ptr<UnifiedBackend>>::failure({ENODEV,"COMPUTE_PORT","probe first"});return Result<std::shared_ptr<UnifiedBackend>>::success(std::shared_ptr<UnifiedBackend>(s.session,&s.session->backend));}
Result<std::shared_ptr<UnifiedBackend>>ComputeExecutionPort::native_backend_snapshot(){return adapter_backend();}
Result<void> ComputeExecutionPort::execute(int fd,const WorkItem&w,vxnn::CpuPool*pool,ExecutionReceipt&r){
 auto&s=*impl_;std::unique_lock<std::mutex>lock(s.mutex,std::try_to_lock);
 if(!lock.owns_lock())return denied(EBUSY,"single active port execution");
 if(!s.bound(fd)||!s.session)return denied(EBADF,"borrowed fd/probe lost");
 if(s.active||s.non_gpu_active)return denied(EBUSY,"previous job or non-GPU callback remains active");
 r.outcome.request=w.request;r.outcome.node=w.node;r.outcome.work_index=w.work_index;r.outcome.status=RequestStatus::RUNNING;
 if(w.kind!=WorkKind::GPU){
  if(w.kind==WorkKind::LEGACY_EXCLUSIVE)return denied(EOPNOTSUPP,"legacy requires its aggregate adapter");
  if(!s.hooks.non_gpu)return denied(EOPNOTSUPP,"non-GPU operation not registered");
  // Trusted final output may retire an older DP result through this SAME
  // port. Preserve the single-execution reservation and current source while
  // allowing that confirmed older request to enter its retirement callback.
  s.non_gpu_active=w;auto session=s.session;lock.unlock();
  try{auto result=s.hooks.non_gpu(w,session->backend,pool,r);lock.lock();s.non_gpu_active.reset();return result;}
  catch(...){if(!lock.owns_lock())lock.lock();s.non_gpu_active.reset();throw;}
 }
 // Allocate/copy the control identity before SUBMIT. Once submission starts,
 // any error conservatively retains this active binding for evidence/recovery.
 s.active=w;s.resolved_job.reset();s.sequence=0;s.ack_attempted=false;
 r.submitted_cookie=gpu_work_cookie(w);r.device_generation=gpu_work_epoch(w);
 if(auto*d=std::get_if<DeferredGpuRef>(&w.operation)){
  if(d->registered_name.empty()||d->registered_name.size()>128||d->node!=w.node||!d->cookie||!d->device_generation)return denied(EINVAL,"invalid logical GPU binding");
  if(!s.hooks.prepare_gpu)return denied(EOPNOTSUPP,"trusted deferred preparation hook required");
  r.prepare_begin_ns=monotonic_ns();if(!r.prepare_begin_ns)return denied(EIO,"prepare clock unavailable");
  // Any partial allocation/load is owned by the trusted adapter. Keep active
  // even if the hook fails or throws: neither condition proves retirement.
  auto prepared=[&](){try{auto result=s.hooks.prepare_gpu(w,s.session->backend,r);r.prepare_end_ns=monotonic_ns();return result;}catch(...){r.prepare_end_ns=monotonic_ns();throw;}}();
  if(!prepared)return Result<void>::failure(*prepared.error);
  if(!r.prepare_end_ns||r.prepare_end_ns<r.prepare_begin_ns)return denied(EIO,"prepare clock failed; resources retained");
  r.program_handle=prepared.value->program_handle;r.program_generation=prepared.value->program_generation;
  s.resolved_job=*prepared.value;
  if(s.resolved_job->cookie!=d->cookie||s.resolved_job->device_generation!=d->device_generation)return Result<void>::failure({ESTALE,"COMPUTE_PORT","resolved cookie="+std::to_string(s.resolved_job->cookie)+" epoch="+std::to_string(s.resolved_job->device_generation)+" differs logical cookie="+std::to_string(d->cookie)+" epoch="+std::to_string(d->device_generation)});
 }else if(auto*j=std::get_if<GPUJobDesc>(&w.operation)){s.resolved_job=*j;}
 else return denied(EINVAL,"GPU operation type differs");
 const auto&job=*s.resolved_job;
 r.submitted_cookie=job.cookie;r.program_handle=job.program_handle;r.program_generation=job.program_generation;r.device_generation=job.device_generation;
 auto program=s.session->backend.program_identity(job);if(!program)return Result<void>::failure(*program.error);r.program_entry_id=program.value->program_sha;
 r.submit_begin_ns=monotonic_ns();auto submitted=s.session->backend.submit(job);r.submit_end_ns=monotonic_ns();auto wire=s.session->backend.submitted_wire_cookie(job.cookie);if(wire)r.submitted_wire_cookie=*wire.value;if(!submitted)return Result<void>::failure(*submitted.error);if(!wire)return Result<void>::failure(*wire.error);s.sequence=*submitted.value;r.submitted_sequence=s.sequence;
 uint64_t now=s.session->io->now_ns();if(!now||job.deadline_ns<=now)return denied(ETIMEDOUT,"deadline elapsed after submit; execution retained");
 r.wait_begin_ns=monotonic_ns();auto wait=s.session->backend.wait(job.cookie,job.deadline_ns-now);r.wait_end_ns=monotonic_ns();if(!wait)return wait;
 r.read_begin_ns=monotonic_ns();auto detail=s.session->backend.detail(s.sequence,job.cookie);r.read_end_ns=monotonic_ns();if(!detail)return Result<void>::failure(*detail.error);
 // No userspace ACK here: Runtime must persist this exact raw receipt first.
 return capture_completion(r,job,*detail.value);
}
Result<void>ComputeExecutionPort::recover_completion(int fd,const WorkItem&w,ExecutionReceipt&r,uint64_t timeout){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(!timeout||timeout>1000000000ULL)return denied(EINVAL,"late observation limit is (0,1s]");
 if(!s.bound(fd)||!s.session)return denied(EBADF,"late observation owner lost");
 if(!s.active||!same_request(s.active->request,w.request)||s.active->node!=w.node||s.active->work_index!=w.work_index||!s.resolved_job||!s.sequence||s.sequence!=r.submitted_sequence||s.resolved_job->cookie!=r.submitted_cookie||s.resolved_job->device_generation!=r.device_generation||!r.submitted_wire_cookie||s.ack_attempted||r.outcome.acknowledged||r.linux_detail||r.outcome.completion)return denied(ESTALE,"only original unobserved unACKed submission may be recovered");
 auto identity=s.session->backend.identity();if(!identity)return Result<void>::failure(*identity.error);
 if(identity.value->device_generation!=r.device_generation||identity.value->hardware_contract!=r.binding.hardware||identity.value->bit!=r.binding.bit||identity.value->xsa!=r.binding.xsa||identity.value->compatible!=r.binding.compatible||identity.value->provider_flags!=r.binding.provider_flags)return denied(ESTALE,"same-owner late observation identity changed");
 auto wire=s.session->backend.submitted_wire_cookie(r.submitted_cookie);if(!wire||*wire.value!=r.submitted_wire_cookie)return denied(ESTALE,"late observation wire cookie changed");
 auto waited=s.session->backend.wait(r.submitted_cookie,timeout);if(!waited)return waited;
 auto detail=s.session->backend.detail(s.sequence,r.submitted_cookie);if(!detail)return Result<void>::failure(*detail.error);
 return capture_completion(r,*s.resolved_job,*detail.value);
}
Result<void> ComputeExecutionPort::retire_completed(int fd,const WorkItem&w,ExecutionReceipt&r){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.bound(fd)||!s.session)return denied(EBADF,"retirement owner lost");
 if(!s.active||!same_request(s.active->request,w.request)||s.active->node!=w.node||s.active->work_index!=w.work_index||!s.resolved_job||!s.sequence||!r.outcome.completion||r.outcome.completion->sequence!=s.sequence||r.outcome.completion->cookie!=s.resolved_job->cookie||!r.submitted_wire_cookie||r.outcome.completion->wire_cookie!=r.submitted_wire_cookie||!r.linux_detail||r.linux_detail->sequence!=s.sequence||r.linux_detail->cookie!=r.submitted_wire_cookie||s.ack_attempted)return denied(ESTALE,"retirement binding/ACK attempt differs");
 s.ack_attempted=true;auto ack=s.session->backend.ack_reap(s.sequence);if(!ack)return ack;
 r.outcome.acknowledged=true;r.outcome.retired=true;
 auto finish=s.hooks.finish_gpu(w,s.session->backend,r);if(!finish)return finish;
 s.active.reset();s.resolved_job.reset();s.sequence=0;return Result<void>::success();
}
Result<void> ComputeExecutionPort::retire_request(int fd,RequestKey request){auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);if(!s.bound(fd)||!s.session)return denied(EBADF,"request retirement owner lost");if(s.active||(s.non_gpu_active&&same_request(s.non_gpu_active->request,request)))return denied(EBUSY,"GPU retirement unconfirmed or current non-GPU callback active");return s.hooks.retire_request(std::move(request),s.session->backend);}
}
