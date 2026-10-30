#include <vxunified/daemon.hpp>
#include <vxunified/journal.hpp>
#include <vxunified/exit_retention.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace vxunified {
namespace {
Result<void>unregistered(){return Result<void>::failure({EOPNOTSUPP,"DAEMON_ADAPTER","application operation is not registered"});}
struct Bindings{ApplicationSet set;};
}
struct Daemon::State{
 detail::ExitRetention exit_retention;
 std::shared_ptr<DeviceOwner>owner;std::shared_ptr<ExecutionPort>port;std::shared_ptr<Runtime>runtime;std::shared_ptr<ServiceControl>control;std::shared_ptr<RpcServer>rpc;std::shared_ptr<EvidenceJournal>journal;
 std::mutex mutex,join_mutex,serve_mutex;std::condition_variable condition;std::thread worker;
 std::atomic<bool>closing{false},worker_failed{false};bool wake=false,stopped=false,native_turn=false;std::optional<vxnn::Error>error;
 std::shared_ptr<Bindings>bindings;std::optional<vxnn::Error>shutdown_error;
 Result<void>notify_native(){std::lock_guard<std::mutex>lock(mutex);if(closing.load()||worker_failed.load())return Result<void>::failure({ESHUTDOWN,"DAEMON","native admission is closing"});wake=true;condition.notify_one();return Result<void>::success();}
 void notify(){std::lock_guard<std::mutex>lock(mutex);wake=true;condition.notify_one();}
 void pump()noexcept{
  try{
   for(;;){
    {std::unique_lock<std::mutex>lock(mutex);condition.wait(lock,[&](){return closing.load()||wake;});if(closing.load())break;wake=false;}
    while(!closing.load()){
     auto mode=runtime->advance_mode();if(!mode){{std::lock_guard<std::mutex>lock(mutex);if(!error)error=*mode.error;}worker_failed=true;return;}
     bool prepared=false;if(runtime->stats().phase==RuntimePhase::READY){
      // Runtime methods have returned and released their locks. Fixed program
      // reservations must be free before even control-first preparation runs.
      if(bindings->set.settle_native){auto settled=bindings->set.settle_native();if(!settled){{std::lock_guard<std::mutex>lock(mutex);if(!error)error=*settled.error;}worker_failed=true;return;}}
      auto native=[&](){return bindings->set.prepare_native?bindings->set.prepare_native():Result<bool>::success(false);};
      auto p=[&](){auto first=native_turn?native():control->prepare_one();if(!first||*first.value)return first;return native_turn?control->prepare_one():native();}();
      if(!p){{std::lock_guard<std::mutex>lock(mutex);if(!error)error=*p.error;}worker_failed=true;return;}
      prepared=*p.value;native_turn=!native_turn;
     }
     if(closing.load())break;
     auto r=runtime->run_one();if(r)continue;if(r.error->code==EAGAIN&&runtime->stats().phase==RuntimePhase::READY){if(prepared)continue;break;}
     {std::lock_guard<std::mutex>lock(mutex);if(!error)error=*r.error;}worker_failed=true;
     // One bounded same-owner late observation. A later explicit recovery may
     // continue evidence delivery; admission stays closed after the first error.
     auto recovery=runtime->recover_pending();(void)recovery;return;
    }
   }
  }catch(...){worker_failed=true;/* Runtime retains original pending receipt; never execute again. */}
 }
};
Daemon::Daemon(std::shared_ptr<State>s):state_(std::move(s)){}
Daemon::~Daemon(){try{if(shutdown())return;}catch(...){/* Even an error string can exhaust memory. */}state_->closing=true;state_->condition.notify_all();state_->exit_retention.retain(state_);}
Result<std::shared_ptr<Daemon>>Daemon::start(const DaemonConfig&cfg,ApplicationBuilder builder,DeviceOpen opener,DeviceClose closer,TransportFactory transport){
 try{
  if(!builder||!nonzero_digest(cfg.config_sha))return Result<std::shared_ptr<Daemon>>::failure({EINVAL,"DAEMON","trusted startup configuration/builder required"});
  if((cfg.service.mode==BackendMode::COMPUTE_V2&&cfg.legacy_profile)||(cfg.service.mode==BackendMode::LEGACY_COMPAT&&cfg.legacy_profile!=4&&cfg.legacy_profile!=6))return Result<std::shared_ptr<Daemon>>::failure({EINVAL,"DAEMON","explicit fixed startup profile/mode required"});
  if(cfg.fast_runtime&&cfg.service.mode!=BackendMode::COMPUTE_V2)return Result<std::shared_ptr<Daemon>>::failure({EOPNOTSUPP,"DAEMON","FAST unavailable for legacy backend"});
  // Establish the lifecycle owner before acquiring any resource. Failures
  // after builder binding or startup evidence must retire that partial graph.
  auto s=std::make_shared<State>();auto daemon=std::shared_ptr<Daemon>(new Daemon(s));auto owner=DeviceOwner::acquire(cfg.service,std::move(opener),std::move(closer));if(!owner)return Result<std::shared_ptr<Daemon>>::failure(*owner.error);s->owner=*owner.value;
  auto journal=EvidenceJournal::open(cfg.evidence_dir);if(!journal)return Result<std::shared_ptr<Daemon>>::failure(*journal.error);s->journal=*journal.value;
  auto bindings=std::make_shared<Bindings>();s->bindings=bindings;std::shared_ptr<ComputeExecutionPort>compute;
  if(cfg.service.mode==BackendMode::COMPUTE_V2){
   ExecutionHooks h;
   h.prepare_gpu=[bindings](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto&f=bindings->set.compute.prepare_gpu;return f?f(w,b,r):Result<GPUJobDesc>::failure({EOPNOTSUPP,"DAEMON_ADAPTER","native deferred preparation is not registered"});};
   h.non_gpu=[bindings](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){auto&f=bindings->set.compute.non_gpu;return f?f(w,b,p,r):unregistered();};
   h.finish_gpu=[bindings](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto&f=bindings->set.compute.finish_gpu;return f?f(w,b,r):unregistered();};
   h.retire_request=[bindings](RequestKey k,UnifiedBackend&b){auto&f=bindings->set.compute.retire_request;return f?f(std::move(k),b):unregistered();};
   compute=std::make_shared<ComputeExecutionPort>(s->owner,std::move(h),std::move(transport));s->port=compute;
  }else{
   LegacyExecutionHooks h;
   h.run=[bindings](int fd,const WorkItem&w,vxnn::CpuPool*p,ExecutionReceipt&r){auto&f=bindings->set.legacy.run;return f?f(fd,w,p,r):unregistered();};
   h.retire_request=[bindings](int fd,RequestKey k){auto&f=bindings->set.legacy.retire_request;return f?f(fd,std::move(k)):unregistered();};
   s->port=std::make_shared<LegacyExecutionPort>(s->owner,cfg.identity,cfg.legacy_profile,std::move(h),std::move(transport));
  }
  auto log=s->journal;
  auto bound_identity=cfg.identity;
  if(compute){
   // DeviceOwner already holds the one private owner lock and device fd. Only
   // CAPS may bootstrap its naturally observed generation; every static field
   // remains pinned and Runtime independently rechecks this SAME port/fd.
   auto actual=compute->probe(s->owner->fd(),cfg.service.mode);if(!actual)return Result<std::shared_ptr<Daemon>>::failure(*actual.error);const auto&a=*actual.value;
   if(!cfg.identity.device_generation||!a.device_generation||a.hardware_contract!=cfg.identity.hardware_contract||a.bit!=cfg.identity.bit||a.xsa!=cfg.identity.xsa||a.compatible!=cfg.identity.compatible||a.provider_flags!=cfg.identity.provider_flags)return Result<std::shared_ptr<Daemon>>::failure({EXDEV,"DAEMON_BOOTSTRAP","complete declared hardware/provider identity differs on locked owner fd"});
   bound_identity=a;
  }
  auto runtime=Runtime::start(s->owner,bound_identity,cfg.scope,s->port,[log](const ExecutionReceipt&r,DeliveryStage stage){return log->save_work(r,stage);},[log](const PublishedResult&r){return log->save_result(r);});if(!runtime)return Result<std::shared_ptr<Daemon>>::failure(*runtime.error);s->runtime=*runtime.value;
  StartupContext context;context.borrowed_fd=s->owner->fd();context.mode=cfg.service.mode;context.identity=s->runtime->identity();context.legacy_profile=cfg.legacy_profile;context.runtime=s->runtime;context.results=s->runtime->trusted_results();context.journal=s->journal;context.scope=cfg.scope;if(compute){auto b=compute->adapter_backend();if(!b)return Result<std::shared_ptr<Daemon>>::failure(*b.error);context.compute_backend=*b.value;}
  context.private_dir=cfg.service.private_dir;context.boot_id=cfg.boot_id;context.fast_runtime=cfg.fast_runtime;
  if(compute){auto backend=std::dynamic_pointer_cast<ComputeV2Backend>(context.compute_backend);if(!backend)return Result<std::shared_ptr<Daemon>>::failure({EPROTO,"DAEMON","concrete compute backend required"});auto policy=backend->set_fast_runtime(cfg.fast_runtime);if(!policy)return Result<std::shared_ptr<Daemon>>::failure(*policy.error);}
  std::weak_ptr<State>weak=s;context.notify_work=[weak](){auto state=weak.lock();return state?state->notify_native():Result<void>::failure({ESHUTDOWN,"DAEMON","native service owner is gone"});};
  auto apps=builder(context);if(!apps)return Result<std::shared_ptr<Daemon>>::failure(*apps.error);bindings->set=*apps.value;
  if(!bindings->set.entries.empty()||bindings->set.prepare_native){
   bool complete=compute?bool(bindings->set.compute.finish_gpu)&&bool(bindings->set.compute.retire_request):bool(bindings->set.legacy.run)&&bool(bindings->set.legacy.retire_request);
   if(!complete)return Result<std::shared_ptr<Daemon>>::failure({EINVAL,"DAEMON","nonempty registry needs native result/retirement hooks"});
  }
  std::vector<std::string>ids;for(const auto&a:bindings->set.entries)ids.push_back(a.id);
  auto control=ServiceControl::start(s->runtime,context.identity,cfg.boot_id,std::move(bindings->set.entries),std::move(bindings->set.display),std::move(bindings->set.export_result),true,[log,scope=cfg.scope](const PreparationReceipt&r){return log->save_preparation(r,scope);});if(!control)return Result<std::shared_ptr<Daemon>>::failure(*control.error);s->control=*control.value;
  auto saved=log->save_startup(context.identity,cfg.service.mode,cfg.boot_id,cfg.config_sha,cfg.scope,ids,bindings->set.installation_sha);if(!saved)return Result<std::shared_ptr<Daemon>>::failure(*saved.error);
  auto rpc=RpcServer::start(cfg.service.private_dir,cfg.boot_id,[weak](const RpcRequest&q)->Result<std::string>{
   auto state=weak.lock();if(!state||state->closing.load())return Result<std::string>::failure({ESHUTDOWN,"DAEMON","service admission is closing"});
   bool admission_open=!state->worker_failed.load()&&state->runtime->stats().phase==RuntimePhase::READY;
   auto r=state->control->handle(q,admission_open);if(q.command=="submit"||q.command=="cancel"||q.command=="mode")state->notify();return r;
  });if(!rpc)return Result<std::shared_ptr<Daemon>>::failure(*rpc.error);s->rpc=*rpc.value;
  // The wrapper also owns all earlier partial startup states.
  s->worker=std::thread([s](){s->pump();});
  if(bindings->set.activate){auto active=bindings->set.activate();if(!active){{std::lock_guard<std::mutex>lock(s->mutex);if(!s->error)s->error=*active.error;}s->closing=true;s->condition.notify_all();auto cleanup=daemon->shutdown();(void)cleanup;return Result<std::shared_ptr<Daemon>>::failure(*active.error);}}
  return Result<std::shared_ptr<Daemon>>::success(std::move(daemon));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<Daemon>>::failure({ENOMEM,"DAEMON","startup allocation"});}catch(const std::exception&e){return Result<std::shared_ptr<Daemon>>::failure({EIO,"DAEMON",e.what()});}
}
Result<void>Daemon::serve_one(uint32_t timeout){
 auto&s=*state_;std::unique_lock<std::mutex>lock(s.serve_mutex,std::try_to_lock);if(!lock.owns_lock())return Result<void>::failure({EBUSY,"DAEMON","single control accept loop"});if(s.closing.load())return Result<void>::failure({ESHUTDOWN,"DAEMON","control loop stopped"});
 // A failed execution/preparation owner keeps its first error and closes new
 // admission, but existing requests must remain queryable through the same RPC.
 // Do not poll native ingress: its wake callback correctly refuses a failed
 // worker and must not prevent read-only status/result/cancel queries.
 if(s.worker_failed.load())return s.rpc->serve_one(std::max(timeout,uint32_t(1)));
 if(s.bindings->set.poll_native){auto r=s.bindings->set.poll_native(std::min(timeout,uint32_t(50)));if(!r&&!s.worker_failed.load())return r;return s.rpc->serve_if_ready(std::max(timeout,uint32_t(1)));}return s.rpc->serve_one(timeout);
}
Result<void>Daemon::shutdown(){
 auto&s=*state_;std::lock_guard<std::mutex>join(s.join_mutex);if(s.stopped)return Result<void>::success();if(s.shutdown_error)return Result<void>::failure(*s.shutdown_error);s.closing=true;s.condition.notify_all();
 auto failed=[&](const vxnn::Error&e){s.shutdown_error=e;{std::lock_guard<std::mutex>lock(s.mutex);if(!s.error)s.error=e;}return Result<void>::failure(e);};
 auto call=[&](const std::function<Result<void>()>&hook)->Result<void>{try{return hook?hook():Result<void>::success();}catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"NATIVE_LIFECYCLE","native shutdown allocation"});}catch(const std::exception&e){return Result<void>::failure({EIO,"NATIVE_LIFECYCLE",e.what()});}catch(...){return Result<void>::failure({EIO,"NATIVE_LIFECYCLE","native shutdown exception"});}};
 // Cancel control metadata before a native ingress hook can fail or wait.
 // An already executing non-preemptible task still finishes its original path.
  using Hook=std::function<Result<void>()>;Hook empty;
  auto hook=[&](Hook ApplicationSet::*member)->const Hook&{return s.bindings?(s.bindings->set.*member):empty;};
  if(s.control){auto admission=s.control->quiesce_preparations();if(!admission){if(s.worker.joinable())s.worker.join();return failed(*admission.error);}}
  auto quiesce=call(hook(&ApplicationSet::quiesce));if(!quiesce){if(s.worker.joinable())s.worker.join();return failed(*quiesce.error);}
 std::lock_guard<std::mutex>control(s.serve_mutex);if(s.worker.joinable())s.worker.join();
  auto display=call(hook(&ApplicationSet::retire_display));if(!display)return failed(*display.error);
  if(s.runtime){auto stopped=s.runtime->shutdown();if(!stopped)return failed(*stopped.error);auto drained=s.runtime->drain_shutdown_results();if(!drained)return failed(*drained.error);}
  auto resources=call(hook(&ApplicationSet::release_resources));if(!resources)return failed(*resources.error);
  // Display -> ResultStore -> execution port -> Bindings can otherwise retain
  // this callback and the device after successful retirement. Drop it only
  // after every shutdown stage succeeds; failed retirement keeps its owners.
  if(s.bindings)s.bindings->set.retire_display={};
 s.stopped=true;return Result<void>::success();
}
Result<vxnn::Error>Daemon::first_error()const{
 auto&s=*state_;auto r=s.runtime->first_error();if(r)return r;{std::lock_guard<std::mutex>lock(s.mutex);if(s.error)return Result<vxnn::Error>::success(*s.error);}if(s.worker_failed.load())return Result<vxnn::Error>::success({EIO,"DAEMON","worker exception; original Runtime pending state retained"});return r;
}
}
